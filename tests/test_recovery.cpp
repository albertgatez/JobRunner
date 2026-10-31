// Recuperacion al arrancar (ADR-005) y cierre controlado (RF-15, RNF-30) de
// JobManager, con un lanzador falso. El estado "de la instancia anterior" se
// prepara directo en el store, como lo dejaria un servidor que murio.
// TC-007 (RF-12, RF-13, RNF-10), TC-009 (RF-15).

#include <catch2/catch_test_macros.hpp>
#include <csignal>

#include "../src/common/logger.hpp"
#include "../src/domain/in_memory_job_store.hpp"
#include "../src/domain/job_manager.hpp"
#include "fakes/fake_process_launcher.hpp"

using jobrunner::Clock;
using jobrunner::InMemoryJobStore;
using jobrunner::Job;
using jobrunner::JobId;
using jobrunner::JobManager;
using jobrunner::JobState;
using jobrunner::Logger;
using jobrunner::testing::FakeProcessLauncher;

namespace {
const std::string kClient = "pid:1";

// Deja en el store un job RUNNING como lo habria dejado otra instancia.
JobId persisted_running(InMemoryJobStore& store, pid_t pid, std::uint64_t start_time) {
    JobId id = store.create("sleep", {"100"});
    store.update(id, [&](Job& j) {
        j.transition_to(JobState::Running);
        j.started_at = Clock::now();
        j.pid = pid;
        j.pid_start_time = start_time;
        j.boot_id = "boot-anterior";
    });
    return id;
}
}  // namespace

// --- Recuperacion (ADR-005) ---------------------------------------------------

TEST_CASE("recover marca RUNNING como FAILED con diagnostico y termina su proceso",
          "[recovery][TC-007]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobId id = persisted_running(store, 777, 555);
    launcher.orphan_group_size = 2;

    JobManager manager(store, launcher, logger);
    auto report = manager.recover();

    CHECK(report.interrupted == 1);
    CHECK(report.orphans_terminated == 2);
    REQUIRE(launcher.orphan_terminations.size() == 1);
    CHECK(launcher.orphan_terminations[0].first == 777);
    CHECK(launcher.orphan_terminations[0].second.start_time == 555u);
    CHECK(launcher.orphan_terminations[0].second.boot_id == "boot-anterior");

    auto job = manager.status(id);
    CHECK(job->state == JobState::Failed);  // RNF-10: nunca RUNNING sin control
    CHECK(job->finished_at.has_value());
    CHECK(job->diagnostic == "interrumpido por reinicio del servicio");
    CHECK(launcher.launches.empty());  // no se relanza
}

TEST_CASE("recover no intenta terminar procesos sin huella registrada", "[recovery]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobId id = store.create("sleep", {"1"});
    store.update(id, [](Job& j) { j.transition_to(JobState::Running); });

    JobManager manager(store, launcher, logger);
    manager.recover();

    CHECK(launcher.orphan_terminations.empty());
    CHECK(manager.status(id)->state == JobState::Failed);
}

TEST_CASE("recover deja los jobs terminales sin cambios", "[recovery][TC-007]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobId ok = store.create("true", {});
    store.update(ok, [](Job& j) {
        j.transition_to(JobState::Running);
        j.transition_to(JobState::Succeeded);
        j.exit_code = 0;
    });
    JobId canceled = store.create("sleep", {"5"});
    store.update(canceled, [](Job& j) { j.transition_to(JobState::Canceled); });
    auto before_ok = store.get(ok);

    JobManager manager(store, launcher, logger);
    auto report = manager.recover();

    CHECK(report.requeued == 0);
    CHECK(report.interrupted == 0);
    auto after_ok = manager.status(ok);
    CHECK(after_ok->state == JobState::Succeeded);
    CHECK(after_ok->exit_code == 0);
    CHECK(after_ok->finished_at == before_ok->finished_at);
    CHECK(manager.status(canceled)->state == JobState::Canceled);
    CHECK(launcher.launches.empty());
}

TEST_CASE("recover reencola QUEUED en orden de llegada respetando el nuevo limite",
          "[recovery][TC-007]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobId a = store.create("a", {});
    JobId b = store.create("b", {});
    JobId c = store.create("c", {});
    // `a` llego despues que `b` y `c` (p. ej. reloj ajustado): manda received_at.
    store.update(a, [](Job& j) { j.received_at = Clock::now() + std::chrono::seconds(10); });

    JobManager manager(store, launcher, logger, /*max_concurrent_jobs=*/1);
    auto report = manager.recover();

    CHECK(report.requeued == 3);
    REQUIRE(launcher.launches.size() == 1);
    CHECK(launcher.launches[0].command == "b");
    CHECK(manager.status(b)->state == JobState::Running);
    CHECK(manager.status(c)->state == JobState::Queued);
    CHECK(manager.status(a)->state == JobState::Queued);

    launcher.exit_child(0, 0);
    REQUIRE(launcher.launches.size() == 2);
    CHECK(launcher.launches[1].command == "c");
    launcher.exit_child(1, 0);
    REQUIRE(launcher.launches.size() == 3);
    CHECK(launcher.launches[2].command == "a");
}

TEST_CASE("recover: los interrumpidos no ocupan cupo en la nueva instancia",
          "[recovery][TC-007]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    persisted_running(store, 100, 1);
    persisted_running(store, 101, 2);
    store.create("nuevo", {});

    JobManager manager(store, launcher, logger, /*max_concurrent_jobs=*/1);
    manager.recover();

    // Con limite 1 y dos RUNNING previos, el QUEUED arranca igual: el cupo
    // empieza en 0 porque ningun proceso esta bajo control (ADR-005, paso 5).
    REQUIRE(launcher.launches.size() == 1);
    CHECK(launcher.launches[0].command == "nuevo");
}

TEST_CASE("dispatch registra la huella del proceso para una recuperacion futura",
          "[recovery]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager(store, launcher, logger);

    auto r = manager.submit("sleep", {"1"}, kClient);
    auto job = manager.status(r.job_id);
    REQUIRE(job->pid.has_value());
    CHECK(job->pid_start_time == static_cast<std::uint64_t>(*job->pid));
    CHECK(job->boot_id == "fake-boot");
}

// --- Cierre controlado (RF-15, RNF-30) -----------------------------------------

TEST_CASE("begin_shutdown cancela RUNNING con SIGTERM y conserva los QUEUED",
          "[shutdown][TC-009]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager(store, launcher, logger, /*max_concurrent_jobs=*/1);

    JobId running = manager.submit("sleep", {"100"}, kClient).job_id;
    JobId queued = manager.submit("sleep", {"200"}, kClient).job_id;
    pid_t pid = launcher.launches[0].pid;

    manager.begin_shutdown();

    CHECK(launcher.signaled(pid, SIGTERM));
    auto job = manager.status(running);
    CHECK(job->state == JobState::Canceled);
    CHECK(job->finished_at.has_value());
    CHECK(job->diagnostic == "cancelado por cierre del servicio");
    CHECK(manager.status(queued)->state == JobState::Queued);

    // El proceso aun no termina: sigue contando como vivo.
    CHECK(manager.has_live_processes());

    // Al terminar no se despacha la cola: el QUEUED espera al siguiente arranque.
    launcher.exit_child(0, -1, SIGTERM);
    CHECK_FALSE(manager.has_live_processes());
    CHECK(launcher.launches.size() == 1);
    CHECK(manager.status(queued)->state == JobState::Queued);
    CHECK(manager.status(running)->state == JobState::Canceled);
}

TEST_CASE("durante el cierre se rechazan trabajos nuevos", "[shutdown][TC-009]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager(store, launcher, logger);

    manager.begin_shutdown();
    auto r = manager.submit("echo", {"tarde"}, kClient);

    CHECK_FALSE(r.accepted);
    CHECK_FALSE(r.error_message.empty());
    CHECK(store.list(std::nullopt).empty());
    CHECK(launcher.launches.empty());
}

TEST_CASE("begin_shutdown no reescribe un job que el cliente ya cancelo", "[shutdown]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager(store, launcher, logger);

    JobId id = manager.submit("sleep", {"100"}, kClient).job_id;
    manager.cancel(id);
    auto canceled_at = manager.status(id)->finished_at;

    manager.begin_shutdown();

    auto job = manager.status(id);
    CHECK(job->state == JobState::Canceled);
    CHECK(job->finished_at == canceled_at);
    CHECK(job->diagnostic.empty());
}

TEST_CASE("kill_live_processes escala a SIGKILL solo los que siguen vivos",
          "[shutdown][TC-009]") {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager(store, launcher, logger);

    manager.submit("obediente", {}, kClient);
    manager.submit("terco", {}, kClient);
    pid_t obediente = launcher.launches[0].pid;
    pid_t terco = launcher.launches[1].pid;

    manager.begin_shutdown();
    launcher.exit_child(0, -1, SIGTERM);
    launcher.signals.clear();  // el SIGKILL al grupo del lider que termino es de INC-02
    manager.kill_live_processes();

    CHECK_FALSE(launcher.signaled(obediente, SIGKILL));
    CHECK(launcher.signaled(terco, SIGKILL));
    launcher.exit_child(1, -1, SIGKILL);
    CHECK_FALSE(manager.has_live_processes());
}
