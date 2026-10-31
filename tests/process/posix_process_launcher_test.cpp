// PosixProcessLauncher REAL (fork/exec/pipes/SIGCHLD) sobre un Reactor real.
// RF-04, RF-10, RF-11, RF-29, RNF-30. TC-005, TC-006, TC-020.
//
// Estas pruebas existen por un bug concreto: el Reactor bloquea SIGTERM,
// SIGINT y SIGCHLD con sigprocmask() para leerlos via signalfd. Esa mascara
// la hereda el hijo tras fork() y SOBREVIVE a execvp(), asi que el job
// arrancaba con SIGTERM bloqueado: cancel() devolvia {"ok": true} pero el
// proceso seguia corriendo.
#include <catch2/catch_test_macros.hpp>

#include <signal.h>

#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "io/reactor.hpp"
#include "process/posix_process_launcher.hpp"
#include "support/watchdog.hpp"

using namespace jobrunner;
using jobrunner::testing::Watchdog;

namespace {

struct Outcome {
    pid_t pid{-1};
    bool exited{false};
    int exit_code{-99};
    int term_signal{0};
    std::string out;
    std::string err;
};

// Lanza `command`, opcionalmente hace algo justo despues del launch y corre el
// Reactor hasta que el hijo termina.
Outcome run_to_exit(const std::string& command, const std::vector<std::string>& args,
                    const std::function<void(PosixProcessLauncher&, pid_t)>& after_launch = {}) {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    Outcome o;

    o.pid = launcher.launch(
        command, args,
        [&](pid_t, bool is_stderr, std::string_view data) {
            (is_stderr ? o.err : o.out).append(data);
        },
        [&](pid_t, int code, int sig) {
            o.exited = true;
            o.exit_code = code;
            o.term_signal = sig;
            reactor.stop();
        });

    if (after_launch) after_launch(launcher, o.pid);
    reactor.run();
    return o;
}

// true si el proceso existe y NO es zombi. Un nieto huerfano que ya murio
// sigue apareciendo para kill(pid, 0) hasta que su nuevo padre lo recoge
// (en contenedores PID 1 a veces no lo hace), asi que miramos /proc.
bool is_running(pid_t pid) {
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat) return false;
    std::string line;
    std::getline(stat, line);
    auto close_paren = line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= line.size()) return false;
    return line[close_paren + 2] != 'Z' && line[close_paren + 2] != 'X';
}

}  // namespace

TEST_CASE("el codigo de salida del hijo llega a on_exit", "[launcher]") {
    Watchdog wd;
    auto o = run_to_exit("sh", {"-c", "exit 3"});
    REQUIRE(o.exited);
    REQUIRE(o.exit_code == 3);
    REQUIRE(o.term_signal == 0);
}

TEST_CASE("stdout y stderr llegan por canales separados (RF-11)", "[launcher]") {
    Watchdog wd;
    auto o = run_to_exit("sh", {"-c", "echo salida; echo error 1>&2"});
    REQUIRE(o.exited);
    REQUIRE(o.out == "salida\n");
    REQUIRE(o.err == "error\n");
}

TEST_CASE("un comando inexistente termina con codigo 127", "[launcher]") {
    Watchdog wd;
    auto o = run_to_exit("/no/existe/jobrunner-test", {});
    REQUIRE(o.exited);
    REQUIRE(o.exit_code == 127);
}

TEST_CASE("el hijo NO hereda las senales bloqueadas por el Reactor (regresion SIGTERM)",
          "[launcher][regression]") {
    Watchdog wd;
    // SigBlk es la mascara de senales bloqueadas del proceso. Con el bug
    // valia 0000000000014002 (SIGINT|SIGTERM|SIGCHLD); debe ser todo ceros.
    auto o = run_to_exit("grep", {"SigBlk", "/proc/self/status"});
    REQUIRE(o.exited);
    REQUIRE(o.exit_code == 0);
    INFO("salida de grep: " << o.out);
    REQUIRE(o.out.find("0000000000000000") != std::string::npos);
}

TEST_CASE("send_signal(SIGTERM) realmente termina al hijo (regresion SIGTERM)",
          "[launcher][regression]") {
    Watchdog wd;
    auto o = run_to_exit("sleep", {"30"}, [](PosixProcessLauncher& launcher, pid_t pid) {
        launcher.send_signal(pid, SIGTERM);
    });
    REQUIRE(o.exited);
    REQUIRE(o.term_signal == SIGTERM);
}

TEST_CASE("send_signal llega tambien a los nietos del grupo de procesos", "[launcher]") {
    Watchdog wd;
    // sh lanza un sleep en segundo plano y espera. El SIGTERM al grupo debe
    // matar a ambos; si solo muriera sh, el nieto quedaria huerfano (RNF-30).
    auto o = run_to_exit("sh", {"-c", "sleep 30 & echo $!; wait"},
                         [](PosixProcessLauncher& launcher, pid_t pid) {
                             // dar tiempo a que sh arranque el nieto
                             ::usleep(200 * 1000);
                             launcher.send_signal(pid, SIGTERM);
                         });
    REQUIRE(o.exited);

    pid_t grandchild = static_cast<pid_t>(std::stol(o.out));
    // Puede tardar un instante en morir, asi que reintentamos brevemente.
    bool gone = false;
    for (int i = 0; i < 50 && !gone; ++i) {
        gone = !is_running(grandchild);
        if (!gone) ::usleep(20 * 1000);
    }
    REQUIRE(gone);
}
