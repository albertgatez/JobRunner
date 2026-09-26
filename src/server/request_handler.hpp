#pragma once

#include <string>

#include "../domain/job_manager.hpp"

namespace jobrunner {

// Translates JSON request/response payloads to and from JobManager calls.
// This is the only place that knows the wire schema — JobManager itself
// never sees JSON (see docs/technical-guide/arquitectura.md).
class RequestHandler {
   public:
    explicit RequestHandler(JobManager& manager);

    // Never throws: malformed input is reported back as a JSON error
    // response rather than propagated (RF-02, RNF-08).
    std::string handle(const std::string& request_json, const std::string& client_origin);

   private:
    JobManager& manager_;
};

}  // namespace jobrunner