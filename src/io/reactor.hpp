#pragma once

#include <sys/signalfd.h>

#include <functional>
#include <unordered_map>

namespace jobrunner {

// Single-threaded event reactor around epoll(7). POSIX signals we care
// about (SIGCHLD, SIGINT, SIGTERM) are delivered through signalfd(2) as
// ordinary readable events, so process supervision and network I/O share
// one control flow and one thread — no locks are needed anywhere on job
// state. See "docs/decisions/ADR-003 - Concurrencia.md" for the
// alternatives considered and why this one was chosen.
class Reactor {
   public:
    using Callback = std::function<void()>;
    using SignalCallback = std::function<void(const struct signalfd_siginfo&)>;

    Reactor();
    ~Reactor();

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    // Registers `fd` for readability. `fd` must already be non-blocking.
    // `on_readable` is invoked from run() whenever data (or EOF/HUP/ERR) is
    // available.
    void add_read(int fd, Callback on_readable);

    // Enables/disables write-readiness notifications for an already
    // registered fd. Pass interested=false with on_writable=nullptr to stop
    // watching for writability once an outgoing buffer has drained.
    void set_write_interest(int fd, bool interested, Callback on_writable);

    // Unregisters `fd`. Safe to call from within that fd's own callback —
    // run() copies callbacks out before invoking them and re-checks
    // registration between an fd's write and read callbacks, so a callback
    // that removes itself never leaves a dangling call on the stack.
    void remove(int fd);

    bool is_registered(int fd) const;

    // Runs the event loop until stop() is called from within a callback.
    void run();
    void stop();

    // `handler` is invoked (from the loop, not from a signal handler) the
    // next time `signo` arrives while blocked. SIGCHLD/SIGINT/SIGTERM are
    // already blocked and routed through signalfd by the constructor.
    void on_signal(int signo, SignalCallback handler);

   private:
    struct FdState {
        Callback on_readable;
        Callback on_writable;
    };

    void handle_signalfd_readable();

    int epoll_fd_{-1};
    int signal_fd_{-1};
    bool running_{false};
    std::unordered_map<int, FdState> fds_;
    std::unordered_map<int, SignalCallback> signal_handlers_;
};

}  // namespace jobrunner