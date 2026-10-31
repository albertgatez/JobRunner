#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
 * Convierte la representacion textual de un estado a su enum.
 * @param text Estado serializado ("QUEUED", "RUNNING", ...).
 * @return Estado correspondiente o std::nullopt si no es valido.
 */
std::optional<JobState> parse_job_state(std::string_view text);

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

    // Huella del proceso (ADR-005): el kernel puede reciclar un PID, pero no
    // la pareja (boot_id, starttime de /proc/<pid>/stat). Permite reconocer,
    // tras una caida del servidor, si un proceso vivo es realmente el de este job.
    std::optional<std::uint64_t> pid_start_time;
    std::string boot_id;

    std::string stdout_data;
    std::string stderr_data;

    // Explicacion del servidor cuando el desenlace no lo decidio el proceso
    // (p. ej. interrumpido por reinicio o cancelado por cierre del servicio).
    std::string diagnostic;

    /**
     * Cambia el estado del job validando la transicion.
     * @param to Estado objetivo.
     */
    void transition_to(JobState to);
};

}  // namespace jobrunner