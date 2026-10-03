// TC-002 a nivel unitario: límite de concurrencia y cola FIFO (RF-03, RF-05, RNF-07).
//
// ESTAS PRUEBAS FALLAN HOY: JobManager aún no limita la concurrencia ni tiene cola
// (tarea técnica "Límite de concurrencia y cola", Issue 4). Es el resultado esperado
// y queda como evidencia de lo pendiente. Pasarán al implementar el Issue 4.
//
// Los nombres empiezan con "TC-002:" para poder filtrarlas en ctest:
//   ctest --test-dir build -R "TC-002"     # solo estas
//   ctest --test-dir build -E "TC-002"     # todas menos estas
//
// kMaxConcurrentJobs es el límite que asume el caso (3). Cuando el Issue 4 defina cómo se
// configura (p. ej. un parámetro del constructor de JobManager), aplicarlo en Fixture.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "../src/common/logger.hpp"
#include "../src/domain/in_memory_job_store.hpp"
#include "../src/domain/job_manager.hpp"
#include "fakes/fake_process_launcher.hpp"

using jobrunner::InMemoryJobStore;
using jobrunner::JobId;
using jobrunner::JobManager;
using jobrunner::JobState;
using jobrunner::Logger;
using jobrunner::testing::FakeProcessLauncher;

namespace {
constexpr std::size_t kMaxConcurrentJobs = 3;
constexpr std::size_t kJobs = 5;

struct Fixture {
    Logger logger{"/dev/null"};
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    JobManager manager{store, launcher, logger};  // TODO(Issue 4): pasar kMaxConcurrentJobs

    // Envía kJobs trabajos distintos (argumentos distintos: evita la ventana de duplicados).
    std::vector<JobId> submit_all() {
        std::vector<JobId> ids;
        for (std::size_t i = 0; i < kJobs; ++i) {
            ids.push_back(manager.submit("sleep", {std::to_string(10 + i)}, "pid:1").job_id);
        }
        return ids;
    }
};
}  // namespace

TEST_CASE("TC-002: no se lanzan más procesos que el límite", "[queue][TC-002]") {
    Fixture f;
    f.submit_all();

    CHECK(f.launcher.launches.size() == kMaxConcurrentJobs);
    CHECK(f.manager.list(JobState::Running).size() == kMaxConcurrentJobs);
    CHECK(f.manager.list(JobState::Queued).size() == kJobs - kMaxConcurrentJobs);
}

TEST_CASE("TC-002: los Jobs excedentes esperan en orden de llegada", "[queue][TC-002]") {
    Fixture f;
    auto ids = f.submit_all();

    for (std::size_t i = 0; i < kMaxConcurrentJobs; ++i) {
        CHECK(f.manager.status(ids[i])->state == JobState::Running);
    }
    for (std::size_t i = kMaxConcurrentJobs; i < kJobs; ++i) {
        CHECK(f.manager.status(ids[i])->state == JobState::Queued);
    }
}

TEST_CASE("TC-002: al terminar un Job se despacha el más antiguo de la cola", "[queue][TC-002]") {
    Fixture f;
    auto ids = f.submit_all();

    f.launcher.exit_child(0, 0);  // termina el primer Job

    CHECK(f.manager.status(ids[0])->state == JobState::Succeeded);
    CHECK(f.manager.status(ids[3])->state == JobState::Running);  // el 4.º sale de la cola
    CHECK(f.manager.status(ids[4])->state == JobState::Queued);   // el 5.º sigue esperando
    CHECK(f.manager.list(JobState::Running).size() == kMaxConcurrentJobs);
}

TEST_CASE("TC-002: cancelar un Job en ejecución libera el cupo", "[queue][TC-002]") {
    Fixture f;
    auto ids = f.submit_all();

    CHECK(f.manager.cancel(ids[0]));

    CHECK(f.manager.status(ids[0])->state == JobState::Canceled);
    CHECK(f.manager.status(ids[3])->state == JobState::Running);
    CHECK(f.manager.status(ids[4])->state == JobState::Queued);
}

TEST_CASE("TC-002: cancelar un Job en cola no lanza ningún proceso", "[queue][TC-002]") {
    Fixture f;
    auto ids = f.submit_all();
    const auto launched_before = f.launcher.launches.size();

    CHECK(f.manager.cancel(ids[kJobs - 1]));

    CHECK(f.manager.status(ids[kJobs - 1])->state == JobState::Canceled);
    CHECK(f.launcher.launches.size() == launched_before);
    CHECK(f.launcher.signals.empty());  // no hay proceso al que enviar señales
}
