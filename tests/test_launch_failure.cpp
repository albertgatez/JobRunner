// TC-008 a nivel unitario: error al crear el proceso de un Job (RF-29, RNF-08, RNF-09).
//
// JobManager con un lanzador falso que falla como fork/pipe. Los fallos reales de
// fork() contra el servidor los provoca verif/scripts/tc-008.sh.

#include <catch2/catch_test_macros.hpp>
#include <string>

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
    JobManager manager{store, launcher, logger, 1};
};
const std::string kClient = "pid:1";
}  // namespace

TEST_CASE("TC-008: si falla el lanzamiento con cupo libre, el job queda FAILED",
          "[launch][TC-008]") {
    Fixture f;
    f.launcher.fail_next_launches = 1;

    auto r = f.manager.submit("echo", {"hola"}, kClient);

    REQUIRE(r.accepted);
    auto job = f.manager.status(r.job_id);
    REQUIRE(job.has_value());
    CHECK(job->state == JobState::Failed);
    CHECK(job->diagnostic.find("fork") != std::string::npos);
    CHECK(job->finished_at.has_value());
    CHECK_FALSE(job->started_at.has_value());
    CHECK_FALSE(job->pid.has_value());
}

TEST_CASE("TC-008: el job fallido se retira de la ventana de duplicados", "[launch][TC-008]") {
    Fixture f;
    f.launcher.fail_next_launches = 1;
    auto first = f.manager.submit("echo", {"hola"}, kClient);

    auto second = f.manager.submit("echo", {"hola"}, kClient);

    CHECK_FALSE(second.was_duplicate);
    CHECK(second.job_id != first.job_id);
    CHECK(f.manager.status(second.job_id)->state == JobState::Running);
}

TEST_CASE("TC-008: un lanzamiento fallido no ocupa cupo", "[launch][TC-008]") {
    Fixture f;
    f.launcher.fail_next_launches = 1;
    f.manager.submit("echo", {"uno"}, kClient);

    auto next = f.manager.submit("echo", {"dos"}, kClient);

    CHECK(f.manager.status(next.job_id)->state == JobState::Running);
    CHECK(f.manager.has_live_processes());
}

TEST_CASE("TC-008: un fallo al despachar desde la cola no detiene la cola", "[launch][TC-008]") {
    Fixture f;
    f.manager.submit("sleep", {"1"}, kClient);
    auto queued_a = f.manager.submit("echo", {"a"}, kClient);
    auto queued_b = f.manager.submit("echo", {"b"}, kClient);
    f.launcher.fail_next_launches = 1;

    REQUIRE_NOTHROW(f.launcher.exit_child(0, 0));

    CHECK(f.manager.status(queued_a.job_id)->state == JobState::Failed);
    CHECK(f.manager.status(queued_b.job_id)->state == JobState::Running);
}

TEST_CASE("TC-008: un fallo durante la recuperación no impide arrancar", "[launch][TC-008]") {
    Fixture f;
    auto id = f.store.create("echo", {"hola"});
    f.launcher.fail_next_launches = 1;

    REQUIRE_NOTHROW(f.manager.recover());

    CHECK(f.manager.status(id)->state == JobState::Failed);
}

TEST_CASE("TC-008: cancelar un job que falló al lanzarse no cambia su estado",
          "[launch][TC-008]") {
    Fixture f;
    f.launcher.fail_next_launches = 1;
    auto r = f.manager.submit("echo", {"hola"}, kClient);

    CHECK(f.manager.cancel(r.job_id));

    CHECK(f.manager.status(r.job_id)->state == JobState::Failed);
}
