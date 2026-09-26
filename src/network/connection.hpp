#pragma once

#include <functional>
#include <memory>
#include <string>

#include "../io/reactor.hpp"
#include "../protocol/frame_codec.hpp"

namespace jobrunner {

// One client connection: reads bytes non-blockingly, reassembles frames via
// FrameCodec, hands each request payload to `on_request`, and writes back
// whatever it returns — buffering partial writes itself so a slow client
// never blocks the reactor (RF-21, RF-22, RNF-08).
class Connection : public std::enable_shared_from_this<Connection> {
   public:
    using RequestHandlerFn = std::function<std::string(const std::string& request_json)>;
    using ClosedCallback = std::function<void()>;

    Connection(Reactor& reactor, int fd, RequestHandlerFn on_request,
               ClosedCallback on_closed = nullptr);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void start();

   private:
    void on_readable();
    void on_writable();
    void queue_write(std::string frame);
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