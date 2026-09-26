#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "./job.hpp"

namespace jobrunner {

// Abstraction over how job metadata is persisted. JobManager depends only
// on this interface (DIP), so the concrete backend can change — in-memory
// today, SQLite from Hito 2 — without touching orchestration logic (OCP).
class IJobStore {
   public:
    virtual ~IJobStore() = default;

    virtual JobId create(const std::string& command,
                          const std::vector<std::string>& args) = 0;

    virtual std::optional<Job> get(JobId id) const = 0;

    // Applies `mutator` to the stored job and persists the result.
    // Returns false if no job with that id exists.
    virtual bool update(JobId id, const std::function<void(Job&)>& mutator) = 0;

    virtual std::vector<Job> list(std::optional<JobState> state_filter) const = 0;
};

}  // namespace jobrunner