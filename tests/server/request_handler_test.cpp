// RequestHandler: traduccion JSON <-> JobManager. RF-02, RF-08..RF-10, RNF-08,
// RNF-14, RNF-21. Nunca debe lanzar excepciones ante entradas malas.
#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <nlohmann/json.hpp>

#include "domain/in_memory_job_store.hpp"
#include "domain/job_manager.hpp"
#include "fakes/fake_process_launcher.hpp"
#include "server/request_handler.hpp"

using namespace jobrunner;
using jobrunner::testing::FakeProcessLauncher;
using json = nlohmann::json;

namespace {

struct Fixture {
    InMemoryJobStore store;
    FakeProcessLauncher launcher;
    Logger logger{"/dev/null"};
    JobManager manager{store, launcher, logger};
    RequestHandler handler{manager};

    json call(const json& request) { return json::parse(handler.handle(request.dump(), "pid:1")); }
    json call_raw(const std::string& raw) { return json::parse(handler.handle(raw, "pid:1")); }
};

bool error_contains(const json& resp, const std::string& needle) {
    return resp.value("error", "").find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("JSON mal formado produce un error, no una excepcion", "[handler][invalid]") {
    Fixture f;
    json resp;
    REQUIRE_NOTHROW(resp = f.call_raw("{esto no es json"));
    REQUIRE(resp["ok"] == false);
    REQUIRE(error_contains(resp, "json invalido"));
}

TEST_CASE("solicitud sin 'op' o con 'op' no string se rechaza", "[handler][invalid]") {
    Fixture f;
    REQUIRE(f.call({{"command", "ls"}})["ok"] == false);
    REQUIRE(f.call({{"op", 5}})["ok"] == false);
}

TEST_CASE("operacion desconocida se reporta con su nombre", "[handler][invalid]") {
    Fixture f;
    auto resp = f.call({{"op", "explotar"}});
    REQUIRE(resp["ok"] == false);
    REQUIRE(error_contains(resp, "explotar"));
}

TEST_CASE("submit exitoso devuelve job_id y lanza el proceso", "[handler][submit]") {
    Fixture f;
    auto resp = f.call({{"op", "submit"}, {"command", "echo"}, {"args", {"hola"}}});
    REQUIRE(resp["ok"] == true);
    REQUIRE(resp["job_id"] == 1);
    REQUIRE(resp["was_duplicate"] == false);
    REQUIRE(f.launcher.launches.size() == 1);
    REQUIRE(f.launcher.launches[0].args == std::vector<std::string>{"hola"});
}

TEST_CASE("submit sin args es valido", "[handler][submit]") {
    Fixture f;
    REQUIRE(f.call({{"op", "submit"}, {"command", "true"}})["ok"] == true);
}

TEST_CASE("submit con comando vacio o ausente se rechaza (RF-02)", "[handler][submit]") {
    Fixture f;
    auto empty = f.call({{"op", "submit"}, {"command", ""}});
    REQUIRE(empty["ok"] == false);
    REQUIRE_FALSE(empty["error"].get<std::string>().empty());

    REQUIRE(f.call({{"op", "submit"}})["ok"] == false);
    REQUIRE(f.call({{"op", "submit"}, {"command", 42}})["ok"] == false);
    REQUIRE(f.launcher.launches.empty());
}

TEST_CASE("submit con args que no son strings se rechaza sin lanzar nada", "[handler][submit]") {
    Fixture f;
    json resp;
    REQUIRE_NOTHROW(resp = f.call({{"op", "submit"}, {"command", "echo"}, {"args", {1, 2}}}));
    REQUIRE(resp["ok"] == false);
    REQUIRE(f.launcher.launches.empty());
}

TEST_CASE("status devuelve estado, comando y salidas separadas", "[handler][status]") {
    Fixture f;
    f.call({{"op", "submit"}, {"command", "sh"}});
    f.launcher.emit_output(0, false, "salida");
    f.launcher.emit_output(0, true, "error");

    auto resp = f.call({{"op", "status"}, {"id", 1}});
    REQUIRE(resp["ok"] == true);
    REQUIRE(resp["job"]["state"] == "RUNNING");
    REQUIRE(resp["job"]["command"] == "sh");
    REQUIRE(resp["job"]["stdout"] == "salida");
    REQUIRE(resp["job"]["stderr"] == "error");
    REQUIRE_FALSE(resp["job"].contains("exit_code"));

    f.launcher.exit_child(0, 7);
    resp = f.call({{"op", "status"}, {"id", 1}});
    REQUIRE(resp["job"]["state"] == "FAILED");
    REQUIRE(resp["job"]["exit_code"] == 7);
}

TEST_CASE("status de un job desconocido o con id invalido falla limpiamente", "[handler][status]") {
    Fixture f;
    auto unknown = f.call({{"op", "status"}, {"id", 99}});
    REQUIRE(unknown["ok"] == false);
    REQUIRE(error_contains(unknown, "desconocido"));

    REQUIRE(f.call({{"op", "status"}})["ok"] == false);
    REQUIRE(f.call({{"op", "status"}, {"id", "uno"}})["ok"] == false);
}

TEST_CASE("list sin filtro devuelve todos y con filtro solo los del estado", "[handler][list]") {
    Fixture f;
    f.call({{"op", "submit"}, {"command", "a"}});
    f.call({{"op", "submit"}, {"command", "b"}});
    f.launcher.exit_child(0, 0);

    REQUIRE(f.call({{"op", "list"}})["jobs"].size() == 2);

    auto running = f.call({{"op", "list"}, {"state", "RUNNING"}});
    REQUIRE(running["ok"] == true);
    REQUIRE(running["jobs"].size() == 1);
    REQUIRE(running["jobs"][0]["command"] == "b");

    REQUIRE(f.call({{"op", "list"}, {"state", "SUCCEEDED"}})["jobs"].size() == 1);
}

TEST_CASE("list con estado desconocido se rechaza", "[handler][list]") {
    Fixture f;
    auto resp = f.call({{"op", "list"}, {"state", "VOLANDO"}});
    REQUIRE(resp["ok"] == false);
    REQUIRE(error_contains(resp, "VOLANDO"));
}

TEST_CASE("cancel de un job en ejecucion responde ok y envia SIGTERM", "[handler][cancel]") {
    Fixture f;
    f.call({{"op", "submit"}, {"command", "sleep"}, {"args", {"60"}}});

    auto resp = f.call({{"op", "cancel"}, {"id", 1}});
    REQUIRE(resp["ok"] == true);
    REQUIRE(f.launcher.signals.size() == 1);
    REQUIRE(f.launcher.signals[0].second == SIGTERM);
    REQUIRE(f.call({{"op", "status"}, {"id", 1}})["job"]["state"] == "CANCELED");
}

TEST_CASE("cancel de un job desconocido o sin id falla", "[handler][cancel]") {
    Fixture f;
    REQUIRE(f.call({{"op", "cancel"}, {"id", 5}})["ok"] == false);
    REQUIRE(f.call({{"op", "cancel"}})["ok"] == false);
}
