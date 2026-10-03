#pragma once

#include <functional>
#include <memory>
#include <string>

#include "../io/reactor.hpp"
#include "../protocol/frame_codec.hpp"

namespace jobrunner {

/**
 * Conexion de un cliente sobre socket stream.
 */
class Connection : public std::enable_shared_from_this<Connection> {
   public:
    using RequestHandlerFn = std::function<std::string(const std::string& request_json)>;
    using ClosedCallback = std::function<void()>;

    /**
     * Construye una conexion cliente.
     * @param reactor Reactor para registrar IO no bloqueante.
     * @param fd Descriptor de socket del cliente.
     * @param on_request Callback de procesamiento de request.
     * @param on_closed Callback opcional al cerrar la conexion.
     */
    Connection(Reactor& reactor, int fd, RequestHandlerFn on_request,
               ClosedCallback on_closed = nullptr);

    /**
     * Libera recursos de la conexion.
     */
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /**
     * Inicia el manejo de lectura en el reactor.
     */
    void start();

   private:
    /**
     * Lee bytes del socket y procesa frames completos.
     */
    void on_readable();

    /**
     * Drena el buffer de salida al socket.
     */
    void on_writable();

    /**
     * Encola una respuesta framed para envio.
     * @param frame Bytes completos del frame.
     */
    void queue_write(std::string frame);

    /**
     * Cierra el socket y desmonta la conexion del reactor.
     */
    void close_connection();

    Reactor& reactor_;
    int fd_;
    RequestHandlerFn on_request_;
    ClosedCallback on_closed_;
    FrameCodec framer_;
    std::string out_buffer_;
    std::size_t out_offset_{0};
    bool closed_{false};
};

}  // namespace jobrunner