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

}  // namespace jobrunner