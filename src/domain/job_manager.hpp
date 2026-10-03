#pragma once

#include <chrono>
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
 * Orquesta envio, ejecucion y cancelacion de jobs.
 */
class JobManager {
   public:
     /**
      * Construye el administrador de jobs.
      * @param store Backend de persistencia.
      * @param launcher Lanzador de procesos.
      * @param logger Bitacora operacional.
      */
    JobManager(IJobStore& store, IProcessLauncher& launcher, Logger& logger);

     /**
      * Registra una solicitud de ejecucion.
      * @param command Comando a ejecutar.
      * @param args Argumentos del comando.
      * @param client_origin Identidad del cliente solicitante.
      * @return Resultado de aceptacion/deduplicacion.
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
      * Solicita cancelacion de un job.
      * @param id Identificador del job.
      * @return true si el job existe; false si no existe.
      */
    bool cancel(JobId id);

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
     * Intenta iniciar un job en estado Queued.
     * @param id Identificador del job.
     */
    void dispatch(JobId id);

    /**
     * Acumula salida del proceso hijo en el job.
     * @param id Identificador del job.
     * @param is_stderr true si corresponde a stderr.
     * @param data Bloque de datos recibido.
     */
    void on_child_output(JobId id, bool is_stderr, std::string_view data);

    /**
     * Marca salida final de un proceso hijo.
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
};

}  // namespace jobrunner