#include "./reactor.hpp"

#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace jobrunner {

namespace {
// Lanza excepcion con el errno actual.
// what: texto base del error.
void throw_errno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
}  // namespace

// Inicializa epoll, bloquea senales y configura signalfd.
Reactor::Reactor() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) throw_errno("epoll_create1");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) throw_errno("sigprocmask");

    signal_fd_ = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signal_fd_ < 0) throw_errno("signalfd");

    add_read(signal_fd_, [this] { handle_signalfd_readable(); });
}

// Cierra descriptores del reactor.
Reactor::~Reactor() {
    if (signal_fd_ >= 0) ::close(signal_fd_);
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

// Registra un descriptor para lectura.
// fd: descriptor no bloqueante.
// on_readable: callback al haber datos/evento.
void Reactor::add_read(int fd, Callback on_readable) {
    FdState state;
    state.on_readable = std::move(on_readable);
    fds_[fd] = std::move(state);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0) throw_errno("epoll_ctl ADD");
}

// Activa/desactiva interes por escritura para un fd.
// fd: descriptor ya registrado.
// interested: true para escuchar EPOLLOUT.
// on_writable: callback cuando sea escribible.
void Reactor::set_write_interest(int fd, bool interested, Callback on_writable) {
    auto it = fds_.find(fd);
    if (it == fds_.end()) return; // fd may have just been removed by another callback
    it->second.on_writable = std::move(on_writable);

    std::uint32_t events = EPOLLIN;
    if (interested) events |= EPOLLOUT;

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) != 0) throw_errno("epoll_ctl MOD");
}

// Elimina un descriptor del reactor.
// fd: descriptor a quitar.
void Reactor::remove(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    fds_.erase(fd);
}

// Indica si un fd sigue registrado.
// fd: descriptor a consultar.
bool Reactor::is_registered(int fd) const { return fds_.find(fd) != fds_.end(); }

// Registra callback para una senal POSIX.
// signo: numero de senal.
// handler: callback asociado.
void Reactor::on_signal(int signo, SignalCallback handler) {
    signal_handlers_[signo] = std::move(handler);
}

// Lee eventos de signalfd y despacha handlers.
void Reactor::handle_signalfd_readable() {
    struct signalfd_siginfo info {};
    while (true) {
        ssize_t n = ::read(signal_fd_, &info, sizeof(info));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            throw_errno("read signalfd");
        }
        if (n != sizeof(info)) break;

        auto it = signal_handlers_.find(static_cast<int>(info.ssi_signo));
        if (it != signal_handlers_.end()) {
            // Copy out before invoking: a handler is free to register a different handler for the same signal.
            SignalCallback handler_copy = it->second;
            if (handler_copy) handler_copy(info);
        }
    }
}

// Solicita detener el loop principal.
void Reactor::stop() { running_ = false; }

// Ejecuta el loop principal de epoll.
void Reactor::run() {
    running_ = true;
    std::array<epoll_event, 64> events{};

    while (running_) {
        int n = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw_errno("epoll_wait");
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            auto it = fds_.find(fd);
            if (it == fds_.end()) continue; // removed by an earlier callback this

            bool hup_or_err = events[i].events & (EPOLLHUP | EPOLLERR);
            bool writable = events[i].events & EPOLLOUT;
            bool readable = events[i].events & EPOLLIN;

            // Copy the callbacks out before invoking them: a callback may remove `fd` (e.g. a connection closing itself), which erases this very map entry. Invoking a *copy* means that erase can't pull the rug out from under the call we're currently making.
            Callback readable_cb = it->second.on_readable;
            Callback writable_cb = it->second.on_writable;

            if (hup_or_err) {
                if (readable_cb) readable_cb();
                continue;
            }
            if (writable && writable_cb) writable_cb();
            if (readable && is_registered(fd) && readable_cb) readable_cb(); // Re-check registration: the writable callback above may already have torn this fd down (e.g. a write error closed the connection), in which case calling the copied readable callback would operate on a now-destroyed object.
        }
    }
}

}  // namespace jobrunner
