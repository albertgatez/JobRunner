#include "./sqlite_job_store.hpp"

#include <fcntl.h>
#include <sqlite3.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jobrunner {

namespace {

// Version del esquema guardada en PRAGMA user_version. Si cambia la tabla,
// se incrementa y apply_schema() migra desde la version anterior.
constexpr int kSchemaVersion = 1;

constexpr const char* kSchema = R"sql(
CREATE TABLE jobs (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    command        TEXT    NOT NULL,
    args           TEXT    NOT NULL,
    state          TEXT    NOT NULL
                   CHECK (state IN ('QUEUED','RUNNING','SUCCEEDED','FAILED','CANCELED')),
    received_at    INTEGER NOT NULL,
    started_at     INTEGER,
    finished_at    INTEGER,
    exit_code      INTEGER,
    exit_signal    INTEGER,
    pid            INTEGER,
    pid_start_time INTEGER,
    boot_id        TEXT    NOT NULL DEFAULT '',
    stdout_data    TEXT    NOT NULL DEFAULT '',
    stderr_data    TEXT    NOT NULL DEFAULT '',
    diagnostic     TEXT    NOT NULL DEFAULT ''
);
CREATE INDEX idx_jobs_state ON jobs(state);
)sql";

// Orden de columnas compartido por todos los SELECT (ver row_to_job).
constexpr const char* kSelectColumns =
    "SELECT id, command, args, state, received_at, started_at, finished_at, exit_code, "
    "exit_signal, pid, pid_start_time, boot_id, stdout_data, stderr_data, diagnostic "
    "FROM jobs";

// Lanza excepcion con el ultimo error de SQLite.
// db: conexion que fallo.
// what: operacion que se intentaba.
[[noreturn]] void throw_sqlite(sqlite3* db, const std::string& what) {
    throw std::runtime_error("sqlite " + what + ": " + sqlite3_errmsg(db));
}

// Ejecuta SQL sin parametros ni resultados.
// db: conexion.
// sql: sentencias a ejecutar.
void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw_sqlite(db, sql);
}

// Los instantes se guardan como nanosegundos desde epoch (UTC): ida y vuelta
// sin perdida y ordenables como enteros.
std::int64_t to_db(TimePoint tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

TimePoint from_db(std::int64_t ns) {
    return TimePoint(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns)));
}

// Sentencia preparada con liberacion automatica.
class Statement {
   public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw_sqlite(db, "prepare");
        }
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, std::int64_t value) { check(sqlite3_bind_int64(stmt_, index, value)); }

    void bind(int index, const std::string& value) {
        check(sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT));
    }

    template <typename T>
    void bind(int index, const std::optional<T>& value) {
        if (value) {
            bind(index, static_cast<std::int64_t>(*value));
        } else {
            check(sqlite3_bind_null(stmt_, index));
        }
    }

    void bind(int index, const std::optional<TimePoint>& value) {
        bind(index, value ? std::optional<std::int64_t>(to_db(*value)) : std::nullopt);
    }

    // Avanza una fila. true si hay fila disponible, false al terminar.
    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw_sqlite(db_, "step");
    }

    bool is_null(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }
    std::int64_t int64(int col) const { return sqlite3_column_int64(stmt_, col); }

    // Usa column_bytes para conservar bytes NUL en la salida de los jobs.
    std::string text(int col) const {
        const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
        int size = sqlite3_column_bytes(stmt_, col);
        return data ? std::string(data, static_cast<std::size_t>(size)) : std::string();
    }

    template <typename T>
    std::optional<T> optional(int col) const {
        if (is_null(col)) return std::nullopt;
        return static_cast<T>(int64(col));
    }

   private:
    void check(int rc) {
        if (rc != SQLITE_OK) throw_sqlite(db_, "bind");
    }

    sqlite3* db_;
    sqlite3_stmt* stmt_{nullptr};
};

// Transaccion de escritura: si no se confirma, se revierte al destruirse
// (p. ej. cuando el mutador de update() lanza una transicion invalida).
class Transaction {
   public:
    explicit Transaction(sqlite3* db) : db_(db) { exec(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        exec(db_, "COMMIT");
        committed_ = true;
    }

   private:
    sqlite3* db_;
    bool committed_{false};
};

// Convierte la fila actual de un SELECT kSelectColumns en un Job.
// row: sentencia posicionada en una fila.
Job row_to_job(const Statement& row) {
    Job job;
    job.id = static_cast<JobId>(row.int64(0));
    job.command = row.text(1);
    job.args = nlohmann::json::parse(row.text(2)).get<std::vector<std::string>>();
    auto state = parse_job_state(row.text(3));
    if (!state) throw std::runtime_error("estado desconocido en la base: " + row.text(3));
    job.state = *state;
    job.received_at = from_db(row.int64(4));
    if (!row.is_null(5)) job.started_at = from_db(row.int64(5));
    if (!row.is_null(6)) job.finished_at = from_db(row.int64(6));
    job.exit_code = row.optional<int>(7);
    job.exit_signal = row.optional<int>(8);
    job.pid = row.optional<pid_t>(9);
    job.pid_start_time = row.optional<std::uint64_t>(10);
    job.boot_id = row.text(11);
    job.stdout_data = row.text(12);
    job.stderr_data = row.text(13);
    job.diagnostic = row.text(14);
    return job;
}

// Reescribe todas las columnas mutables de la fila del job.
// db: conexion.
// id: fila a actualizar (se ignora cualquier cambio de job.id del mutador).
// job: valores nuevos.
void write_job(sqlite3* db, JobId id, const Job& job) {
    Statement stmt(db,
                   "UPDATE jobs SET command = ?1, args = ?2, state = ?3, received_at = ?4, "
                   "started_at = ?5, finished_at = ?6, exit_code = ?7, exit_signal = ?8, "
                   "pid = ?9, pid_start_time = ?10, boot_id = ?11, stdout_data = ?12, "
                   "stderr_data = ?13, diagnostic = ?14 WHERE id = ?15");
    stmt.bind(1, job.command);
    stmt.bind(2, nlohmann::json(job.args).dump());
    stmt.bind(3, std::string(to_string(job.state)));
    stmt.bind(4, to_db(job.received_at));
    stmt.bind(5, job.started_at);
    stmt.bind(6, job.finished_at);
    stmt.bind(7, job.exit_code);
    stmt.bind(8, job.exit_signal);
    stmt.bind(9, job.pid);
    stmt.bind(10, job.pid_start_time);
    stmt.bind(11, job.boot_id);
    stmt.bind(12, job.stdout_data);
    stmt.bind(13, job.stderr_data);
    stmt.bind(14, job.diagnostic);
    stmt.bind(15, static_cast<std::int64_t>(id));
    stmt.step();
}

}  // namespace

// Abre la base, toma el candado de instancia y aplica el esquema.
// db_path: ruta del archivo de base de datos.
SqliteJobStore::SqliteJobStore(const std::string& db_path) {
    // flock en un archivo aparte (no en el .db) para que la base siga
    // pudiendo inspeccionarse con la CLI de sqlite3 mientras el servidor corre.
    const std::string lock_path = db_path + ".lock";
    lock_fd_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd_ < 0) {
        throw std::runtime_error("no se pudo abrir " + lock_path + ": " + std::strerror(errno));
    }
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        int saved = errno;
        close();
        if (saved == EWOULDBLOCK) {
            throw std::runtime_error("otra instancia de JobRunner ya usa " + db_path);
        }
        throw std::runtime_error("flock " + lock_path + ": " + std::strerror(saved));
    }

    try {
        if (sqlite3_open_v2(db_path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            nullptr) != SQLITE_OK) {
            throw_sqlite(db_, "open " + db_path);
        }
        // WAL: una transaccion interrumpida (kill -9, crash) se descarta al
        // reabrir y la base queda consistente (RNF-11, RNF-28). Con
        // synchronous=NORMAL un commit sobrevive a la caida del proceso; ante
        // un corte de energia se podria perder la ultima transaccion, pero
        // nunca se corrompe la base. Se acepta a cambio de no hacer fsync en
        // cada fragmento de salida de un job.
        exec(db_, "PRAGMA journal_mode = WAL");
        exec(db_, "PRAGMA synchronous = NORMAL");
        apply_schema();
    } catch (...) {
        close();
        throw;
    }
}

// Cierra conexion y candado.
SqliteJobStore::~SqliteJobStore() { close(); }

// Libera recursos; tolera llamarse con recursos ya liberados.
void SqliteJobStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    if (lock_fd_ >= 0) {
        ::close(lock_fd_);  // libera el flock
        lock_fd_ = -1;
    }
}

// Crea el esquema en una base nueva y rechaza versiones desconocidas.
void SqliteJobStore::apply_schema() {
    Statement version_stmt(db_, "PRAGMA user_version");
    version_stmt.step();
    const auto version = version_stmt.int64(0);

    if (version == kSchemaVersion) return;
    if (version > kSchemaVersion) {
        throw std::runtime_error("la base usa el esquema v" + std::to_string(version) +
                                 ", esta version de JobRunner solo entiende hasta v" +
                                 std::to_string(kSchemaVersion));
    }
    // version 0: base recien creada.
    Transaction tx(db_);
    exec(db_, kSchema);
    exec(db_, ("PRAGMA user_version = " + std::to_string(kSchemaVersion)).c_str());
    tx.commit();
}

// Inserta un job nuevo en QUEUED.
// command: comando a ejecutar.
// args: argumentos del comando.
JobId SqliteJobStore::create(const std::string& command, const std::vector<std::string>& args) {
    Statement stmt(db_,
                   "INSERT INTO jobs (command, args, state, received_at) VALUES (?1, ?2, ?3, ?4)");
    stmt.bind(1, command);
    stmt.bind(2, nlohmann::json(args).dump());
    stmt.bind(3, std::string(to_string(JobState::Queued)));
    stmt.bind(4, to_db(Clock::now()));
    stmt.step();
    return static_cast<JobId>(sqlite3_last_insert_rowid(db_));
}

// Busca un job por id.
// id: identificador del job.
std::optional<Job> SqliteJobStore::get(JobId id) const {
    Statement stmt(db_, std::string(kSelectColumns) + " WHERE id = ?1");
    stmt.bind(1, static_cast<std::int64_t>(id));
    if (!stmt.step()) return std::nullopt;
    return row_to_job(stmt);
}

// Lee-modifica-escribe un job de forma atomica.
// id: identificador del job.
// mutator: logica de modificacion.
bool SqliteJobStore::update(JobId id, const std::function<void(Job&)>& mutator) {
    Transaction tx(db_);
    auto job = get(id);
    if (!job) return false;  // ~Transaction revierte (no habia cambios)
    mutator(*job);           // si lanza, ~Transaction revierte
    write_job(db_, id, *job);
    tx.commit();
    return true;
}

// Lista jobs con filtro opcional por estado.
// state_filter: estado opcional para filtrar.
std::vector<Job> SqliteJobStore::list(std::optional<JobState> state_filter) const {
    std::string sql = kSelectColumns;
    if (state_filter) sql += " WHERE state = ?1";
    sql += " ORDER BY id";
    Statement stmt(db_, sql);
    if (state_filter) stmt.bind(1, std::string(to_string(*state_filter)));

    std::vector<Job> result;
    while (stmt.step()) result.push_back(row_to_job(stmt));
    return result;
}

// Borra todo el historial y reinicia los ids en 1.
std::size_t SqliteJobStore::reset() {
    Transaction tx(db_);
    exec(db_, "DELETE FROM jobs");
    auto removed = static_cast<std::size_t>(sqlite3_changes(db_));
    exec(db_, "DELETE FROM sqlite_sequence WHERE name = 'jobs'");
    tx.commit();
    return removed;
}

}  // namespace jobrunner
