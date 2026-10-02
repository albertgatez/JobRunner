#pragma once

#include <map>

#include "./job_store.hpp"

namespace jobrunner {

// Hito 1 persistence backend: keeps everything in a std::map. Safe without
// locks because the whole service runs on a single reactor thread (see
// "docs/decisions/ADR-003 - Concurrencia.md"). Swappable for
// SqliteJobStore in Hito 2 without changing IJobStore's callers.
class InMemoryJobStore : public IJobStore {
   public:
    JobId create(const std::string& command,
                 const std::vector<std::string>& args) override;
    std::optional<Job> get(JobId id) const override;
    bool update(JobId id, const std::function<void(Job&)>& mutator) override;
    std::vector<Job> list(std::optional<JobState> state_filter) const override;

   private:
    std::map<JobId, Job> jobs_;
    JobId next_id_{1};
};

}  // namespace jobrunner