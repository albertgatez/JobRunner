#include "./job_manager.hpp"

#include <csignal>

namespace jobrunner {

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
JobManager::JobManager(IJobStore& store, IProcessLauncher& launcher, Logger& logger)
    : store_(store), launcher_(launcher), logger_(logger) {}

// Crea o deduplica un job y dispara ejecucion.
// command: comando solicitado.
// args: argumentos del comando.
// client_origin: identificador del cliente solicitante.
SubmitResult JobManager::submit(const std::string& command,
                                 const std::vector<std::string>& args,
                                 const std::string& client_origin) {
    SubmitResult result;

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

    JobId id = store_.create(command, args);
    recent_submissions_[key] = {id, std::chrono::steady_clock::now()};

    logger_.info("job recibido: " + command, id);

    result.accepted = true;
    result.job_id = id;
    dispatch(id);
    return result;
}

// Inicia ejecucion de un job queued.
// id: identificador del job.
void JobManager::dispatch(JobId id) {
    auto job = store_.get(id);
    if (!job || job->state != JobState::Queued) return;

    pid_t pid = launcher_.launch(
        job->command, job->args,
        [this, id](pid_t /*pid*/, bool is_stderr, std::string_view data) {
            on_child_output(id, is_stderr, data);
        },
        [this, id](pid_t /*pid*/, int exit_code, int term_signal) {
            on_child_exit(id, exit_code, term_signal);
        });

    store_.update(id, [pid](Job& j) {
        j.transition_to(JobState::Running);
        j.started_at = Clock::now();
        j.pid = pid;
    });
    logger_.info("job en ejecucion (pid " + std::to_string(pid) + ")", id);
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

// Finaliza estado del job al terminar el hijo.
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
}

// Solicita cancelacion de un job.
// id: identificador del job.
bool JobManager::cancel(JobId id) {
    auto job = store_.get(id);
    if (!job) return false;

    if (job->state == JobState::Succeeded || job->state == JobState::Failed ||
        job->state == JobState::Canceled) {
        return true;  // idempotent: already finished, nothing to do (RF-26)
    }

    if (job->state == JobState::Queued) {
        store_.update(id, [](Job& j) {
            j.transition_to(JobState::Canceled);
            j.finished_at = Clock::now();
        });
        return true;
    }

    // Running: ask nicely first. Escalating to SIGKILL after a timeout is a
    // Hito 2 addition (RF-30) — tracked as an open item, not implemented yet.
    if (job->pid) {
        launcher_.send_signal(*job->pid, SIGTERM);
    }
    store_.update(id, [](Job& j) {
        j.transition_to(JobState::Canceled);
        j.finished_at = Clock::now();
    });
    return true;
}

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