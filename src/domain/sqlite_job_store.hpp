#pragma once

#include <cstddef>
#include <string>

#include "./job_store.hpp"

struct sqlite3;

namespace jobrunner {
// Hito 2 persistence backend (ADR-004): one row per job in a SQLite file, WAL journal, one transaction per update(). Like InMemoryJobStore it needs no locks: the whole service runs on a single reactor thread (ADR-003). A lock file keeps a second server instance from opening the same database, since its recovery (ADR-005) would mark this instance's running jobs as interrupted.
/**
 * Implementacion de IJobStore respaldada por SQLite.
 */
class SqliteJobStore : public IJobStore {
   public:
     /**
      * Abre (o crea) la base y aplica el esquema.
      * @param db_path Ruta del archivo .db; su directorio debe existir.
      * @throws std::runtime_error si la base no se puede abrir, tiene un
      *         esquema incompatible u otra instancia ya la esta usando.
      */
    explicit SqliteJobStore(const std::string& db_path);

     /**
      * Cierra la base y libera el candado de instancia.
      */
    ~SqliteJobStore() override;

    SqliteJobStore(const SqliteJobStore&) = delete;
    SqliteJobStore& operator=(const SqliteJobStore&) = delete;

     /**
      * Inserta un job nuevo en estado QUEUED.
      * @param command Comando a ejecutar.
      * @param args Argumentos del comando.
      * @return Id del job creado.
      */
    JobId create(const std::string& command,
                 const std::vector<std::string>& args) override;

     /**
      * Obtiene un job por su id.
      * @param id Identificador del job.
      * @return Job encontrado o std::nullopt.
      */
    std::optional<Job> get(JobId id) const override;

     /**
      * Lee, muta y reescribe la fila dentro de una transaccion. Si el
      * mutador lanza una excepcion, la fila queda como estaba.
      * @param id Identificador del job.
      * @param mutator Funcion de actualizacion.
      * @return true si el job existe y se actualiza; false si no existe.
      */
    bool update(JobId id, const std::function<void(Job&)>& mutator) override;

     /**
      * Lista jobs ordenados por id con filtro opcional por estado.
      * @param state_filter Estado opcional de filtrado.
      * @return Lista de jobs.
      */
    std::vector<Job> list(std::optional<JobState> state_filter) const override;

     /**
      * Borra todos los registros y reinicia la numeracion de ids.
      * @return Cantidad de jobs eliminados.
      */
    std::size_t reset();

   private:
    /**
     * Crea las tablas si la base es nueva y valida la version del esquema.
     */
    void apply_schema();

    /**
     * Libera la conexion y el candado (usado por el destructor y si el
     * constructor falla a medias).
     */
    void close();

    sqlite3* db_{nullptr};
    int lock_fd_{-1};
};

}  // namespace jobrunner
