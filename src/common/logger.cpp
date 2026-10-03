#include <chrono>
#include <ctime>

#include "./logger.hpp"

namespace jobrunner {

// Inicializa salida a archivo o stderr.
// path: ruta de log; vacio para stderr.
Logger::Logger(std::string path) {
    if (path.empty()) {
        file_ = stderr;
        owns_file_ = false;
    } else {
        file_ = std::fopen(path.c_str(), "a");
        if (!file_) {
            file_ = stderr;
            owns_file_ = false;
        } else {
            owns_file_ = true;
        }
    }
}

// Cierra archivo si el logger es propietario.
Logger::~Logger() {
    if (owns_file_ && file_) std::fclose(file_);
}

// Escribe una linea de log con nivel y job opcional.
// level: severidad del evento.
// message: texto del evento.
// job_id: id opcional para correlacion.
void Logger::write(const char* level, const std::string& message,
                    std::optional<std::uint64_t> job_id) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));

    if (job_id) {
        std::fprintf(file_, "%s [%s] job=%llu %s\n", timebuf, level,
                      static_cast<unsigned long long>(*job_id), message.c_str());
    } else {
        std::fprintf(file_, "%s [%s] %s\n", timebuf, level, message.c_str());
    }
    std::fflush(file_);
}

// Registra evento informativo.
// message: descripcion del evento.
// job_id: id opcional del job.
void Logger::info(const std::string& message, std::optional<std::uint64_t> job_id) {
    write("INFO", message, job_id);
}

// Registra evento de error.
// message: descripcion del error.
// job_id: id opcional del job.
void Logger::error(const std::string& message, std::optional<std::uint64_t> job_id) {
    write("ERROR", message, job_id);
}

}  // namespace jobrunner