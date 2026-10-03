// TC-003 (RF-06, RNF-27): modelo de estados del Job.
// Prueba unitaria de la matriz completa de transiciones.

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

#include "../src/domain/job.hpp"

using jobrunner::is_valid_transition;
using jobrunner::Job;
using jobrunner::JobState;
using jobrunner::to_string;

namespace {
constexpr JobState kAll[] = {JobState::Queued, JobState::Running, JobState::Succeeded,
                             JobState::Failed, JobState::Canceled};

bool is_terminal(JobState s) {
    return s == JobState::Succeeded || s == JobState::Failed || s == JobState::Canceled;
}
}  // namespace

TEST_CASE("to_string devuelve el nombre de cada estado", "[state][TC-003]") {
    CHECK(std::string(to_string(JobState::Queued)) == "QUEUED");
    CHECK(std::string(to_string(JobState::Running)) == "RUNNING");
    CHECK(std::string(to_string(JobState::Succeeded)) == "SUCCEEDED");
    CHECK(std::string(to_string(JobState::Failed)) == "FAILED");
    CHECK(std::string(to_string(JobState::Canceled)) == "CANCELED");
}

TEST_CASE("Transiciones permitidas desde QUEUED", "[state][TC-003]") {
    CHECK(is_valid_transition(JobState::Queued, JobState::Running));
    CHECK(is_valid_transition(JobState::Queued, JobState::Canceled));
    CHECK_FALSE(is_valid_transition(JobState::Queued, JobState::Queued));
    CHECK_FALSE(is_valid_transition(JobState::Queued, JobState::Succeeded));
    // QUEUED -> FAILED hoy no existe; se necesitará al manejar el fallo de
    // fork/pipe (RF-29). Al agregarla, mover esta aserción a las permitidas.
    CHECK_FALSE(is_valid_transition(JobState::Queued, JobState::Failed));
}

TEST_CASE("Transiciones permitidas desde RUNNING", "[state][TC-003]") {
    CHECK(is_valid_transition(JobState::Running, JobState::Succeeded));
    CHECK(is_valid_transition(JobState::Running, JobState::Failed));
    CHECK(is_valid_transition(JobState::Running, JobState::Canceled));
    CHECK_FALSE(is_valid_transition(JobState::Running, JobState::Running));
    CHECK_FALSE(is_valid_transition(JobState::Running, JobState::Queued));
}

TEST_CASE("Los estados terminales no tienen ninguna transición de salida", "[state][TC-003][RNF-27]") {
    for (JobState from : kAll) {
        if (!is_terminal(from)) continue;
        for (JobState to : kAll) {
            INFO(to_string(from) << " -> " << to_string(to));
            CHECK_FALSE(is_valid_transition(from, to));
        }
    }
}

TEST_CASE("Job::transition_to aplica transiciones válidas", "[state][TC-003]") {
    Job job;
    REQUIRE(job.state == JobState::Queued);

    job.transition_to(JobState::Running);
    CHECK(job.state == JobState::Running);

    job.transition_to(JobState::Succeeded);
    CHECK(job.state == JobState::Succeeded);
}

TEST_CASE("Job::transition_to lanza logic_error ante una transición inválida y conserva el estado",
          "[state][TC-003][RNF-27]") {
    Job job;
    job.transition_to(JobState::Running);
    job.transition_to(JobState::Failed);

    CHECK_THROWS_AS(job.transition_to(JobState::Running), std::logic_error);
    CHECK_THROWS_AS(job.transition_to(JobState::Queued), std::logic_error);
    CHECK(job.state == JobState::Failed);
}
