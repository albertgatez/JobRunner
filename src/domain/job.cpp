#include <stdexcept>

#include "./job.hpp"

namespace jobrunner {

// Convierte estado enum a texto de protocolo.
// state: estado interno del job.
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

// Convierte texto de protocolo a estado enum.
// text: estado serializado.
std::optional<JobState> parse_job_state(std::string_view text) {
    if (text == "QUEUED") return JobState::Queued;
    if (text == "RUNNING") return JobState::Running;
    if (text == "SUCCEEDED") return JobState::Succeeded;
    if (text == "FAILED") return JobState::Failed;
    if (text == "CANCELED") return JobState::Canceled;
    return std::nullopt;
}

// Valida si una transicion de estado es legal.
// from: estado actual.
// to: estado objetivo.
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

// Aplica una transicion validada al job.
// to: siguiente estado requerido.
void Job::transition_to(JobState to) {
    if (!is_valid_transition(state, to)) {
        throw std::logic_error(std::string("invalid job state transition: ") +
                                to_string(state) + " -> " + to_string(to));
    }
    state = to;
}

}  // namespace jobrunner