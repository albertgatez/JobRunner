#include "./job_manager.hpp"
#include <algorithm>
#include <csignal>
#include <exception>

namespace jobrunner {

namespace {
constexpr const char* kInterruptedDiagnostic = "interrumpido por reinicio del servicio";
constexpr const char* kShutdownDiagnostic = "cancelado por cierre del servicio";
constexpr const char* kLaunchFailedDiagnostic = "no se pudo crear el proceso";
}  // namespace

// Calcula hash para deduplicacion por comando/args/origen.
// k: llave de deduplicacion.
std::size_t JobManager::DedupKeyHash::operator()(const DedupKey& k) const {
    std::hash<std::string> h;
    std::size_t seed = h(k.command) ^ (h(k.client_origin) << 1);
    for (const auto& a : k.args) {
        seed ^= h(a) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
    }
    return seed;
}

// Construye el orquestador de jobs.
// store: backend de almacenamiento.
// launcher: lanzador de procesos.
// logger: bitacora operacional.
// max_concurrent_jobs: limite de jobs ejecutandose a la vez.
// max_queue_size: capacidad maxima de la cola antes de rechazar.
JobManager::JobManager(IJobStore& store, IProcessLauncher& launcher, Logger& logger,
                       std::size_t max_concurrent_jobs, std::size_t max_queue_size,
                       std::chrono::milliseconds cancel_timeout)
    : store_(store),
      launcher_(launcher),
      logger_(logger),
      max_concurrent_jobs_(max_concurrent_jobs),
      max_queue_size_(max_queue_size),
      cancel_timeout_(cancel_timeout) {}

// Crea, deduplica, encola o rechaza un job segun el cupo disponible.
// command: comando solicitado.
// args: argumentos del comando.
// client_origin: identificador del cliente solicitante.
SubmitResult JobManager::submit(const std::string& command,
                                 const std::vector<std::string>& args,
                                 const std::string& client_origin) {
    SubmitResult result;

    // RF-15: durante el cierre no se aceptan trabajos nuevos.
    if (shutting_down_) {
        result.accepted = false;
        result.error_message = "el servicio se esta cerrando, no acepta trabajos nuevos";
        return result;
    }

    if (command.empty()) {
        result.accepted = false;
        result.error_message = "el comando no puede estar vacio";
        return result;
    }

    prune_dedup_window();

    DedupKey key{command, args, client_origin};
    auto dup_it = recent_submissions_.find(key);
    if (dup_it != recent_submissions_.end()) {
        result.accepted = true;
        result.was_duplicate = true;
        result.job_id = dup_it->second.first;
        logger_.info("solicitud duplicada detectada, reutilizando job", result.job_id);
        return result;
    }
    // RF-25 / RNF-29: si no hay cupo y la cola ya esta llena, se rechaza
    // antes de crear el job. No queda registro ni id para la solicitud.
    if (live_processes_.size() >= max_concurrent_jobs_ && queue_.size() >= max_queue_size_) {
        result.accepted = false;
        result.error_message = "cola llena (" + std::to_string(max_queue_size_) +
                                " jobs en espera), intenta mas tarde";
        logger_.info("solicitud rechazada: cola llena");
        return result;
    }
    JobId id = store_.create(command, args);
    recent_submissions_[key] = {id, std::chrono::steady_clock::now()};

    logger_.info("job recibido: " + command, id);

    result.accepted = true;
    result.job_id = id;
    if (live_processes_.size() < max_concurrent_jobs_) {
        dispatch(id);
    } else {
        queue_.push_back(id);
        logger_.info("job encolado, en espera de cupo", id);
    }
    return result;
}

// Lanza efectivamente un job en estado Queued y ocupa un cupo.
// id: identificador del job.
void JobManager::dispatch(JobId id) {
    auto job = store_.get(id);
    if (!job || job->state != JobState::Queued) return;

    pid_t pid = -1;
    try {
        pid = launcher_.launch(
            job->command, job->args,
            [this, id](pid_t /*pid*/, bool is_stderr, std::string_view data) {
                on_child_output(id, is_stderr, data);
            },
            [this, id](pid_t /*pid*/, int exit_code, int term_signal) {
                on_child_exit(id, exit_code, term_signal);
            });
    } catch (const std::exception& e) {
        // Se despacha desde submit, on_child_exit y recover: un fallo de un
        // job no debe llegar hasta el event loop ni tumbar el servidor (RNF-09).
        on_launch_failed(id, e.what());
        return;
    }

    auto fingerprint = launcher_.fingerprint(pid);
    store_.update(id, [pid, &fingerprint](Job& j) {
        j.transition_to(JobState::Running);
        j.started_at = Clock::now();
        j.pid = pid;
        if (fingerprint) {
            j.pid_start_time = fingerprint->start_time;
            j.boot_id = fingerprint->boot_id;
        }
    });
    live_processes_[id] = pid;
    logger_.info("job en ejecucion (pid " + std::to_string(pid) + ")", id);
}

// Marca FAILED un job cuyo proceso no se pudo crear (RF-29) y lo retira de la
// ventana de duplicados para no bloquear un reenvio legitimo (RF-27).
// id: identificador del job.
// reason: causa del fallo.
void JobManager::on_launch_failed(JobId id, const std::string& reason) {
    const std::string diagnostic = std::string(kLaunchFailedDiagnostic) + ": " + reason;
    store_.update(id, [&diagnostic](Job& j) {
        j.transition_to(JobState::Failed);
        j.finished_at = Clock::now();
        j.diagnostic = diagnostic;
    });
    logger_.error(diagnostic, id);
    std::erase_if(recent_submissions_, [id](const auto& entry) { return entry.second.first == id; });
}

// Si hay cupo libre y jobs en cola, despacha el mas antiguo (FIFO).
void JobManager::dispatch_next_queued() {
    if (shutting_down_) return;  // la cola se conserva para el siguiente arranque
    while (live_processes_.size() < max_concurrent_jobs_ && !queue_.empty()) {
        JobId next_id = queue_.front();
        queue_.pop_front();
        dispatch(next_id);
    }
}

// Acumula salida stdout/stderr del proceso.
// id: identificador del job.
// is_stderr: true si el bloque viene de stderr.
// data: bytes de salida capturados.
void JobManager::on_child_output(JobId id, bool is_stderr, std::string_view data) {
    store_.update(id, [is_stderr, data](Job& j) {
        if (is_stderr) {
            j.stderr_data.append(data);
        } else {
            j.stdout_data.append(data);
        }
    });
}

// Finaliza estado del job, libera su cupo y despacha el siguiente en cola.
// id: identificador del job.
// exit_code: codigo de retorno.
// term_signal: senal de terminacion, 0 si no aplica.
void JobManager::on_child_exit(JobId id, int exit_code, int term_signal) {
    store_.update(id, [exit_code, term_signal](Job& j) {
        if (j.state == JobState::Canceled) return;  // already finalized by cancel()
        JobState final_state =
            (term_signal == 0 && exit_code == 0) ? JobState::Succeeded : JobState::Failed;
        j.transition_to(final_state);
        j.finished_at = Clock::now();
        j.exit_code = exit_code;
        if (term_signal != 0) j.exit_signal = term_signal;
    });
    logger_.info("job finalizado (codigo " + std::to_string(exit_code) + ")", id);
    // El cupo se libera hasta que el proceso realmente termino (ADR-003),
    // sin importar si el job ya estaba CANCELED mientras corria.
    auto live = live_processes_.find(id);
    if (live != live_processes_.end()) {
        // RNF-30 (INC-02): el lider ya termino, pero un subproceso de su grupo
        // pudo ignorar SIGTERM. Al cerrar no debe sobrevivir ninguno.
        if (shutting_down_) launcher_.send_signal(live->second, SIGKILL);
        live_processes_.erase(live);
    }
    // Si el proceso termino por su cuenta (o atendio el SIGTERM) antes de
    // que expirara el timeout de escalamiento, no hace falta mandar SIGKILL.
    auto timer_it = escalation_timers_.find(id);
    if (timer_it != escalation_timers_.end()) {
        launcher_.cancel_timer(timer_it->second);
        escalation_timers_.erase(timer_it);
    }
    dispatch_next_queued();
}

// Solicita cancelacion de un job. Si esta en QUEUED, lo retira de la cola.
// id: identificador del job.
bool JobManager::cancel(JobId id) {
    auto job = store_.get(id);
    if (!job) return false;

    if (job->state == JobState::Succeeded || job->state == JobState::Failed ||
        job->state == JobState::Canceled) {
        return true;  // idempotent: already finished, nothing to do (RF-26)
    }

    if (job->state == JobState::Queued) {
        queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
        store_.update(id, [](Job& j) {
            j.transition_to(JobState::Canceled);
            j.finished_at = Clock::now();
        });
        return true;
    }

    // Running: ask nicely first (SIGTERM). Si el proceso lo ignora, un
    // temporizador no bloqueante escala a SIGKILL tras cancel_timeout_
    // (ADR-006). El Job ya queda CANCELED aqui; el escalamiento solo decide
    // como termina el proceso, no el estado que ve el cliente.
    if (job->pid) {
        pid_t pid = *job->pid;
        launcher_.send_signal(pid, SIGTERM);
        escalation_timers_[id] = launcher_.schedule_once(cancel_timeout_, [this, id, pid] {
            escalation_timers_.erase(id);
            if (live_processes_.count(id)) {  // sigue vivo: ignoro el SIGTERM
                launcher_.send_signal(pid, SIGKILL);
                logger_.info("el proceso no termino tras SIGTERM, enviando SIGKILL al pid " +
                                 std::to_string(pid),
                             id);
            }
        });
    }
    store_.update(id, [](Job& j) {
        j.transition_to(JobState::Canceled);
        j.finished_at = Clock::now();
    });
    return true;
}

// Reconstruye el estado tras un reinicio (ADR-005).
RecoveryReport JobManager::recover() {
    RecoveryReport report;
    std::vector<Job> queued;

    for (const Job& job : store_.list(std::nullopt)) {
        if (job.state == JobState::Queued) {
            queued.push_back(job);
            continue;
        }
        if (job.state != JobState::Running) continue;  // terminal: historia, sin cambios

        // El servidor anterior murio sin cerrar este job. Si su proceso (o
        // algun hijo de su grupo) sigue vivo, ya nadie lee su salida ni lo
        // espera: se termina, verificando antes que sea el mismo proceso.
        std::size_t terminated = 0;
        if (job.pid && job.pid_start_time) {
            terminated = launcher_.terminate_orphan_group(
                *job.pid, ProcessFingerprint{job.boot_id, *job.pid_start_time});
        }
        report.orphans_terminated += terminated;

        store_.update(job.id, [](Job& j) {
            j.transition_to(JobState::Failed);
            j.finished_at = Clock::now();
            j.diagnostic = kInterruptedDiagnostic;
        });
        ++report.interrupted;
        logger_.info(std::string(kInterruptedDiagnostic) + ", marcado FAILED" +
                         (terminated > 0 ? " (" + std::to_string(terminated) +
                                               " proceso(s) huerfano(s) terminado(s))"
                                         : ""),
                     job.id);
    }

    // FIFO por hora de llegada; el id desempata (es creciente).
    std::stable_sort(queued.begin(), queued.end(), [](const Job& a, const Job& b) {
        return a.received_at < b.received_at;
    });
    for (const Job& job : queued) {
        queue_.push_back(job.id);
        logger_.info("job reencolado tras reinicio", job.id);
    }
    report.requeued = queued.size();

    logger_.info("recuperacion completada: " + std::to_string(report.requeued) +
                 " reencolado(s), " + std::to_string(report.interrupted) +
                 " interrumpido(s), " + std::to_string(report.orphans_terminated) +
                 " proceso(s) huerfano(s) terminado(s)");
    dispatch_next_queued();
    return report;
}

// Cancela los jobs en ejecucion y bloquea nuevas solicitudes.
void JobManager::begin_shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;

    for (const auto& [id, pid] : live_processes_) {
        launcher_.send_signal(pid, SIGTERM);
        store_.update(id, [](Job& j) {
            if (j.state != JobState::Running) return;  // ya cancelado por un cliente
            j.transition_to(JobState::Canceled);
            j.finished_at = Clock::now();
            j.diagnostic = kShutdownDiagnostic;
        });
        logger_.info(std::string(kShutdownDiagnostic) + " (SIGTERM al pid " +
                         std::to_string(pid) + ")",
                     id);
    }
}

// Fuerza la terminacion de los procesos que ignoraron SIGTERM.
void JobManager::kill_live_processes() {
    for (const auto& [id, pid] : live_processes_) {
        launcher_.send_signal(pid, SIGKILL);
        logger_.info("el proceso no termino tras SIGTERM, enviando SIGKILL al pid " +
                         std::to_string(pid),
                     id);
    }
}

// Indica si queda algun proceso por recolectar.
bool JobManager::has_live_processes() const { return !live_processes_.empty(); }

// Consulta un job por id.
// id: identificador del job.
std::optional<Job> JobManager::status(JobId id) const { return store_.get(id); }

// Lista jobs con filtro opcional por estado.
// state_filter: estado opcional de filtrado.
std::vector<Job> JobManager::list(std::optional<JobState> state_filter) const {
    return store_.list(state_filter);
}

// Limpia entradas de deduplicacion vencidas.
void JobManager::prune_dedup_window() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = recent_submissions_.begin(); it != recent_submissions_.end();) {
        if (now - it->second.second > kDedupWindow) {
            it = recent_submissions_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace jobrunner