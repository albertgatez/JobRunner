#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/logger.hpp"
#include "./job.hpp"
#include "./job_store.hpp"
#include "../process/process_launcher.hpp"

namespace jobrunner {

/**
 * Resultado de una operacion submit.
 */
struct SubmitResult {
    bool accepted{false};
    JobId job_id{};
    bool was_duplicate{false};
    std::string error_message;
};

/**
 * Resumen de la reconstruccion de estado al arrancar (ADR-005).
 */
struct RecoveryReport {
    std::size_t requeued{0};            // QUEUED reinsertados en la cola
    std::size_t interrupted{0};         // RUNNING marcados FAILED
    std::size_t orphans_terminated{0};  // procesos de la instancia anterior terminados
};
// Orchestrates job submission and lifecycle, including the concurrency limit and the FIFO queue (ADR-003). This is the only class that knows JobRunner's business rules (validation, duplicate detection, cancellation) — it depends on IJobStore and IProcessLauncher as abstractions (DIP), so either can be swapped without touching this class (OCP), and it never touches JSON or sockets directly (SRP).
/**
 * Orquesta envio, cola, ejecucion y cancelacion de jobs.
 */
class JobManager {
   public:
     /**
      * Construye el administrador de jobs.
      * @param store Backend de persistencia.
      * @param launcher Lanzador de procesos.
      * @param logger Bitacora operacional.
      * @param max_concurrent_jobs Limite de jobs ejecutandose a la vez (RF-05).
      * @param max_queue_size Capacidad maxima de la cola antes de rechazar (RF-25).
      */
    JobManager(IJobStore& store, IProcessLauncher& launcher, Logger& logger,
               std::size_t max_concurrent_jobs = 3, std::size_t max_queue_size = 10,
               std::chrono::milliseconds cancel_timeout = std::chrono::seconds{5});
    // `client_origin` identifies who sent the request (e.g. "pid:1234" for a Unix-socket peer) and scopes the duplicate-request window (RF-27) so two different clients submitting the same command are never merged into one job.
     /**
      * Registra una solicitud de ejecucion. Si no hay cupo, encola el job;
      * si la cola esta llena, rechaza la solicitud sin crear el job (RF-25).
      * @param command Comando a ejecutar.
      * @param args Argumentos del comando.
      * @param client_origin Identidad del cliente solicitante.
      * @return Resultado de aceptacion/rechazo/deduplicacion.
      */
    SubmitResult submit(const std::string& command, const std::vector<std::string>& args,
                        const std::string& client_origin);

     /**
      * Consulta un job por su id.
      * @param id Identificador del job.
      * @return Job encontrado o std::nullopt.
      */
    std::optional<Job> status(JobId id) const;

     /**
      * Lista jobs con filtro opcional por estado.
      * @param state_filter Estado opcional de filtrado.
      * @return Lista de jobs.
      */
    std::vector<Job> list(std::optional<JobState> state_filter) const;

     /**
      * Solicita cancelacion de un job. Si esta en QUEUED, lo retira de la
      * cola sin que llegue a ocupar cupo.
      * @param id Identificador del job.
      * @return true si el job existe; false si no existe.
      */
    bool cancel(JobId id);

     /**
      * Reconstruye el estado a partir del historial persistido (ADR-005):
      * los RUNNING pasan a FAILED (terminando antes lo que quede vivo de su
      * proceso), los QUEUED vuelven a la cola en orden de llegada y se
      * despachan segun el cupo de esta instancia. Llamar una sola vez, antes
      * de aceptar conexiones.
      * @return Conteo de jobs reencolados, interrumpidos y procesos terminados.
      */
    RecoveryReport recover();

     /**
      * Inicia el cierre controlado (RF-15): rechaza solicitudes nuevas, deja
      * de despachar la cola y cancela los jobs RUNNING enviandoles SIGTERM.
      * Los QUEUED se conservan para reencolarse en el siguiente arranque.
      */
    void begin_shutdown();

     /**
      * Envia SIGKILL a los procesos que sigan vivos (fin del periodo de gracia).
      */
    void kill_live_processes();

     /**
      * Indica si queda algun proceso hijo sin terminar.
      * @return true mientras haya procesos lanzados que no se han recolectado.
      */
    bool has_live_processes() const;

   private:
    struct DedupKey {
        std::string command;
        std::vector<std::string> args;
        std::string client_origin;
        bool operator==(const DedupKey&) const = default;
    };
    struct DedupKeyHash {
        /**
         * Calcula hash de una llave de deduplicacion.
         * @param k Llave compuesta por comando, args y origen.
         * @return Valor hash combinado.
         */
        std::size_t operator()(const DedupKey& k) const;
    };

    /**
     * Lanza efectivamente un job en estado Queued y ocupa un cupo.
     * @param id Identificador del job.
     */
    void dispatch(JobId id);

    /**
     * Marca FAILED un job cuyo proceso no se pudo crear y lo retira de la
     * ventana de deduplicacion.
     * @param id Identificador del job.
     * @param reason Causa del fallo.
     */
    void on_launch_failed(JobId id, const std::string& reason);

    /**
     * Si hay cupo libre y jobs en cola, despacha el mas antiguo (FIFO).
     */
    void dispatch_next_queued();

    /**
     * Acumula salida del proceso hijo en el job.
     * @param id Identificador del job.
     * @param is_stderr true si corresponde a stderr.
     * @param data Bloque de datos recibido.
     */
    void on_child_output(JobId id, bool is_stderr, std::string_view data);

    /**
     * Marca salida final de un proceso hijo, libera su cupo y despacha
     * el siguiente job en cola, si existe.
     * @param id Identificador del job.
     * @param exit_code Codigo de salida.
     * @param term_signal Senal de terminacion (0 si no aplica).
     */
    void on_child_exit(JobId id, int exit_code, int term_signal);

    /**
     * Elimina entradas vencidas de la ventana de deduplicacion.
     */
    void prune_dedup_window();

    IJobStore& store_;
    IProcessLauncher& launcher_;
    Logger& logger_;

    std::unordered_map<DedupKey, std::pair<JobId, std::chrono::steady_clock::time_point>,
                       DedupKeyHash>
        recent_submissions_;

    static constexpr std::chrono::milliseconds kDedupWindow{3000};

    const std::size_t max_concurrent_jobs_;
    const std::size_t max_queue_size_;
    const std::chrono::milliseconds cancel_timeout_;
    // Procesos realmente vivos (job -> pid); su tamano es el cupo ocupado.
    // Un job CANCELED sigue aqui hasta que su proceso termina (ADR-003).
    std::unordered_map<JobId, pid_t> live_processes_;
    std::deque<JobId> queue_;
    bool shutting_down_{false};
    // Temporizadores de escalamiento SIGTERM->SIGKILL armados por cancel()
    // sobre un job RUNNING (ADR-006). Se cancelan en on_child_exit() si el
    // proceso termina antes del timeout.
    std::unordered_map<JobId, TimerId> escalation_timers_;
};

}  // namespace jobrunner
