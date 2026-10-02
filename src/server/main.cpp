// server/main.cpp — Punto de entrada del servidor JobRunner
// Este archivo ensambla todos los módulos del sistema y arranca el
// event loop. Es el "compositor" de la arquitectura: conecta dominio,
// I/O, red y procesos en un único hilo de control.

#include <csignal>       // Necesario para SIGINT/SIGTERM y signalfd_siginfo
#include <cstdlib>       // EXIT_SUCCESS
#include <iostream>      // std::cerr (por si falla el logger)
#include <memory>        // std::shared_ptr
#include <string>        // std::string
#include <unordered_map> // Mapa fd→Connection

#include "../common/logger.hpp"                     // Logger single-threaded
#include "../domain/in_memory_job_store.hpp"        // Persistencia en memoria
#include "../domain/job_manager.hpp"                // Orquestador de jobs
#include "../io/reactor.hpp"                        // Event loop (epoll+signalfd)
#include "../network/connection.hpp"                // Conexión por cliente
#include "../network/unix_socket_listener.hpp"      // Acepta en socket Unix
#include "../process/posix_process_launcher.hpp"    // fork()+execvp()
#include "./request_handler.hpp"                    // Traduce JSON↔JobManager

using namespace jobrunner;

int main(int argc, char** argv) {
    std::string socket_path = "/tmp/jobrunner.sock";
    if (argc > 1) socket_path = argv[1];

    // Instancia el logger del sistema. Es single-threaded por diseño:
    // todo el servidor corre en un solo hilo (el Reactor), así que no
    // necesita mutexs. Escribe eventos operacionales (no salida de jobs)
    // con formato: TIMESTAMP [LEVEL] mensaje.
    // Se pasa a JobManager para que registre eventos
    // del ciclo de vida de los jobs (submit, exit, error, etc.).
    Logger logger;

    // Implementación de IJobStore respaldada por un std::map<JobId, Job>
    // en memoria. No requiere locks (single-threaded). Es reemplazable
    // por SqliteJobStore (Hito 2) sin tocar JobManager, gracias a la
    // inyección de dependencias. Contiene todos los jobs con su estado,
    // timestamps, PIDs y capturas de stdout/stderr.
    InMemoryJobStore store;

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
    JobManager manager(store, launcher, logger);

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
    // Esto detiene la aceptación de trabajos nuevos y el event loop.
    // OJO: no espera ni termina los jobs en ejecución; pueden quedar
    // huérfanos (pendiente, RNF-30).
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
    // Loguea que el event loop terminó y el servidor se detuvo limpiamente.
    logger.info("JobRunner detenido");

    // Retorna 0 al SO. En este punto:
    //   • El listener ya no acepta conexiones (stop_accepting)
    //   • Todas las Connection se destruyen (fin del scope)
    //   • Los shared_ptr<Connection> en connections se liberan
    //   • Los destructores de cada módulo liberan recursos (fds, etc.)
    // Los jobs en ejecución NO se terminan aquí: cada uno tiene su propio
    // grupo de procesos y no recibe SIGHUP al cerrar el servidor, así que
    // pueden quedar huérfanos. Pendiente (RNF-30): enviar SIGTERM a los
    // jobs actibos y esperarlos antes de salir.
    return EXIT_SUCCESS;
}