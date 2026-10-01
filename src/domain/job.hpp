#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace jobrunner {

using JobId = std::uint64_t;
using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

enum class JobState {
    Queued,
    Running,
    Succeeded,
    Failed,
    Canceled,
};

const char* to_string(JobState state);

// Returns true if transitioning from `from` to `to` is a legal move in
// JobRunner's lifecycle (RF-06). Terminal states never move again, so
// state regressions are structurally impossible (RNF-27).
bool is_valid_transition(JobState from, JobState to);

struct Job {
    JobId id{};
    std::string command;
    std::vector<std::string> args;

    JobState state{JobState::Queued};

    TimePoint received_at{};
    std::optional<TimePoint> started_at;
    std::optional<TimePoint> finished_at;

    std::optional<int> exit_code;    // set when the process exits normally
    std::optional<int> exit_signal;  // set when a signal terminated it

    std::optional<pid_t> pid;  // set while Running

    std::string stdout_data;
    std::string stderr_data;

    // Throws std::logic_error if `to` is not reachable from the current
    // state. Callers that need a non-throwing check should call
    // is_valid_transition() first.
    void transition_to(JobState to);
};

}  // namespace jobrunner