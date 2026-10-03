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

/**
 * Convierte un estado de job a su representacion textual.
 * @param state Estado del job.
 * @return Estado serializado para salida/protocolo.
 */
const char* to_string(JobState state);

/**
 * Valida si una transicion de estado es legal.
 * @param from Estado de origen.
 * @param to Estado de destino.
 * @return true si la transicion es valida; false en caso contrario.
 */
bool is_valid_transition(JobState from, JobState to);

/**
 * Modelo de datos de un job.
 */
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

    /**
     * Cambia el estado del job validando la transicion.
     * @param to Estado objetivo.
     */
    void transition_to(JobState to);
};

}  // namespace jobrunner