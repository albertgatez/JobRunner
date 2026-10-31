// SqliteJobStore (ADR-004): mismo contrato IJobStore que InMemoryJobStore,
// mas lo que solo tiene sentido con persistencia: los datos sobreviven a
// reabrir la base (RF-12), update() es atomico (RNF-11, RNF-28), --reset-db
// y el candado contra una segunda instancia. TC-007, TC-022.

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "../src/domain/sqlite_job_store.hpp"

using jobrunner::Clock;
using jobrunner::Job;
using jobrunner::JobId;
using jobrunner::JobState;
using jobrunner::SqliteJobStore;

namespace {
// Directorio temporal propio de cada prueba, borrado al terminar.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "jobrunner-test-XXXXXX").string();
        path = mkdtemp(tmpl.data());
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string db() const { return (path / "jobrunner.db").string(); }
};
}  // namespace

TEST_CASE("sqlite: create asigna ids crecientes y deja el job en QUEUED", "[sqlite][TC-007]") {
    TempDir dir;
    SqliteJobStore store(dir.db());
    JobId a = store.create("ls", {"-l", "/tmp"});
    JobId b = store.create("echo", {});
    CHECK(a == 1);
    CHECK(b == 2);

    auto job = store.get(a);
    REQUIRE(job.has_value());
    CHECK(job->command == "ls");
    CHECK(job->args == std::vector<std::string>{"-l", "/tmp"});
    CHECK(job->state == JobState::Queued);
    CHECK(job->received_at.time_since_epoch().count() != 0);
    CHECK_FALSE(job->started_at.has_value());
    CHECK_FALSE(job->finished_at.has_value());
    CHECK_FALSE(job->pid.has_value());
    CHECK_FALSE(store.get(99).has_value());
}

TEST_CASE("sqlite: todos los campos sobreviven a reabrir la base", "[sqlite][TC-007]") {
    TempDir dir;
    const auto started = Clock::now();
    const std::string binary_output("a\0b\xff", 4);  // NUL y bytes no UTF-8
    JobId id;
    {
        SqliteJobStore store(dir.db());
        id = store.create("sh", {"-c", "echo \"hola mundo\"; exit 3", ""});
        REQUIRE(store.update(id, [&](Job& j) {
            j.transition_to(JobState::Running);
            j.started_at = started;
            j.pid = 4242;
            j.pid_start_time = 123456789;
            j.boot_id = "boot-abc";
            j.stdout_data = binary_output;
        }));
        REQUIRE(store.update(id, [&](Job& j) {
            j.transition_to(JobState::Failed);
            j.finished_at = started + std::chrono::milliseconds(1500);
            j.exit_code = 3;
            j.exit_signal = 15;
            j.stderr_data = "error\n";
            j.diagnostic = "interrumpido por reinicio del servicio";
        }));
    }

    SqliteJobStore reopened(dir.db());
    auto job = reopened.get(id);
    REQUIRE(job.has_value());
    CHECK(job->command == "sh");
    CHECK(job->args == std::vector<std::string>{"-c", "echo \"hola mundo\"; exit 3", ""});
    CHECK(job->state == JobState::Failed);
    CHECK(job->started_at == started);
    CHECK(job->finished_at == started + std::chrono::milliseconds(1500));
    CHECK(job->exit_code == 3);
    CHECK(job->exit_signal == 15);
    CHECK(job->pid == 4242);
    CHECK(job->pid_start_time == 123456789u);
    CHECK(job->boot_id == "boot-abc");
    CHECK(job->stdout_data == binary_output);
    CHECK(job->stderr_data == "error\n");
    CHECK(job->diagnostic == "interrumpido por reinicio del servicio");
}

TEST_CASE("sqlite: update de un id desconocido devuelve false sin llamar al mutador",
          "[sqlite]") {
    TempDir dir;
    SqliteJobStore store(dir.db());
    bool called = false;
    CHECK_FALSE(store.update(7, [&](Job&) { called = true; }));
    CHECK_FALSE(called);
}

TEST_CASE("sqlite: si el mutador lanza, la fila queda intacta (RNF-28)", "[sqlite][TC-022]") {
    TempDir dir;
    SqliteJobStore store(dir.db());
    JobId id = store.create("true", {});

    // Cambia un campo y despues intenta una transicion ilegal: nada debe quedar.
    REQUIRE_THROWS_AS(store.update(id,
                                   [](Job& j) {
                                       j.stdout_data = "parcial";
                                       j.transition_to(JobState::Succeeded);
                                   }),
                      std::logic_error);

    auto job = store.get(id);
    CHECK(job->state == JobState::Queued);
    CHECK(job->stdout_data.empty());
    // La base sigue utilizable tras el rollback.
    CHECK(store.update(id, [](Job& j) { j.transition_to(JobState::Running); }));
}

TEST_CASE("sqlite: list filtra por estado y ordena por id", "[sqlite]") {
    TempDir dir;
    SqliteJobStore store(dir.db());
    JobId a = store.create("a", {});
    JobId b = store.create("b", {});
    JobId c = store.create("c", {});
    store.update(b, [](Job& j) { j.transition_to(JobState::Running); });

    auto all = store.list(std::nullopt);
    REQUIRE(all.size() == 3);
    CHECK(all[0].id == a);
    CHECK(all[1].id == b);
    CHECK(all[2].id == c);

    auto queued = store.list(JobState::Queued);
    REQUIRE(queued.size() == 2);
    CHECK(queued[0].id == a);
    CHECK(queued[1].id == c);

    auto running = store.list(JobState::Running);
    REQUIRE(running.size() == 1);
    CHECK(running[0].id == b);
    CHECK(store.list(JobState::Canceled).empty());
}

TEST_CASE("sqlite: los ids no se reutilizan al reabrir la base", "[sqlite]") {
    TempDir dir;
    {
        SqliteJobStore store(dir.db());
        store.create("a", {});
        store.create("b", {});
    }
    SqliteJobStore reopened(dir.db());
    CHECK(reopened.create("c", {}) == 3);
}

TEST_CASE("sqlite: reset borra todo y reinicia los ids", "[sqlite][reset]") {
    TempDir dir;
    {
        SqliteJobStore store(dir.db());
        store.create("a", {});
        store.create("b", {});
        CHECK(store.reset() == 2);
        CHECK(store.list(std::nullopt).empty());
        CHECK(store.create("c", {}) == 1);
    }
    // El reinicio es persistente.
    SqliteJobStore reopened(dir.db());
    auto all = reopened.list(std::nullopt);
    REQUIRE(all.size() == 1);
    CHECK(all[0].command == "c");
}

TEST_CASE("sqlite: una segunda instancia sobre la misma base es rechazada", "[sqlite]") {
    TempDir dir;
    SqliteJobStore first(dir.db());
    CHECK_THROWS_AS(SqliteJobStore(dir.db()), std::runtime_error);
}

TEST_CASE("sqlite: el candado se libera al cerrar la base", "[sqlite]") {
    TempDir dir;
    { SqliteJobStore first(dir.db()); }
    CHECK_NOTHROW(SqliteJobStore(dir.db()));
}

TEST_CASE("sqlite: rechaza una base con un esquema mas nuevo", "[sqlite]") {
    TempDir dir;
    { SqliteJobStore store(dir.db()); }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dir.db().c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "PRAGMA user_version = 99", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    CHECK_THROWS_AS(SqliteJobStore(dir.db()), std::runtime_error);
}

TEST_CASE("sqlite: 500 jobs sin perdida de metadatos (RNF-06)", "[sqlite][RNF-06]") {
    TempDir dir;
    {
        SqliteJobStore store(dir.db());
        for (int i = 0; i < 500; ++i) store.create("echo", {std::to_string(i)});
    }
    SqliteJobStore reopened(dir.db());
    auto all = reopened.list(std::nullopt);
    REQUIRE(all.size() == 500);
    for (int i = 0; i < 500; ++i) {
        CHECK(all[i].args == std::vector<std::string>{std::to_string(i)});
    }
}
