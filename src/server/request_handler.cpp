#include "./request_handler.hpp"

#include <nlohmann/json.hpp>

namespace jobrunner {

using json = nlohmann::json;

namespace {

json job_to_json(const Job& job) {
    json j;
    j["id"] = job.id;
    j["command"] = job.command;
    j["args"] = job.args;
    j["state"] = to_string(job.state);
    if (job.exit_code) j["exit_code"] = *job.exit_code;
    if (job.exit_signal) j["exit_signal"] = *job.exit_signal;
    j["stdout"] = job.stdout_data;
    j["stderr"] = job.stderr_data;
    return j;
}

std::string error_response(const std::string& message) {
    json j;
    j["ok"] = false;
    j["error"] = message;
    return j.dump();
}

std::optional<JobState> parse_state(const std::string& s) {
    if (s == "QUEUED") return JobState::Queued;
    if (s == "RUNNING") return JobState::Running;
    if (s == "SUCCEEDED") return JobState::Succeeded;
    if (s == "FAILED") return JobState::Failed;
    if (s == "CANCELED") return JobState::Canceled;
    return std::nullopt;
}

}  // namespace

RequestHandler::RequestHandler(JobManager& manager) : manager_(manager) {}

std::string RequestHandler::handle(const std::string& request_json,
                                    const std::string& client_origin) {
    json request;
    try {
        request = json::parse(request_json);
    } catch (const std::exception& e) {
        return error_response(std::string("json invalido: ") + e.what());
    }

    try {
        if (!request.contains("op") || !request["op"].is_string()) {
            return error_response("falta el campo 'op'");
        }
        std::string op = request["op"];

        if (op == "submit") {
            if (!request.contains("command") || !request["command"].is_string()) {
                return error_response("falta 'command'");
            }
            std::string command = request["command"];
            std::vector<std::string> args;
            if (request.contains("args")) {
                for (const auto& a : request["args"]) args.push_back(a.get<std::string>());
            }
            auto result = manager_.submit(command, args, client_origin);
            json resp;
            resp["ok"] = result.accepted;
            if (!result.accepted) {
                resp["error"] = result.error_message;
            } else {
                resp["job_id"] = result.job_id;
                resp["was_duplicate"] = result.was_duplicate;
            }
            return resp.dump();
        }

        if (op == "status") {
            if (!request.contains("id")) return error_response("falta 'id'");
            JobId id = request["id"].get<JobId>();
            auto job = manager_.status(id);
            json resp;
            resp["ok"] = job.has_value();
            if (job) {
                resp["job"] = job_to_json(*job);
            } else {
                resp["error"] = "job desconocido";
            }
            return resp.dump();
        }

        if (op == "list") {
            std::optional<JobState> filter;
            if (request.contains("state") && request["state"].is_string()) {
                filter = parse_state(request["state"].get<std::string>());
                if (!filter) {
                    return error_response("estado desconocido: " +
                                           request["state"].get<std::string>());
                }
            }
            auto jobs = manager_.list(filter);
            json resp;
            resp["ok"] = true;
            json arr = json::array();
            for (const auto& j : jobs) arr.push_back(job_to_json(j));
            resp["jobs"] = arr;
            return resp.dump();
        }

        if (op == "cancel") {
            if (!request.contains("id")) return error_response("falta 'id'");
            JobId id = request["id"].get<JobId>();
            bool found = manager_.cancel(id);
            json resp;
            resp["ok"] = found;
            if (!found) resp["error"] = "job desconocido";
            return resp.dump();
        }

        return error_response("operacion desconocida: " + op);
    } catch (const std::exception& e) {
        return error_response(std::string("solicitud invalida: ") + e.what());
    }
}

}  // namespace jobrunner