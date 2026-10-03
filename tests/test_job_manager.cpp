// TC-001 (RF-01, RF-02), TC-003 (RF-06, RF-07), TC-005 (RF-10) y TC-018 (RF-27)
// a nivel unitario: JobManager con un lanzador de procesos falso (sin fork).

#include <catch2/catch_test_macros.hpp>
#include <csignal>

#include "../src/common/logger.hpp"
#include "../src/domain/in_memory_job_store.hpp"
#include "../src/domain/job_manager.hpp"
#include "fakes/fake_process_launcher.hpp"

using jobrunner::InMemoryJobStore;
using jobrunner::JobManager;
using jobrunner::JobState;
using jobrunner::Logger;
using jobrunner::testing::FakeProcessLauncher;

namespace {
struct Fixture {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager{store, launcher, logger};
};
const std::string kClient = "pid:1";
}  // namespace

// --- TC-001: envío de trabajos ------------------------------------------------

TEST_CASE("submit válido devuelve un id y lanza el proceso", "[submit][TC-001]") {
    Fixture f;
    auto r = f.manager.submit("echo", {"hola"}, kClient);

    REQUIRE(r.accepted);
    CHECK_FALSE(r.was_duplicate);
    REQUIRE(f.launcher.launches.size() == 1);
    CHECK(f.launcher.launches[0].command == "echo");
    CHECK(f.launcher.launches[0].args == std::vector<std::string>{"hola"});

    auto job = f.manager.status(r.job_id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Running);
    CHECK(job->pid == f.launcher.launches[0].pid);
}

TEST_CASE("Cada submit distinto recibe un id único", "[submit][TC-001]") {
    Fixture f;
    // Comandos diferentes entre sí: iguales caerían en la ventana de duplicados.
    auto a = f.manager.submit("echo", {"uno"}, kClient);
    auto b = f.manager.submit("echo", {"dos"}, kClient);
    auto c = f.manager.submit("true", {}, kClient);

    CHECK(a.job_id != b.job_id);
    CHECK(b.job_id != c.job_id);
    CHECK(a.job_id != c.job_id);
}

TEST_CASE("submit con comando vacío se rechaza con mensaje y no crea un job", "[submit][TC-001]") {
    Fixture f;
    auto r = f.manager.submit("", {}, kClient);

    CHECK_FALSE(r.accepted);
    CHECK_FALSE(r.error_message.empty());
    CHECK(f.launcher.launches.empty());
    CHECK(f.manager.list(std::nullopt).empty());
}

// --- TC-018: solicitudes duplicadas -------------------------------------------

TEST_CASE("Una solicitud repetida del mismo cliente reutiliza el job", "[submit][TC-018]") {
    Fixture f;
    auto first = f.manager.submit("sleep", {"5"}, kClient);
    auto again = f.manager.submit("sleep", {"5"}, kClient);

    CHECK(again.accepted);
    CHECK(again.was_duplicate);
    CHECK(again.job_id == first.job_id);
    CHECK(f.launcher.launches.size() == 1);
}

TEST_CASE("El mismo comando de otro cliente no se considera duplicado", "[submit][TC-018]") {
    Fixture f;
    auto a = f.manager.submit("sleep", {"5"}, "pid:1");
    auto b = f.manager.submit("sleep", {"5"}, "pid:2");

    CHECK_FALSE(b.was_duplicate);
    CHECK(a.job_id != b.job_id);
    CHECK(f.launcher.launches.size() == 2);
}

// --- TC-003 / TC-006: ciclo de vida, tiempos y código de salida ----------------

TEST_CASE("Un proceso que termina con código 0 deja el job en SUCCEEDED", "[lifecycle][TC-003]") {
    Fixture f;
    auto id = f.manager.submit("true", {}, kClient).job_id;
    f.launcher.exit_child(0, 0);

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Succeeded);
    CHECK(job->exit_code == 0);
    CHECK_FALSE(job->exit_signal.has_value());
}

TEST_CASE("Un código distinto de 0 deja el job en FAILED con ese código", "[lifecycle][TC-003]") {
    Fixture f;
    auto id = f.manager.submit("false", {}, kClient).job_id;
    f.launcher.exit_child(0, 1);

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Failed);
    CHECK(job->exit_code == 1);
}

TEST_CASE("Un proceso terminado por señal deja el job en FAILED y registra la señal",
          "[lifecycle][TC-003]") {
    Fixture f;
    auto id = f.manager.submit("sleep", {"100"}, kClient).job_id;
    f.launcher.exit_child(0, -1, SIGKILL);

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Failed);
    CHECK(job->exit_signal == SIGKILL);
}

TEST_CASE("Los tiempos del job son coherentes: recibido <= inicio <= fin", "[lifecycle][TC-003][RF-07]") {
    Fixture f;
    auto id = f.manager.submit("true", {}, kClient).job_id;

    auto running = f.manager.status(id);
    REQUIRE(running.has_value());
    REQUIRE(running->started_at.has_value());
    CHECK_FALSE(running->finished_at.has_value());
    CHECK(running->received_at <= *running->started_at);

    f.launcher.exit_child(0, 0);

    auto done = f.manager.status(id);
    REQUIRE(done.has_value());
    REQUIRE(done->finished_at.has_value());
    CHECK(*done->started_at <= *done->finished_at);
}

TEST_CASE("stdout y stderr se capturan por separado", "[output][TC-006][RF-11]") {
    Fixture f;
    auto id = f.manager.submit("sh", {"-c", "x"}, kClient).job_id;
    f.launcher.emit_output(0, false, "salida ");
    f.launcher.emit_output(0, false, "normal");
    f.launcher.emit_output(0, true, "error");

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->stdout_data == "salida normal");
    CHECK(job->stderr_data == "error");
}

TEST_CASE("list filtra por estado", "[list][TC-004][RF-09]") {
    Fixture f;
    auto a = f.manager.submit("true", {}, kClient).job_id;
    auto b = f.manager.submit("sleep", {"5"}, kClient).job_id;
    f.launcher.exit_child(0, 0);  // termina el primero

    CHECK(f.manager.list(std::nullopt).size() == 2);

    auto running = f.manager.list(JobState::Running);
    REQUIRE(running.size() == 1);
    CHECK(running[0].id == b);

    auto done = f.manager.list(JobState::Succeeded);
    REQUIRE(done.size() == 1);
    CHECK(done[0].id == a);

    CHECK(f.manager.list(JobState::Failed).empty());
}

TEST_CASE("status de un id desconocido no devuelve nada", "[status][TC-004]") {
    Fixture f;
    CHECK_FALSE(f.manager.status(999).has_value());
}

// --- TC-005: cancelación -------------------------------------------------------

TEST_CASE("Cancelar un job en ejecución envía SIGTERM y lo deja en CANCELED", "[cancel][TC-005]") {
    Fixture f;
    auto id = f.manager.submit("sleep", {"100"}, kClient).job_id;
    auto pid = f.launcher.launches[0].pid;

    CHECK(f.manager.cancel(id));
    CHECK(f.launcher.signaled(pid, SIGTERM));

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Canceled);
    CHECK(job->finished_at.has_value());
}

TEST_CASE("Un job cancelado conserva CANCELED cuando su proceso termina después",
          "[cancel][TC-005][RNF-27]") {
    Fixture f;
    auto id = f.manager.submit("sleep", {"100"}, kClient).job_id;
    f.manager.cancel(id);
    f.launcher.exit_child(0, -1, SIGTERM);

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Canceled);
}

TEST_CASE("Cancelar dos veces el mismo job es idempotente", "[cancel][TC-017][RF-26]") {
    Fixture f;
    auto id = f.manager.submit("sleep", {"100"}, kClient).job_id;

    CHECK(f.manager.cancel(id));
    CHECK(f.manager.cancel(id));

    auto job = f.manager.status(id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Canceled);
    CHECK(f.launcher.signals.size() == 1);  // la segunda cancelación no vuelve a señalar
}

TEST_CASE("Cancelar un job ya terminado es un éxito sin cambios", "[cancel][TC-005]") {
    Fixture f;
    auto id = f.manager.submit("true", {}, kClient).job_id;
    f.launcher.exit_child(0, 0);

    CHECK(f.manager.cancel(id));
    CHECK(f.manager.status(id)->state == JobState::Succeeded);
    CHECK(f.launcher.signals.empty());
}

TEST_CASE("Cancelar un id desconocido devuelve false", "[cancel][TC-005]") {
    Fixture f;
    CHECK_FALSE(f.manager.cancel(999));
}
