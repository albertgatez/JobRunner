#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "./job.hpp"

namespace jobrunner {

// Abstraction over how job metadata is persisted. JobManager depends only on this interface (DIP), so the concrete backend can change — in-memory today, SQLite from Hito 2 — without touching orchestration logic (OCP).

/**
 * Interfaz de persistencia para metadatos de jobs.
 */
class IJobStore {
   public:
     /**
      * Destructor virtual para uso polimorfico.
      */
    virtual ~IJobStore() = default;

     /**
      * Crea un job nuevo y devuelve su identificador.
      * @param command Ejecutable/comando a lanzar.
      * @param args Argumentos del comando.
      * @return Id unico del job creado.
      */
    virtual JobId create(const std::string& command,
                          const std::vector<std::string>& args) = 0;

     /**
      * Obtiene un job por id.
      * @param id Identificador del job.
      * @return Job encontrado o std::nullopt.
      */
    virtual std::optional<Job> get(JobId id) const = 0;

     /**
      * Actualiza un job existente aplicando una funcion mutadora.
      * @param id Identificador del job.
      * @param mutator Funcion que modifica el job almacenado.
      * @return true si el job existe y se actualiza; false si no existe.
      */
    virtual bool update(JobId id, const std::function<void(Job&)>& mutator) = 0;

     /**
      * Lista jobs con filtro opcional por estado.
      * @param state_filter Estado opcional para filtrar resultados.
      * @return Coleccion de jobs.
      */
    virtual std::vector<Job> list(std::optional<JobState> state_filter) const = 0;
};

}  // namespace jobrunner
