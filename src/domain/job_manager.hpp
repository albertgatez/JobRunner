#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/logger.hpp"
#include "./job.hpp"
#include "./job_store.hpp"
#include "../process/process_launcher.hpp"

namespace jobrunner {

struct SubmitResult {
    bool accepted{false};
    JobId job_id{};
    bool was_duplicate{false};
    std::string error_message;
};

// Orchestrates job submission and lifecycle. This is the only class that
// knows JobRunner's business rules (validation, duplicate detection,
// cancellation) — it depends on IJobStore and IProcessLauncher as
// abstractions (DIP), so either can be swapped without touching this class
// (OCP), and it never touches JSON or sockets directly (SRP).
class JobManager {
   public:
    JobManager(IJobStore& store, IProcessLauncher& launcher, Logger& logger);

    // `client_origin` identifies who sent the request (e.g. "pid:1234" for
    // a Unix-socket peer) and scopes the duplicate-request window (RF-27)
    // so two different clients submitting the same command are never
    // merged into one job.
    SubmitResult submit(const std::string& command, const std::vector<std::string>& args,
                        const std::string& client_origin);

    std::optional<Job> status(JobId id) const;
    std::vector<Job> list(std::optional<JobState> state_filter) const;

    // Returns false only if the job id is unknown. A job already in a
    // terminal state is a no-op success — cancellation is idempotent
    // (RF-10, RF-26).
    bool cancel(JobId id);

   private:
    struct DedupKey {
        std::string command;
        std::vector<std::string> args;
        std::string client_origin;
        bool operator==(const DedupKey&) const = default;
    };
    struct DedupKeyHash {
        std::size_t operator()(const DedupKey& k) const;
    };

    void dispatch(JobId id);
    void on_child_output(JobId id, bool is_stderr, std::string_view data);
    void on_child_exit(JobId id, int exit_code, int term_signal);
    void prune_dedup_window();

    IJobStore& store_;
    IProcessLauncher& launcher_;
    Logger& logger_;

    std::unordered_map<DedupKey, std::pair<JobId, std::chrono::steady_clock::time_point>,
                       DedupKeyHash>
        recent_submissions_;

    static constexpr std::chrono::milliseconds kDedupWindow{3000};
};

}  // namespace jobrunner