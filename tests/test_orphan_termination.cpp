// PosixProcessLauncher::fingerprint / terminate_orphan_group con procesos
// reales (ADR-005): tras una caida del servidor, al arrancar se terminan los
// restos del grupo de procesos de un job RUNNING, pero solo si su identidad
// coincide con la huella registrada. RNF-10, RNF-30. TC-007, TC-020.

#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>

#include "../src/io/reactor.hpp"
#include "../src/process/posix_process_launcher.hpp"

using jobrunner::PosixProcessLauncher;
using jobrunner::ProcessFingerprint;
using jobrunner::Reactor;

namespace {

// Lanza `sh -c script` como lider de su propio grupo, sin pasar por el
// launcher (como lo habria dejado una instancia anterior del servidor), y
// espera a que el script escriba "listo" para saber que ya creo a sus hijos.
pid_t spawn_group(const char* script) {
    int ready[2];
    REQUIRE(pipe(ready) == 0);
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        setpgid(0, 0);
        // Reactor bloquea SIGTERM en este proceso; el hijo no debe heredarlo.
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        dup2(ready[1], STDOUT_FILENO);
        close(ready[0]);
        close(ready[1]);
        execl("/bin/sh", "sh", "-c", script, static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(pid, pid);
    close(ready[1]);
    char buf[16];
    REQUIRE(read(ready[0], buf, sizeof(buf)) > 0);
    close(ready[0]);
    return pid;
}

// Espera (hasta 3 s) a que no quede ningun proceso en el grupo. El lider es
// hijo de la prueba y se recolecta aqui; sus hijos huerfanos los recolecta init.
bool group_gone(pid_t pgid) {
    waitpid(pgid, nullptr, 0);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::kill(-pgid, 0) != 0 && errno == ESRCH) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// Limpieza para las pruebas que esperan que el grupo NO se toque.
void kill_group(pid_t pgid) {
    ::kill(-pgid, SIGKILL);
    group_gone(pgid);
}

}  // namespace

TEST_CASE("fingerprint identifica un proceso vivo y falla si no existe", "[orphan]") {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    pid_t pid = spawn_group("echo listo; exec sleep 30");

    auto fp = launcher.fingerprint(pid);
    REQUIRE(fp.has_value());
    CHECK_FALSE(fp->boot_id.empty());
    CHECK(fp->start_time > 0);
    CHECK(launcher.fingerprint(pid)->start_time == fp->start_time);  // estable

    kill_group(pid);
    CHECK_FALSE(launcher.fingerprint(pid).has_value());
}

TEST_CASE("terminate_orphan_group termina al lider y a sus hijos", "[orphan][TC-020]") {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    pid_t pgid = spawn_group("sleep 30 & sleep 30 & echo listo; wait");
    auto fp = launcher.fingerprint(pgid);
    REQUIRE(fp.has_value());

    CHECK(launcher.terminate_orphan_group(pgid, *fp) == 3);  // sh + 2 sleep
    CHECK(group_gone(pgid));
}

TEST_CASE("terminate_orphan_group escala a SIGKILL si se ignora SIGTERM", "[orphan][TC-020]") {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    pid_t pgid = spawn_group("trap '' TERM; echo listo; while :; do sleep 0.1; done");
    auto fp = launcher.fingerprint(pgid);
    REQUIRE(fp.has_value());

    CHECK(launcher.terminate_orphan_group(pgid, *fp) >= 1);
    CHECK(group_gone(pgid));
}

TEST_CASE("terminate_orphan_group no toca un PID reciclado (starttime distinto)",
          "[orphan][RNF-10]") {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    pid_t pgid = spawn_group("echo listo; exec sleep 30");
    auto fp = launcher.fingerprint(pgid);
    REQUIRE(fp.has_value());

    ProcessFingerprint other_process{fp->boot_id, fp->start_time + 1};
    CHECK(launcher.terminate_orphan_group(pgid, other_process) == 0);
    CHECK(::kill(pgid, 0) == 0);  // sigue vivo

    kill_group(pgid);
}

TEST_CASE("terminate_orphan_group no toca nada si el equipo se reinicio", "[orphan][RNF-10]") {
    Reactor reactor;
    PosixProcessLauncher launcher(reactor);
    pid_t pgid = spawn_group("echo listo; exec sleep 30");
    auto fp = launcher.fingerprint(pgid);
    REQUIRE(fp.has_value());

    ProcessFingerprint other_boot{"otro-arranque", fp->start_time};
    CHECK(launcher.terminate_orphan_group(pgid, other_boot) == 0);
    CHECK(::kill(pgid, 0) == 0);

    kill_group(pgid);
}
