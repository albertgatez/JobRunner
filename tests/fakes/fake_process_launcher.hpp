#pragma once

#include <csignal>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../src/process/process_launcher.hpp"

namespace jobrunner::testing {

// IProcessLauncher falso: no hace fork ni exec. Registra los lanzamientos y
// deja que la prueba dispare a mano la salida y la terminación del "proceso",
// de modo que JobManager se prueba sin procesos reales ni sleeps.
class FakeProcessLauncher : public IProcessLauncher {
   public:
    struct Launch {
        std::string command;
        std::vector<std::string> args;
        pid_t pid{};
        OutputCallback on_output;
        ExitCallback on_exit;
    };

    pid_t launch(const std::string& command, const std::vector<std::string>& args,
                 OutputCallback on_output, ExitCallback on_exit) override {
        pid_t pid = next_pid_++;
        launches.push_back({command, args, pid, std::move(on_output), std::move(on_exit)});
        return pid;
    }

    void send_signal(pid_t pid, int signal) override { signals.emplace_back(pid, signal); }

    // --- Ayudas para la prueba ---------------------------------------------
    void emit_output(std::size_t index, bool is_stderr, const std::string& data) {
        auto& l = launches.at(index);
        l.on_output(l.pid, is_stderr, data);
    }

    void exit_child(std::size_t index, int exit_code, int term_signal = 0) {
        auto& l = launches.at(index);
        l.on_exit(l.pid, exit_code, term_signal);
    }

    bool signaled(pid_t pid, int signal) const {
        for (const auto& [p, s] : signals) {
            if (p == pid && s == signal) return true;
        }
        return false;
    }

    std::vector<Launch> launches;
    std::vector<std::pair<pid_t, int>> signals;

   private:
    pid_t next_pid_{1000};
};

}  // namespace jobrunner::testing
