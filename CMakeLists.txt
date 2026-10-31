cmake_minimum_required(VERSION 3.14)
project(JobRunner LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Advertencias estrictas acordadas en ADR-001 (RNF-19).
add_compile_options(-Wall -Wextra -Wpedantic)

option(JOBRUNNER_BUILD_TESTS "Construir las pruebas automatizadas (Catch2)" ON)

include(FetchContent)

# --- Dependencias (ADR-001): nlohmann/json y Catch2, con versión fijada -----
# Si el sistema ya las tiene instaladas se usan; si no, se descargan.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- SQLite (ADR-004): biblioteca del sistema o, si no estan sus cabeceras
# (paquete sqlite-devel / libsqlite3-dev), la amalgamacion oficial fijada.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  FetchContent_Declare(sqlite_amalgamation
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)
  FetchContent_MakeAvailable(sqlite_amalgamation)
  add_library(sqlite3_bundled STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3_bundled SYSTEM PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  # Servidor de un solo hilo (ADR-003) y sin extensiones cargables: no
  # necesita pthread ni libdl. -w: el codigo de SQLite no es nuestro.
  target_compile_definitions(sqlite3_bundled PRIVATE
    SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION)
  target_compile_options(sqlite3_bundled PRIVATE -w)
  add_library(SQLite::SQLite3 ALIAS sqlite3_bundled)
endif()

# --- Núcleo del sistema (todo menos los puntos de entrada) -------------------
add_library(jobrunner_core STATIC
  src/common/logger.cpp
  src/domain/job.cpp
  src/domain/in_memory_job_store.cpp
  src/domain/sqlite_job_store.cpp
  src/domain/job_manager.cpp
  src/io/reactor.cpp
  src/network/connection.cpp
  src/network/unix_socket_listener.cpp
  src/process/posix_process_launcher.cpp
  src/protocol/frame_codec.cpp
  src/server/request_handler.cpp)
target_link_libraries(jobrunner_core PUBLIC nlohmann_json::nlohmann_json SQLite::SQLite3)

add_executable(jobrunner-server src/server/main.cpp)
target_link_libraries(jobrunner-server PRIVATE jobrunner_core)

add_executable(jobrunner-cli src/client/main.cpp src/protocol/frame_codec.cpp)
target_link_libraries(jobrunner-cli PRIVATE nlohmann_json::nlohmann_json)

# --- Pruebas automatizadas (RNF-20): `ctest` desde el directorio de build ----
if(JOBRUNNER_BUILD_TESTS)
  enable_testing()

  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.5.4
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  endif()

  add_executable(jobrunner_tests
    tests/test_job_state.cpp
    tests/test_job_manager.cpp
    tests/test_concurrency_queue.cpp
    tests/test_launch_failure.cpp
    tests/test_sqlite_job_store.cpp
    tests/test_recovery.cpp
    tests/test_orphan_termination.cpp)
  target_link_libraries(jobrunner_tests PRIVATE jobrunner_core Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(jobrunner_tests)

endif()
