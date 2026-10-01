#pragma once

#include <unordered_map>

#include "../io/reactor.hpp"
#include "./process_launcher.hpp"

namespace jobrunner {

// Real IProcessLauncher: forks a child per job, execs the requested
// command, and captures stdout/stderr via non-blocking pipes registered
// with the Reactor. Children are reaped from a SIGCHLD handler wired
// through the same reactor (RF-04, RF-29).
class PosixProcessLauncher : public IProcessLauncher {
   public:
    explicit PosixProcessLauncher(Reactor& reactor);

    pid_t launch(const std::string& command, const std::vector<std::string>& args,
                 OutputCallback on_output, ExitCallback on_exit) override;

    void send_signal(pid_t pid, int signal) override;

   private:
    struct ChildInfo {
        int stdout_fd{-1};
        int stderr_fd{-1};
        OutputCallback on_output;
        ExitCallback on_exit;
    };

    void reap_exited_children();
    void drain_pipe(pid_t pid, int fd, bool is_stderr);

    Reactor& reactor_;
    std::unordered_map<pid_t, ChildInfo> children_;
};

}  // namespace jobrunner