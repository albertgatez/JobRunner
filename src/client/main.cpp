#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "../protocol/frame_codec.hpp"

using json = nlohmann::json;
using jobrunner::FrameCodec;

namespace {

int connect_socket(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("connect");
        ::close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// Blocking read of exactly one framed response. Unlike the server, the CLI
// only ever has a single request in flight, so a simple blocking loop is
// enough here.
bool read_response(int fd, std::string& out_json) {
    FrameCodec framer;
    char buf[4096];
    while (true) {
        if (auto frame = framer.try_extract_frame()) {
            out_json = *frame;
            return true;
        }
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) return false;
        framer.feed(buf, static_cast<std::size_t>(n));
    }
}

void print_usage() {
    std::cerr << "uso: jobrunner-cli <socket> submit <comando> [args...]\n"
              << "     jobrunner-cli <socket> status <id>\n"
              << "     jobrunner-cli <socket> list [estado]\n"
              << "     jobrunner-cli <socket> cancel <id>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    std::string socket_path = argv[1];
    std::string op = argv[2];

    json request;
    if (op == "submit") {
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "submit";
        request["command"] = argv[3];
        json args = json::array();
        for (int i = 4; i < argc; ++i) args.push_back(argv[i]);
        request["args"] = args;
    } else if (op == "status") {
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "status";
        request["id"] = std::strtoull(argv[3], nullptr, 10);
    } else if (op == "list") {
        request["op"] = "list";
        if (argc >= 4) request["state"] = argv[3];
    } else if (op == "cancel") {
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "cancel";
        request["id"] = std::strtoull(argv[3], nullptr, 10);
    } else {
        print_usage();
        return EXIT_FAILURE;
    }

    int fd = connect_socket(socket_path);
    if (fd < 0) return EXIT_FAILURE;

    if (!send_all(fd, FrameCodec::encode_frame(request.dump()))) {
        std::cerr << "error: no se pudo enviar la solicitud\n";
        ::close(fd);
        return EXIT_FAILURE;
    }

    std::string response_json;
    if (!read_response(fd, response_json)) {
        std::cerr << "error: el servicio no respondio (¿esta corriendo?)\n";
        ::close(fd);
        return EXIT_FAILURE;
    }
    ::close(fd);

    json response = json::parse(response_json, nullptr, false);
    if (response.is_discarded()) {
        std::cerr << "error: respuesta invalida del servicio\n";
        return EXIT_FAILURE;
    }

    std::cout << response.dump(2) << "\n";
    return response.value("ok", false) ? EXIT_SUCCESS : EXIT_FAILURE;
}