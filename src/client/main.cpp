// ═══════════════════════════════════════════════════════════════════════════
// client/main.cpp — Cliente CLI de JobRunner
// ═══════════════════════════════════════════════════════════════════════════
// Cliente de línea de comandos que se conecta al servidor JobRunner vía
// socket Unix. Envía un solo request por invocación y muestra la respuesta
// JSON formateada. A diferencia del servidor (event loop con epoll), el
// cliente usa I/O bloqueante simple: solo tiene un request en vuelo.
//
// Uso:
//   jobrunner-cli <socket> submit <comando> [args...]
//   jobrunner-cli <socket> status <id>
//   jobrunner-cli <socket> list [estado]
//   jobrunner-cli <socket> cancel <id>

#include <sys/socket.h>   // socket(), connect(), AF_UNIX
#include <sys/un.h>       // sockaddr_un
#include <unistd.h>       // read(), write(), close()

#include <cstdlib>        // EXIT_SUCCESS, EXIT_FAILURE
#include <cstring>        // strncpy, strtoull
#include <iostream>       // std::cout, std::cerr
#include <nlohmann/json.hpp>// Librería JSON: parse, serialize, manipulación
#include <string>         // std::string

#include "../protocol/frame_codec.hpp"  // FrameCodec: framing [4 bytes len][payload]

using json = nlohmann::json;
using jobrunner::FrameCodec;

// ═══════════════════════════════════════════════════════════════════════════
// Namespace anónimo: funciones auxiliares internas del cliente
// ═══════════════════════════════════════════════════════════════════════════
namespace {

// ═══════════════════════════════════════════════════════════════════════════
// connect_socket: Crea y conecta un socket Unix al servidor
// ═══════════════════════════════════════════════════════════════════════════
// Crea un socket AF_UNIX/SOCK_STREAM y lo conecta a la ruta especificada.
// Retorna el file descriptor conectado, o -1 en caso de error.
int connect_socket(const std::string& path) {  // path: ruta del socket Unix (ej: "/tmp/jobrunner.sock")
    // Crear socket Unix orientado a conexión (SOCK_STREAM)
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);  // fd: file descriptor del socket
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    // Preparar la dirección del socket Unix
    sockaddr_un addr{};              // addr: estructura de dirección del socket Unix
    addr.sun_family = AF_UNIX;       // Familia: Unix domain socket
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);  // Copiar ruta (con límite de tamaño)

    // Conectar al servidor, retornando -1 en caso de error
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("connect");
        ::close(fd);
        return -1;
    }
    return fd;  // Retornar fd conectado
}

// ═══════════════════════════════════════════════════════════════════════════
// send_all: Envía todos los datos por el socket (loop bloqueante)
// ═══════════════════════════════════════════════════════════════════════════
// A diferencia de write() simple que puede enviar parcialmente, esta función
// garantiza que todos los bytes sean enviados. Retorna true si se envió todo.
bool send_all(int fd, const std::string& data) {  // fd: socket, data: bytes a enviar
    std::size_t sent = 0;  // sent: bytes enviados hasta ahora
    while (sent < data.size()) {
        // Escribir desde la posición actual
        ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);  // n: bytes escritos en este intento
        if (n <= 0) return false;  // Error o conexión cerrada
        sent += static_cast<std::size_t>(n);  // Avanzar el offset
    }
    return true;  // Todos los bytes enviados
}

// ═══════════════════════════════════════════════════════════════════════════
// read_response: Lee exactamente una respuesta framed del servidor
// ═══════════════════════════════════════════════════════════════════════════
// A diferencia del servidor (que usa epoll para manejar múltiples conexiones
// simultáneas), el cliente solo tiene un request en vuelo, así que un loop
// bloqueante simple es suficiente. Usa FrameCodec para reensamblar el frame
// a partir de lecturas parciales.
bool read_response(int fd, std::string& out_json) {  // fd: socket, out_json: resultado (JSON del frame)
    FrameCodec framer;    // framer: decodificador de frames (acumula bytes)
    char buf[4096];       // buf: buffer de lectura temporal

    while (true) {
        // Intentar extraer un frame completo con los bytes acumulados
        if (auto frame = framer.try_extract_frame()) {
            out_json = *frame;  // Guardar el payload JSON extraído
            return true;        // Frame completo recibido
        }

        // Leer más bytes del socket (bloqueante)
        ssize_t n = ::read(fd, buf, sizeof(buf));  // n: bytes leídos
        if (n <= 0) return false;  // Error o conexión cerrada

        // Alimentar los bytes al decodificador de frames
        framer.feed(buf, static_cast<std::size_t>(n));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// print_usage: Muestra la ayuda de uso del CLI en stderr
// ═══════════════════════════════════════════════════════════════════════════
void print_usage() {
    std::cerr << "uso: jobrunner-cli <socket> submit <comando> [args...]\n"
              << "     jobrunner-cli <socket> status <id>\n"
              << "     jobrunner-cli <socket> list [estado]\n"
              << "     jobrunner-cli <socket> cancel <id>\n";
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// main: Punto de entrada del cliente CLI
// ═══════════════════════════════════════════════════════════════════════════
int main(int argc, char** argv) {  // argc: cantidad de argumentos, argv: valores
    // ── Validación mínima: necesitamos al menos <socket> y <op> ──────────
    if (argc < 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    // ── Parsear argumentos principales ───────────────────────────────────
    std::string socket_path = argv[1];  // socket_path: ruta del socket Unix
    std::string op = argv[2];          // op: operación a ejecutar (submit/status/list/cancel)

    // ── Construir el request JSON según la operación ─────────────────────
    json request;  // request: objeto JSON que se enviará al servidor

    if (op == "submit") {
        // submit requiere al menos un comando
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "submit";
        request["command"] = argv[3];  // comando a ejecutar
        json args = json::array();     // args: array JSON de argumentos
        for (int i = 4; i < argc; ++i) args.push_back(argv[i]);  // Agregar cada argumento
        request["args"] = args;

    } else if (op == "status") {
        // status requiere un ID de job
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "status";
        request["id"] = std::strtoull(argv[3], nullptr, 10);  // id: convertir string a uint64

    } else if (op == "list") {
        // list es opcionalmente filtrado por estado
        request["op"] = "list";
        if (argc >= 4) request["state"] = argv[3];  // state opcional: Queued/Running/Succeeded/Failed/Canceled

    } else if (op == "cancel") {
        // cancel requiere un ID de job
        if (argc < 4) {
            print_usage();
            return EXIT_FAILURE;
        }
        request["op"] = "cancel";
        request["id"] = std::strtoull(argv[3], nullptr, 10);  // id: convertir string a uint64

    } else {
        // Operación desconocida
        print_usage();
        return EXIT_FAILURE;
    }

    // ── Conectar al servidor ─────────────────────────────────────────────
    int fd = connect_socket(socket_path);  // fd: socket conectado
    if (fd < 0) return EXIT_FAILURE;

    // ── Enviar el request como frame ─────────────────────────────────────
    // FrameCodec::encode_frame: [4 bytes longitud big-endian][payload JSON]
    if (!send_all(fd, FrameCodec::encode_frame(request.dump()))) {
        std::cerr << "error: no se pudo enviar la solicitud\n";
        ::close(fd);
        return EXIT_FAILURE;
    }

    // ── Leer la respuesta del servidor ───────────────────────────────────
    std::string response_json;  // response_json: payload JSON de la respuesta
    if (!read_response(fd, response_json)) {
        std::cerr << "error: el servicio no respondio (¿esta corriendo?)\n";
        ::close(fd);
        return EXIT_FAILURE;
    }
    ::close(fd);  // Cerrar conexión (un solo request por invocación)

    // ── Parsear y mostrar la respuesta ───────────────────────────────────
    json response = json::parse(response_json, nullptr, false);  // response: JSON parseado (sin lanzar excepciones)
    if (response.is_discarded()) {
        std::cerr << "error: respuesta invalida del servicio\n";
        return EXIT_FAILURE;
    }

    // Imprimir JSON formateado con indentación de 2 espacios
    std::cout << response.dump(2) << "\n";

    // Retornar éxito si "ok" es true, fallo caso contrario
    return response.value("ok", false) ? EXIT_SUCCESS : EXIT_FAILURE;
}
