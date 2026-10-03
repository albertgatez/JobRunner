#include "./connection.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>

namespace jobrunner {

// Construye una conexion cliente.
// reactor: loop de eventos.
// fd: socket del cliente.
// on_request: callback para procesar payloads.
// on_closed: callback al cerrar conexion.
Connection::Connection(Reactor& reactor, int fd, RequestHandlerFn on_request,
                        ClosedCallback on_closed)
    : reactor_(reactor),
      fd_(fd),
      on_request_(std::move(on_request)),
      on_closed_(std::move(on_closed)) {}

// Cierra el socket si sigue abierto.
Connection::~Connection() {
    if (!closed_) ::close(fd_);
}

// Registra lectura de este socket en el reactor.
void Connection::start() {
    reactor_.add_read(fd_, [this] { on_readable(); });
}

// Consume bytes de entrada y procesa frames completos.
void Connection::on_readable() {
    std::array<char, 4096> buf{};
    while (true) {
        ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n > 0) {
            framer_.feed(buf.data(), static_cast<std::size_t>(n));
        } else if (n == 0) {
            close_connection();
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            close_connection();
            return;
        }
    }

    try {
        while (auto payload = framer_.try_extract_frame()) {
            std::string response_json = on_request_(*payload);
            queue_write(FrameCodec::encode_frame(response_json));
        }
    } catch (const std::exception&) {
        close_connection();
    }
}

// Encola respuesta de salida.
// frame: bytes framed a enviar.
void Connection::queue_write(std::string frame) {
    bool was_idle = out_offset_ >= out_buffer_.size();
    out_buffer_.append(frame);
    if (was_idle) on_writable();
}

// Intenta vaciar buffer de salida al socket.
void Connection::on_writable() {
    while (out_offset_ < out_buffer_.size()) {
        ssize_t n = ::write(fd_, out_buffer_.data() + out_offset_, out_buffer_.size() - out_offset_);
        if (n > 0) {
            out_offset_ += static_cast<std::size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            reactor_.set_write_interest(fd_, true, [this] { on_writable(); });
            return;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            close_connection();
            return;
        }
    }
    out_buffer_.clear();
    out_offset_ = 0;
    if (reactor_.is_registered(fd_)) {
        reactor_.set_write_interest(fd_, false, nullptr);
    }
}

// Cierra y desmonta conexion del reactor.
void Connection::close_connection() {
    if (closed_) return;
    closed_ = true;
    reactor_.remove(fd_);
    ::close(fd_);
    if (on_closed_) on_closed_();
}

}  // namespace jobrunner