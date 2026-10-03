#pragma once

#include <string>

#include "../domain/job_manager.hpp"

namespace jobrunner {

/**
 * Traduce solicitudes/respuestas JSON al dominio.
 */
class RequestHandler {
   public:
     /**
      * Construye el traductor JSON <-> dominio.
      * @param manager Orquestador de jobs del dominio.
      */
    explicit RequestHandler(JobManager& manager);

     /**
      * Procesa una solicitud JSON y devuelve respuesta JSON.
      * @param request_json Payload recibido por red.
      * @param client_origin Identidad del cliente (ej. pid:1234).
      * @return Respuesta JSON serializada.
      */
    std::string handle(const std::string& request_json, const std::string& client_origin);

   private:
    JobManager& manager_;
};

}  // namespace jobrunner