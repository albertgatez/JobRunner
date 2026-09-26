#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include "../common/logger.hpp"
#include "../domain/in_memory_job_store.hpp"
#include "../domain/job_manager.hpp"
#include "../io/reactor.hpp"
#include "../network/connection.hpp"
#include "../network/unix_socket_listener.hpp"
#include "../process/posix_process_launcher.hpp"
#include "./request_handler.hpp"

using namespace jobrunner;

int main(int argc, char** argv) {
    std::string socket_path = "/tmp/jobrunner.sock";
    if (argc > 1) socket_path = argv[1];

    Logger logger;
    InMemoryJobStore store;
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    JobManager manager(store, launcher, logger);
    RequestHandler handler(manager);

    std::unordered_map<int, std::shared_ptr<Connection>> connections;

    UnixSocketListener listener(reactor, socket_path, [&](int client_fd, std::string origin) {
        auto conn = std::make_shared<Connection>(
            reactor, client_fd,
            [&handler, origin](const std::string& req) { return handler.handle(req, origin); },
            [&connections, client_fd] { connections.erase(client_fd); });
        connections[client_fd] = conn;
        conn->start();
    });

    reactor.on_signal(SIGINT, [&](const struct signalfd_siginfo&) {
        logger.info("senal de apagado recibida, dejando de aceptar trabajos nuevos");
        listener.stop_accepting();
        reactor.stop();
    });
    reactor.on_signal(SIGTERM, [&](const struct signalfd_siginfo&) {
        logger.info("senal de apagado recibida, dejando de aceptar trabajos nuevos");
        listener.stop_accepting();
        reactor.stop();
    });

    logger.info("JobRunner escuchando en " + socket_path);
    reactor.run();
    logger.info("JobRunner detenido");
    return EXIT_SUCCESS;
}