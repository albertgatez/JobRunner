#pragma once

#include <map>

#include "./job_store.hpp"

namespace jobrunner {

/**
 * Implementacion en memoria de IJobStore.
 */
class InMemoryJobStore : public IJobStore {
   public:
     /**
      * Crea y guarda un job en memoria.
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
      * Aplica una mutacion a un job almacenado.
      * @param id Identificador del job.
      * @param mutator Funcion de actualizacion.
      * @return true si el job existe y se actualiza; false si no existe.
      */
    bool update(JobId id, const std::function<void(Job&)>& mutator) override;

     /**
      * Lista jobs con filtro opcional por estado.
      * @param state_filter Estado opcional de filtrado.
      * @return Lista de jobs.
      */
    std::vector<Job> list(std::optional<JobState> state_filter) const override;

   private:
    std::map<JobId, Job> jobs_;
    JobId next_id_{1};
};

}  // namespace jobrunner