#pragma once

#include <csignal>
#include <functional>
#include <map>
#include <stdexcept>
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
        if (fail_next_launches > 0) {
            --fail_next_launches;
            throw std::runtime_error("fork: Resource temporarily unavailable");
        }
        pid_t pid = next_pid_++;
        launches.push_back({command, args, pid, std::move(on_output), std::move(on_exit)});
        return pid;
    }

    void send_signal(pid_t pid, int signal) override { signals.emplace_back(pid, signal); }

    // Huella determinista: el "starttime" es el propio pid.
    std::optional<ProcessFingerprint> fingerprint(pid_t pid) const override {
        return ProcessFingerprint{"fake-boot", static_cast<std::uint64_t>(pid)};
    }

    std::size_t terminate_orphan_group(pid_t pgid, const ProcessFingerprint& leader) override {
        orphan_terminations.emplace_back(pgid, leader);
        return orphan_group_size;
    }

    struct PendingTimer {
        TimerId id{};
        std::function<void()> on_fire;
        bool cancelled{false};
    };

    TimerId schedule_once(std::chrono::milliseconds /*delay*/,
                          std::function<void()> on_fire) override {
        TimerId id = next_timer_id_++;
        timers.push_back({id, std::move(on_fire), false});
        return id;
    }

    void cancel_timer(TimerId id) override {
        for (auto& t : timers) {
            if (t.id == id) t.cancelled = true;
        }
    }

    // Ayuda para la prueba: simula que el timeout expiro. Devuelve false si
    // el temporizador no existe o ya se habia cancelado.
    bool fire_timer(TimerId id) {
        for (auto& t : timers) {
            if (t.id == id && !t.cancelled) {
                t.on_fire();
                return true;
            }
        }
        return false;
    }

    std::vector<PendingTimer> timers;

    // Cantidad de lanzamientos siguientes que fallan como fork/pipe.
    int fail_next_launches{0};

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
    std::vector<std::pair<pid_t, ProcessFingerprint>> orphan_terminations;
    std::size_t orphan_group_size{0};  // lo que devuelve terminate_orphan_group

   private:
    pid_t next_pid_{1000};
    TimerId next_timer_id_{1};
};

}  // namespace jobrunner::testing
