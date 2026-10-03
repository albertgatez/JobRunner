#pragma once

#include <functional>
#include <string>

#include "../io/reactor.hpp"

namespace jobrunner {

/**
 * Listener para conexiones entrantes en socket Unix.
 */
class UnixSocketListener {
   public:
    using AcceptCallback = std::function<void(int client_fd, std::string origin)>;

    /**
     * Construye y registra el listener en el reactor.
     * @param reactor Loop de eventos.
     * @param socket_path Ruta del archivo de socket.
     * @param on_accept Callback para cada cliente aceptado.
     */
    UnixSocketListener(Reactor& reactor, std::string socket_path, AcceptCallback on_accept);

    /**
     * Libera recursos del listener.
     */
    ~UnixSocketListener();

    UnixSocketListener(const UnixSocketListener&) = delete;
    UnixSocketListener& operator=(const UnixSocketListener&) = delete;

    /**
     * Detiene la aceptacion de nuevas conexiones.
     */
    void stop_accepting();

   private:
    /**
     * Acepta conexiones pendientes y notifica via callback.
     */
    void on_connection_ready();

    Reactor& reactor_;
    std::string socket_path_;
    AcceptCallback on_accept_;
    int listen_fd_{-1};
};

}  // namespace jobrunner