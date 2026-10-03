#include "./in_memory_job_store.hpp"

namespace jobrunner {

// Crea y almacena un job nuevo en memoria.
// command: comando a ejecutar.
// args: argumentos del comando.
JobId InMemoryJobStore::create(const std::string& command,
                                const std::vector<std::string>& args) {
    JobId id = next_id_++;
    Job job;
    job.id = id;
    job.command = command;
    job.args = args;
    job.state = JobState::Queued;
    job.received_at = Clock::now();
    jobs_.emplace(id, std::move(job));
    return id;
}

// Busca un job por id.
// id: identificador del job.
std::optional<Job> InMemoryJobStore::get(JobId id) const {
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return std::nullopt;
    return it->second;
}

// Actualiza un job existente con una funcion mutadora.
// id: identificador del job.
// mutator: logica de modificacion.
bool InMemoryJobStore::update(JobId id, const std::function<void(Job&)>& mutator) {
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    mutator(it->second);
    return true;
}

// Lista jobs con filtro opcional por estado.
// state_filter: estado opcional para filtrar.
std::vector<Job> InMemoryJobStore::list(std::optional<JobState> state_filter) const {
    std::vector<Job> result;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        if (!state_filter || job.state == *state_filter) {
            result.push_back(job);
        }
    }
    return result;
}

}  // namespace jobrunner