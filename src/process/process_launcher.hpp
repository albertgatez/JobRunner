#pragma once

#include <sys/types.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace jobrunner {

// Invoked exactly once when the child process exits, from within the
// reactor's event loop (never from a signal handler directly).
using ExitCallback = std::function<void(pid_t pid, int exit_code, int term_signal)>;

// Invoked whenever new bytes are available on the child's stdout/stderr.
using OutputCallback = std::function<void(pid_t pid, bool is_stderr, std::string_view data)>;

// Abstraction over how a job is turned into an OS process. JobManager
// depends on this interface (DIP), so tests can substitute a fake that
// never actually forks (see tests/fakes/fake_process_launcher.hpp).
class IProcessLauncher {
   public:
    virtual ~IProcessLauncher() = default;

    // Forks and execs `command` with `args`. stdout/stderr are captured via
    // pipes and delivered through `on_output` without being mixed together
    // (RF-11); `on_exit` fires exactly once when the process terminates.
    // Returns the child pid.
    virtual pid_t launch(const std::string& command, const std::vector<std::string>& args,
                          OutputCallback on_output, ExitCallback on_exit) = 0;

    // Sends `signal` (e.g. SIGTERM, SIGKILL) to the process group of `pid`.
    virtual void send_signal(pid_t pid, int signal) = 0;
};

}  // namespace jobrunner