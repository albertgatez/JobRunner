#include <chrono>
#include <ctime>

#include "./logger.hpp"

namespace jobrunner {

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

Logger::~Logger() {
    if (owns_file_ && file_) std::fclose(file_);
}

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

void Logger::info(const std::string& message, std::optional<std::uint64_t> job_id) {
    write("INFO", message, job_id);
}

void Logger::error(const std::string& message, std::optional<std::uint64_t> job_id) {
    write("ERROR", message, job_id);
}

}  // namespace jobrunner