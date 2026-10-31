// IJobStore (backend en memoria). Apoya RF-08, RF-09.
#include <catch2/catch_test_macros.hpp>

#include "domain/in_memory_job_store.hpp"

using namespace jobrunner;

TEST_CASE("create asigna IDs unicos y crece de uno en uno", "[store]") {
    InMemoryJobStore store;
    JobId a = store.create("echo", {"a"});
    JobId b = store.create("echo", {"b"});
    REQUIRE(a != b);
    REQUIRE(b == a + 1);
}

TEST_CASE("create deja el job en QUEUED con sus datos y hora de recepcion", "[store]") {
    InMemoryJobStore store;
    JobId id = store.create("ls", {"-l", "/tmp"});
    auto job = store.get(id);
    REQUIRE(job.has_value());
    REQUIRE(job->id == id);
    REQUIRE(job->command == "ls");
    REQUIRE(job->args == std::vector<std::string>{"-l", "/tmp"});
    REQUIRE(job->state == JobState::Queued);
    REQUIRE(job->received_at.time_since_epoch().count() != 0);
    REQUIRE_FALSE(job->started_at.has_value());
    REQUIRE_FALSE(job->finished_at.has_value());
}

TEST_CASE("get de un ID desconocido devuelve nullopt", "[store]") {
    InMemoryJobStore store;
    REQUIRE_FALSE(store.get(42).has_value());
}

TEST_CASE("update aplica el mutador y persiste el cambio", "[store]") {
    InMemoryJobStore store;
    JobId id = store.create("sleep", {"1"});
    bool ok = store.update(id, [](Job& j) { j.transition_to(JobState::Running); });
    REQUIRE(ok);
    REQUIRE(store.get(id)->state == JobState::Running);
}

TEST_CASE("update de un ID desconocido devuelve false y no llama al mutador", "[store]") {
    InMemoryJobStore store;
    bool called = false;
    REQUIRE_FALSE(store.update(7, [&](Job&) { called = true; }));
    REQUIRE_FALSE(called);
}

TEST_CASE("get devuelve una copia: modificarla no altera el almacen", "[store]") {
    InMemoryJobStore store;
    JobId id = store.create("true", {});
    auto copy = store.get(id);
    copy->command = "false";
    REQUIRE(store.get(id)->command == "true");
}

TEST_CASE("list filtra por estado y sin filtro devuelve todo", "[store]") {
    InMemoryJobStore store;
    JobId a = store.create("a", {});
    JobId b = store.create("b", {});
    store.create("c", {});
    store.update(a, [](Job& j) { j.transition_to(JobState::Running); });
    store.update(b, [](Job& j) { j.transition_to(JobState::Canceled); });

    REQUIRE(store.list(std::nullopt).size() == 3);

    auto running = store.list(JobState::Running);
    REQUIRE(running.size() == 1);
    REQUIRE(running[0].id == a);

    REQUIRE(store.list(JobState::Queued).size() == 1);
    REQUIRE(store.list(JobState::Canceled).size() == 1);
    REQUIRE(store.list(JobState::Succeeded).empty());
}
