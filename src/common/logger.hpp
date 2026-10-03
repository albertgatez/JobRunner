<<<<<<< Updated upstream
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace jobrunner {

/**
 * Logger operacional del servicio.
 *
 * Registra eventos y errores, con un id de job opcional para correlacion.
 */
class Logger {
   public:
     /**
      * Construye el logger.
      * @param path Ruta del archivo destino; vacio para usar stderr.
      */
    explicit Logger(std::string path = "");

     /**
      * Libera recursos asociados al sink de salida.
      */
    ~Logger();

     /**
      * Constructor copia deshabilitado para evitar duplicar ownership.
      */
    Logger(const Logger&) = delete;

     /**
      * Asignacion por copia deshabilitada para mantener ownership unico.
      */
    Logger& operator=(const Logger&) = delete;

     /**
      * Registra un evento informativo.
      * @param message Mensaje del evento.
      * @param job_id Id de job opcional para correlacion.
      */
    void info(const std::string& message, std::optional<std::uint64_t> job_id = std::nullopt);

     /**
      * Registra un evento de error.
      * @param message Mensaje de error.
      * @param job_id Id de job opcional para correlacion.
      */
    void error(const std::string& message, std::optional<std::uint64_t> job_id = std::nullopt);

   private:
     /**
      * Escribe una linea en el sink configurado.
      * @param level Nivel de severidad (INFO, ERROR).
      * @param message Mensaje a persistir.
      * @param job_id Id de job opcional para trazabilidad.
      */
    void write(const char* level, const std::string& message,
               std::optional<std::uint64_t> job_id);

    std::FILE* file_{nullptr};
    bool owns_file_{false};
};

=======
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace jobrunner {

// Minimal single-threaded logger (RF-14, RNF-22). No locking is needed:
// JobRunner's whole service runs on one reactor thread, so calls into this
// class are never concurrent (see "docs/decisions/ADR-003 - Concurrencia.md").
// Never logs raw job stdout/stderr content, only operational events with an
// optional job id for correlation (RNF-15).
class Logger {
   public:
    explicit Logger(std::string path = "");
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void info(const std::string& message, std::optional<std::uint64_t> job_id = std::nullopt);
    void error(const std::string& message, std::optional<std::uint64_t> job_id = std::nullopt);

   private:
    void write(const char* level, const std::string& message,
               std::optional<std::uint64_t> job_id);

    std::FILE* file_{nullptr};
    bool owns_file_{false};
};

>>>>>>> Stashed changes
}  // namespace jobrunner