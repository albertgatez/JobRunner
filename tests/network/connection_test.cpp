// Connection sobre un socketpair + Reactor real. RF-21, RF-22, RNF-08, RNF-14,
// RNF-24. TC-012, TC-019.
#include <catch2/catch_test_macros.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "io/reactor.hpp"
#include "network/connection.hpp"
#include "protocol/frame_codec.hpp"
#include "support/watchdog.hpp"

using namespace jobrunner;
using jobrunner::testing::Watchdog;

namespace {

struct SocketPair {
    int server{-1};
    int client{-1};

    SocketPair() {
        int sv[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
        server = sv[0];
        client = sv[1];
    }
    ~SocketPair() {
        if (client >= 0) ::close(client);
        // `server` lo cierra Connection.
    }
    void close_client() {
        ::close(client);
        client = -1;
    }
};

void write_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        REQUIRE(n > 0);
        off += static_cast<std::size_t>(n);
    }
}

// Lee de `fd` (no bloqueante) todos los frames disponibles.
std::vector<std::string> read_frames(int fd) {
    FrameCodec codec;
    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        codec.feed(buf, static_cast<std::size_t>(n));
    }
    std::vector<std::string> frames;
    while (auto f = codec.try_extract_frame()) frames.push_back(*f);
    return frames;
}

}  // namespace

TEST_CASE("una solicitud valida recibe su respuesta enmarcada", "[connection]") {
    Watchdog wd;
    Reactor reactor;
    SocketPair sp;

    std::string seen;
    auto conn = std::make_shared<Connection>(reactor, sp.server, [&](const std::string& req) {
        seen = req;
        reactor.stop();
        return std::string("pong");
    });
    conn->start();

    write_all(sp.client, FrameCodec::encode_frame("ping"));
    reactor.run();

    REQUIRE(seen == "ping");
    auto frames = read_frames(sp.client);
    REQUIRE(frames == std::vector<std::string>{"pong"});
}

TEST_CASE("dos solicitudes seguidas en un mismo write se responden en orden (RNF-24)",
          "[connection]") {
    Watchdog wd;
    Reactor reactor;
    SocketPair sp;

    std::vector<std::string> seen;
    auto conn = std::make_shared<Connection>(reactor, sp.server, [&](const std::string& req) {
        seen.push_back(req);
        if (seen.size() == 2) reactor.stop();
        return "re:" + req;
    });
    conn->start();

    write_all(sp.client, FrameCodec::encode_frame("a") + FrameCodec::encode_frame("b"));
    reactor.run();

    REQUIRE(seen == std::vector<std::string>{"a", "b"});
    REQUIRE(read_frames(sp.client) == std::vector<std::string>{"re:a", "re:b"});
}

TEST_CASE("una solicitud fragmentada en trozos pequenos se reensambla (RF-21)", "[connection]") {
    Watchdog wd;
    Reactor reactor;
    SocketPair sp;

    std::string seen;
    auto conn = std::make_shared<Connection>(reactor, sp.server, [&](const std::string& req) {
        seen = req;
        reactor.stop();
        return std::string("ok");
    });
    conn->start();

    const std::string frame = FrameCodec::encode_frame(R"({"op":"list"})");
    std::thread writer([&] {
        for (std::size_t i = 0; i < frame.size(); i += 3) {
            ssize_t n = ::write(sp.client, frame.data() + i, std::min<std::size_t>(3, frame.size() - i));
            (void)n;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    reactor.run();
    writer.join();

    REQUIRE(seen == R"({"op":"list"})");
    REQUIRE(read_frames(sp.client) == std::vector<std::string>{"ok"});
}

TEST_CASE("un frame que declara mas de kMaxFrameSize cierra la conexion (RNF-14)",
          "[connection]") {
    Watchdog wd;
    Reactor reactor;
    SocketPair sp;

    bool handler_called = false;
    bool closed = false;
    auto conn = std::make_shared<Connection>(
        reactor, sp.server,
        [&](const std::string&) {
            handler_called = true;
            return std::string();
        },
        [&] {
            closed = true;
            reactor.stop();
        });
    conn->start();

    write_all(sp.client, std::string("\x00\x10\x00\x01", 4));  // kMaxFrameSize + 1
    reactor.run();

    REQUIRE(closed);
    REQUIRE_FALSE(handler_called);
    char c;
    REQUIRE(::read(sp.client, &c, 1) == 0);  // EOF: el servidor cerro su extremo
}

TEST_CASE("la desconexion del cliente se detecta y se notifica (RF-22, RNF-08)",
          "[connection]") {
    Watchdog wd;
    Reactor reactor;
    SocketPair sp;

    bool closed = false;
    auto conn = std::make_shared<Connection>(
        reactor, sp.server, [](const std::string&) { return std::string(); },
        [&] {
            closed = true;
            reactor.stop();
        });
    conn->start();

    sp.close_client();
    reactor.run();

    REQUIRE(closed);
}
