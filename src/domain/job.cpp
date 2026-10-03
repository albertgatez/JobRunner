#include <stdexcept>

#include "./job.hpp"

namespace jobrunner {

const char* to_string(JobState state) {
    switch (state) {
        case JobState::Queued:
            return "QUEUED";
        case JobState::Running:
            return "RUNNING";
        case JobState::Succeeded:
            return "SUCCEEDED";
        case JobState::Failed:
            return "FAILED";
        case JobState::Canceled:
            return "CANCELED";
    }
    return "UNKNOWN";
}

bool is_valid_transition(JobState from, JobState to) {
    switch (from) {
        case JobState::Queued:
            return to == JobState::Running || to == JobState::Canceled;
        case JobState::Running:
            return to == JobState::Succeeded || to == JobState::Failed ||
                   to == JobState::Canceled;
        case JobState::Succeeded:
        case JobState::Failed:
        case JobState::Canceled:
            return false;  // terminal: no regressions (RNF-27)
    }
    return false;
}

void Job::transition_to(JobState to) {
    if (!is_valid_transition(state, to)) {
        throw std::logic_error(std::string("invalid job state transition: ") +
                                to_string(state) + " -> " + to_string(to));
    }
    state = to;
}

}  // namespace jobrunner