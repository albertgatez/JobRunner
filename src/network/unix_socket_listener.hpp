#pragma once

#include <functional>
#include <string>

#include "../io/reactor.hpp"

namespace jobrunner {

// Accepts connections on an AF_UNIX stream socket and hands each new fd to
// `on_accept`, along with a best-effort textual origin for that peer (its
// pid, via SO_PEERCRED) used to scope duplicate-request detection per
// client (RF-27). In Hito 3 a TcpListener implementing the same shape adds
// LAN/VPN-restricted remote access (RF-18..RF-20) without JobManager,
// Connection or the protocol layer changing at all.
class UnixSocketListener {
   public:
    using AcceptCallback = std::function<void(int client_fd, std::string origin)>;

    UnixSocketListener(Reactor& reactor, std::string socket_path, AcceptCallback on_accept);
    ~UnixSocketListener();

    UnixSocketListener(const UnixSocketListener&) = delete;
    UnixSocketListener& operator=(const UnixSocketListener&) = delete;

    // RF-15: stop accepting new jobs during a controlled shutdown, while
    // existing connections keep being served.
    void stop_accepting();

   private:
    void on_connection_ready();

    Reactor& reactor_;
    std::string socket_path_;
    AcceptCallback on_accept_;
    int listen_fd_{-1};
};

}  // namespace jobrunner