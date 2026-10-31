// Es el "compositor" de la arquitectura: conecta dominio, I/O, red y procesos en un único hilo de control.

#include <algorithm>     // std::min
#include <chrono>        // Plazos del cierre controlado
#include <csignal>       // Necesario para SIGINT/SIGTERM y signalfd_siginfo
#include <cstdlib>       // EXIT_SUCCESS / EXIT_FAILURE
#include <exception>     // std::exception
#include <filesystem>    // Crear el directorio de datos
#include <iostream>      // std::cerr (por si falla el logger)
#include <memory>        // std::shared_ptr
#include <string>        // std::string
#include <string_view>   // Comparación de argumentos
#include <unordered_map> // Mapa fd→Connection
#include <vector>        // Argumentos posicionales

#include "../common/logger.hpp"                     // Logger single-threaded
#include "../domain/sqlite_job_store.hpp"           // Persistencia en SQLite (ADR-004)
#include "../domain/job_manager.hpp"                // Orquestador de jobs
#include "../io/reactor.hpp"                        // Event loop (epoll+signalfd)
#include "../network/connection.hpp"                // Conexión por cliente
#include "../network/unix_socket_listener.hpp"      // Acepta en socket Unix
#include "../process/posix_process_launcher.hpp"    // fork()+execvp()
#include "./request_handler.hpp"                    // Traduce JSON↔JobManager

using namespace jobrunner;

namespace {

// Plazo que tienen los jobs para atender el SIGTERM del cierre controlado
// antes de escalar a SIGKILL (mismo valor propuesto en ADR-006), y plazo
// extra para recolectarlos tras el SIGKILL.
constexpr std::chrono::seconds kShutdownGracePeriod{5};
constexpr std::chrono::seconds kKillGracePeriod{1};

// Configuración de arranque leída de la línea de comandos.
struct ServerOptions {
    std::string socket_path = "/tmp/jobrunner.sock";
    std::size_t max_concurrent_jobs = 3;   // RF-05
    std::string data_dir = "data";         // RF-16: aquí vive jobrunner.db
    bool reset_db = false;                 // Borrar el historial al arrancar
};

// Muestra la ayuda de uso del servidor.
// out: flujo de salida.
void print_usage(std::ostream& out) {
    out << "uso: jobrunner-server [socket] [max_concurrentes] [opciones]\n"
           "\n"
           "  socket              ruta del socket Unix (por defecto /tmp/jobrunner.sock)\n"
           "  max_concurrentes    jobs ejecutándose a la vez (por defecto 3)\n"
           "\n"
           "opciones:\n"
           "  --data-dir DIR      directorio de datos; la base es DIR/jobrunner.db\n"
           "                      (por defecto ./data)\n"
           "  --reset-db          borra todos los registros de la base antes de arrancar\n"
           "  -h, --help          muestra esta ayuda\n";
}

// Interpreta argv. Los argumentos posicionales conservan el orden de Hito 1
// (socket, límite de concurrencia); las opciones con "--" van en cualquier lugar.
// Lanza std::invalid_argument ante un argumento desconocido o incompleto.
ServerOptions parse_options(int argc, char** argv) {
    ServerOptions opts;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--reset-db") {
            opts.reset_db = true;
        } else if (arg == "--data-dir") {
            if (i + 1 >= argc) throw std::invalid_argument("--data-dir requiere un directorio");
            opts.data_dir = argv[++i];
        } else if (arg.starts_with("-")) {
            throw std::invalid_argument("opción desconocida: " + std::string(arg));
        } else {
            positional.emplace_back(arg);
        }
    }
    if (positional.size() > 2) throw std::invalid_argument("demasiados argumentos");
    if (positional.size() > 0) opts.socket_path = positional[0];
    if (positional.size() > 1) opts.max_concurrent_jobs = std::stoul(positional[1]);
    return opts;
}

// Mantiene vivo el event loop hasta que todos los jobs terminen o venza el
// plazo, para que el SIGCHLD de cada uno se recolecte (sin zombies).
// reactor: event loop ya detenido con stop().
// manager: dueño del conteo de procesos vivos.
// grace: tiempo máximo de espera.
void drain_live_processes(Reactor& reactor, const JobManager& manager,
                          std::chrono::milliseconds grace) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + grace;
    while (manager.has_live_processes()) {
        auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now());
        if (remaining.count() <= 0) return;
        reactor.poll_once(static_cast<int>(std::min<long long>(remaining.count(), 100)));
    }
}

// Arranca y ejecuta el servidor hasta el cierre controlado.
// opts: configuración de arranque.
int run_server(const ServerOptions& opts) {
    const std::string& socket_path = opts.socket_path;

    // Instancia el logger del sistema. Es single-threaded por diseño:
    // todo el servidor corre en un solo hilo (el Reactor), así que no
    // necesita mutexs. Escribe eventos operacionales (no salida de jobs)
    // con formato: TIMESTAMP [LEVEL] mensaje.
    // Se pasa a JobManager para que registre eventos
    // del ciclo de vida de los jobs (submit, exit, error, etc.).
    Logger logger;

    // Implementación de IJobStore respaldada por SQLite (ADR-004): una fila
    // por job en DATA_DIR/jobrunner.db, una transacción por actualización.
    // Reemplaza a InMemoryJobStore sin tocar JobManager (inyección de
    // dependencias). Falla al arrancar si otra instancia ya usa la base.
    // Con --reset-db se borra el historial antes de recuperar nada.
    std::filesystem::create_directories(opts.data_dir);
    const std::string db_path = opts.data_dir + "/jobrunner.db";
    SqliteJobStore store(db_path);
    logger.info("base de datos: " + db_path);
    if (opts.reset_db) {
        std::size_t removed = store.reset();
        logger.info("base de datos reiniciada (--reset-db): " + std::to_string(removed) +
                    " registro(s) eliminado(s)");
    }

    // El corazón del sistema: event loop single-threaded basado en
    // epoll(7) + signalfd(2). Maneja:
    //   • Eventos de red (conexiones entrantes, pipes de stdout/stderr)
    //   • Señales (SIGCHLD para esperar hijos, SIGINT/SIGTERM para apagado)
    // Un solo hilo despacha todo → no hay race conditions ni locks.
    // Se pasa a launcher y a cada Connection.
    Reactor reactor;

    // Implementación real de IProcessLauncher. Usa fork()+execvp() para
    // lanzar procesos hijos. Recibe el Reactor por referencia para
    // registrar los pipes de stdout/stderr como fuentes de eventos.
    // Cuando el proceso hijo termina, el Reaper (dentro del launcher)
    // atiende SIGCHLD vía signalfd y hace waitpid(-1, WNOHANG).
    // También usa setpgid(0,0) para que cada job tenga su propio
    // grupo de procesos (necesario para kill(-pid,...) en cancelación).
    PosixProcessLauncher launcher(reactor);

    // El orquestador central del dominio. Recibe:
    //   • store      → para crear/actualizar/consultar jobs
    //   • launcher   → para despachar jobs como procesos reales
    //   • logger     → para registrar eventos operacionales
    // Gestiona: validación de comandos, detección de duplicados
    // (ventana de 3 segundos por cliente), transiciones de estado,
    // captura de salida y cancelación. Es el único punto que conoce
    // el ciclo de vida completo de un job.
    JobManager manager(store, launcher, logger, opts.max_concurrent_jobs);

    // Recuperación (ADR-005), antes de aceptar conexiones: los RUNNING de
    // una instancia anterior que murió de forma abrupta pasan a FAILED
    // (terminando su proceso si sigue vivo y es el mismo), y los QUEUED
    // vuelven a la cola en orden de llegada.
    manager.recover();

    // Capa de traducción JSON ↔ JobManager. Recibe el manager. 
    // Expone handle(json, origin) que interpreta las
    // operaciones: submit, status, list, cancel. Retorna JSON de
    // respuesta con estructura {ok, job_id, job/jobs, error}.
    // El campo "origin" (pid del cliente vía SO_PEERCRED) alimenta
    // la ventana de dedup: mismo comando+args+origen en <3s = duplicado.
    RequestHandler handler(manager);

    // Mapa de conexiones activas: file descriptor → Connection.
    // Se usa para que el lambda de cierre de Connection (línea 36)
    // pueda eliminar la entrada del mapa cuando el cliente se desconecta.
    // El shared_ptr garantiza que la Connection viva aunque el mapa
    // se modifique. Al salir del scope de main, se destruyen todas.
    std::unordered_map<int, std::shared_ptr<Connection>> connections;

    // Crea el listener sobre el socket Unix. Al aceptar una conexión,
    // invoca el lambda callback con:
    //   • client_fd → file descriptor aceptado
    //   • origin    → "pid:XXXX" obtenido vía SO_PEERCRED
    //
    // El lambda hace tres cosas:
    //   1. Crea un shared_ptr<Connection> con dos callbacks:
    //      - on_request: llama handler.handle(req, origin)
    //      - on_close:   borra connections[client_fd]
    //   2. Lo registra en el mapa connections
    //   3. Llama conn->start() para registrar el fd en el Reactor
    //
    // El patrón de borrar del mapa en on_close evita fugas de memoria
    // cuando los clientes se desconectan.
    UnixSocketListener listener(reactor, socket_path, [&](int client_fd, std::string origin) {
        auto conn = std::make_shared<Connection>(
            reactor, client_fd,
            // Callback de request: delega en RequestHandler
            [&handler, origin](const std::string& req) { return handler.handle(req, origin); },
            // Callback de cierre: auto-limpieza del mapa
            [&connections, client_fd] { connections.erase(client_fd); });
        connections[client_fd] = conn;  // Mantener viva la Connection
        conn->start();                  // Registrar fd en epoll
    });

    // Registra manejador para Ctrl+C (SIGINT). Cuando se recibe:
    //   1. Loguea el evento de apagado
    //   2. listener.stop_accepting() → cierra el listening socket
    //      (no se aceptan conexiones nuevas, pero las existentes
    //       pueden terminar sus requests en vuelo)
    //   3. reactor.stop() → desbloquea epoll_wait y sale del event loop
    // Esto detiene la aceptación de trabajos nuevos y el event loop; los
    // jobs en ejecución se cancelan después de run() (ver más abajo).
    reactor.on_signal(SIGINT, [&](const struct signalfd_siginfo&) {
        logger.info("señal de apagado recibida, dejando de aceptar trabajos nuevos");
        listener.stop_accepting();
        reactor.stop();
    });

    // Idéntico a SIGINT pero para SIGTERM (kill PID por defecto).
    // Permite apagado graceful vía: kill <pid> o systemctl stop.
    // Sin esto, SIGTERM mataría el proceso sin limpiar recursos.
    reactor.on_signal(SIGTERM, [&](const struct signalfd_siginfo&) {
        logger.info("señal de apagado recibida, dejando de aceptar trabajos nuevos");
        listener.stop_accepting();
        reactor.stop();
    });

    // Loguea el inicio del servidor con la ruta del socket. Aparece
    // en el log como: TIMESTAMP [INFO] JobRunner escuchando en /tmp/jobrunner.sock
    logger.info("JobRunner escuchando en " + socket_path);

    // BLOQUEANTE: entra en el event loop infinito (epoll_wait).
    // Despacha eventos de:
    //   • Conexiones de clientes (UnixSocketListener)
    //   • Pipes de salida de procesos (stdout/stderr de jobs)
    //   • Señales (SIGCHLD para waitpid, SIGINT/SIGTERM para apagado)
    // Solo retorna cuando se llama reactor.stop() (por señal de apagado).
    // Aquí es donde "vive" el servidor durante toda su ejecución.
    reactor.run();

    // Se alcanza solo después de un shutdown controlado (SIGINT/SIGTERM).
    // Cierre sin huérfanos (RF-15, RNF-30):
    //   1. begin_shutdown(): los jobs RUNNING pasan a CANCELED (con
    //      finished_at) y reciben SIGTERM; se rechazan solicitudes nuevas
    //      de clientes ya conectados y la cola deja de despacharse. Los
    //      QUEUED se quedan así en la base y se reencolan al volver a arrancar.
    //   2. Se sigue girando el event loop hasta que todos terminen, para
    //      recolectarlos con waitpid y guardar su salida final.
    //   3. Si alguno ignora SIGTERM durante el plazo, se escala a SIGKILL.
    manager.begin_shutdown();
    drain_live_processes(reactor, manager, kShutdownGracePeriod);
    if (manager.has_live_processes()) {
        manager.kill_live_processes();
        drain_live_processes(reactor, manager, kKillGracePeriod);
    }
    if (manager.has_live_processes()) {
        logger.error("quedaron procesos sin recolectar al cerrar el servidor");
    }

    // Loguea que el servidor se detuvo limpiamente.
    logger.info("JobRunner detenido");

    // Retorna 0 al SO. En este punto:
    //   • El listener ya no acepta conexiones (stop_accepting)
    //   • Todas las Connection se destruyen (fin del scope)
    //   • Los shared_ptr<Connection> en connections se liberan
    //   • Los destructores de cada módulo liberan recursos (fds, base, etc.)
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    ServerOptions opts;
    try {
        if (argc > 1 && (std::string_view(argv[1]) == "-h" ||
                         std::string_view(argv[1]) == "--help")) {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }
        opts = parse_options(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "jobrunner-server: " << e.what() << "\n\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    // Errores de arranque (base bloqueada por otra instancia, esquema
    // incompatible, socket ocupado...) terminan el proceso con un mensaje
    // claro en lugar de un std::terminate.
    try {
        return run_server(opts);
    } catch (const std::exception& e) {
        std::cerr << "jobrunner-server: error fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
