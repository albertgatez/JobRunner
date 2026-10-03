#include "./unix_socket_listener.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace jobrunner {

namespace {
// Marca un fd como no bloqueante.
// fd: descriptor a configurar.
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
}  // namespace

// Crea socket listener y lo registra en reactor.
// reactor: loop de eventos.
// socket_path: ruta del socket unix.
// on_accept: callback al aceptar cliente.
UnixSocketListener::UnixSocketListener(Reactor& reactor, std::string socket_path,
                                        AcceptCallback on_accept)
    : reactor_(reactor), socket_path_(std::move(socket_path)), on_accept_(std::move(on_accept)) {
    ::unlink(socket_path_.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error(std::string("bind: ") + std::strerror(errno));
    }
    if (::listen(listen_fd_, /*backlog=*/64) != 0) {
        throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
    }
    set_nonblocking(listen_fd_);

    reactor_.add_read(listen_fd_, [this] { on_connection_ready(); });
}

// Cierra listener y limpia archivo de socket.
UnixSocketListener::~UnixSocketListener() {
    if (listen_fd_ >= 0) {
        reactor_.remove(listen_fd_);
        ::close(listen_fd_);
    }
    ::unlink(socket_path_.c_str());
}

// Acepta clientes pendientes y notifica callback.
void UnixSocketListener::on_connection_ready() {
    while (true) {
        int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }

        std::string origin = "desconocido";
        struct ucred cred {};
        socklen_t len = sizeof(cred);
        if (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            origin = "pid:" + std::to_string(cred.pid);
        }
        on_accept_(client_fd, origin);
    }
}

// Detiene aceptacion de nuevas conexiones.
void UnixSocketListener::stop_accepting() {
    if (listen_fd_ >= 0) {
        reactor_.remove(listen_fd_);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

}  // namespace jobrunner