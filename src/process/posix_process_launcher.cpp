#include "./posix_process_launcher.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace jobrunner {

namespace {
// Marca un fd como no bloqueante.
// fd: descriptor a configurar.
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Tiempo que se le da a un huerfano para atender SIGTERM antes de SIGKILL.
constexpr std::chrono::milliseconds kOrphanGracePeriod{2000};

// Campos de /proc/<pid>/stat que interesan para identificar procesos.
struct ProcStat {
    char state{};
    pid_t pgrp{};
    std::uint64_t start_time{};
};

// Lee estado, grupo y starttime de un proceso.
// pid: proceso a consultar.
std::optional<ProcStat> read_proc_stat(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;

    // El campo 2 (comm) va entre parentesis y puede contener espacios o ')':
    // se parsea a partir del ultimo ')', donde empieza el campo 3 (state).
    auto close_paren = line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 > line.size()) return std::nullopt;
    std::istringstream rest(line.substr(close_paren + 2));

    ProcStat st;
    long ppid = 0;
    rest >> st.state >> ppid >> st.pgrp;
    std::string skip;
    for (int field = 6; field < 22; ++field) rest >> skip;
    rest >> st.start_time;
    if (!rest) return std::nullopt;
    return st;
}

// Identificador del arranque actual del kernel.
std::string read_boot_id() {
    std::ifstream in("/proc/sys/kernel/random/boot_id");
    std::string id;
    std::getline(in, id);
    return id;
}

// Un zombie ya no ejecuta nada: solo espera a ser recolectado.
bool is_alive(const ProcStat& st) { return st.state != 'Z' && st.state != 'X'; }

struct OrphanMember {
    pid_t pid{};
    std::uint64_t start_time{};
};

// Busca en /proc los procesos vivos del grupo `pgid` que pudieron nacer del
// lider del job (iniciados en o despues de `leader_start`).
std::vector<OrphanMember> find_group_members(pid_t pgid, std::uint64_t leader_start) {
    std::vector<OrphanMember> members;
    DIR* dir = opendir("/proc");
    if (!dir) return members;
    const pid_t self = getpid();
    while (dirent* entry = readdir(dir)) {
        char* end = nullptr;
        long value = std::strtol(entry->d_name, &end, 10);
        if (*end != '\0' || value <= 0) continue;
        auto pid = static_cast<pid_t>(value);
        if (pid == self) continue;
        auto st = read_proc_stat(pid);
        if (st && st->pgrp == pgid && st->start_time >= leader_start && is_alive(*st)) {
            members.push_back({pid, st->start_time});
        }
    }
    closedir(dir);
    return members;
}

// Indica si `m` sigue vivo y sigue siendo el mismo proceso.
bool still_running(const OrphanMember& m) {
    auto st = read_proc_stat(m.pid);
    return st && st->start_time == m.start_time && is_alive(*st);
}
}  // namespace

// Registra manejo de SIGCHLD para recoleccion de hijos.
// reactor: loop de eventos central.
PosixProcessLauncher::PosixProcessLauncher(Reactor& reactor) : reactor_(reactor) {
    reactor_.on_signal(SIGCHLD,
                        [this](const struct signalfd_siginfo&) { reap_exited_children(); });
}

// Lanza un proceso hijo con captura de stdout/stderr.
// command: ejecutable/comando.
// args: argumentos del comando.
// on_output: callback por chunks de salida.
// on_exit: callback al finalizar.
pid_t PosixProcessLauncher::launch(const std::string& command,
                                    const std::vector<std::string>& args,
                                    OutputCallback on_output, ExitCallback on_exit) {
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe2(stdout_pipe, O_CLOEXEC) != 0) {
        throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    }
    if (pipe2(stderr_pipe, O_CLOEXEC) != 0) {
        int saved = errno;
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        throw std::runtime_error(std::string("pipe: ") + std::strerror(saved));
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(command.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        throw std::runtime_error(std::string("fork: ") + std::strerror(saved));
    }

    if (pid == 0) {
        setpgid(0, 0);

        // Si el servidor muere de forma abrupta (kill -9, crash), el kernel
        // mata al job en lugar de dejarlo huerfano (RNF-30). Si el padre ya
        // murio antes de llegar aqui, la senal nunca llegaria: se sale.
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent) _exit(127);

        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        execvp(command.c_str(), argv.data());
        _exit(127);
    }

    setpgid(pid, pid);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    int out_fd = stdout_pipe[0];
    int err_fd = stderr_pipe[0];
    set_nonblocking(out_fd);
    set_nonblocking(err_fd);

    ChildInfo info;
    info.stdout_fd = out_fd;
    info.stderr_fd = err_fd;
    info.on_output = std::move(on_output);
    info.on_exit = std::move(on_exit);
    children_[pid] = std::move(info);

    reactor_.add_read(out_fd, [this, pid, out_fd] { drain_pipe(pid, out_fd, false); });
    reactor_.add_read(err_fd, [this, pid, err_fd] { drain_pipe(pid, err_fd, true); });

    return pid;
}

// Drena datos de un pipe de salida del hijo.
// pid: proceso propietario.
// fd: descriptor del pipe.
// is_stderr: true si corresponde a stderr.
void PosixProcessLauncher::drain_pipe(pid_t pid, int fd, bool is_stderr) {
    auto it = children_.find(pid);
    if (it == children_.end()) return;

    std::array<char, 4096> buf{};
    while (true) {
        ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            if (it->second.on_output) {
                it->second.on_output(pid, is_stderr,
                                      std::string_view(buf.data(), static_cast<size_t>(n)));
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }
    }
}

// Recolecta hijos terminados y dispara callbacks de salida.
void PosixProcessLauncher::reap_exited_children() {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        auto it = children_.find(pid);
        if (it == children_.end()) continue;

        drain_pipe(pid, it->second.stdout_fd, false);
        drain_pipe(pid, it->second.stderr_fd, true);

        reactor_.remove(it->second.stdout_fd);
        reactor_.remove(it->second.stderr_fd);
        close(it->second.stdout_fd);
        close(it->second.stderr_fd);

        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        int term_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

        ExitCallback on_exit = std::move(it->second.on_exit);
        children_.erase(it);

        if (on_exit) on_exit(pid, exit_code, term_signal);
    }
}

// Envia senal al grupo de proceso del job.
// pid: proceso lider del grupo.
// signal: numero de senal POSIX.
void PosixProcessLauncher::send_signal(pid_t pid, int signal) {
    ::kill(-pid, signal);
}

// Lee la huella de un proceso.
// pid: proceso a identificar.
std::optional<ProcessFingerprint> PosixProcessLauncher::fingerprint(pid_t pid) const {
    auto st = read_proc_stat(pid);
    if (!st) return std::nullopt;
    return ProcessFingerprint{read_boot_id(), st->start_time};
}

// Termina los restos de un job de una instancia anterior del servidor.
// pgid: grupo de procesos del job.
// leader: huella registrada del lider.
std::size_t PosixProcessLauncher::terminate_orphan_group(pid_t pgid,
                                                         const ProcessFingerprint& leader) {
    // Otro arranque del equipo: ningun proceso de entonces sigue vivo.
    if (leader.boot_id.empty() || leader.boot_id != read_boot_id()) return 0;

    // El PID del lider existe pero con otro starttime: se reciclo, lo que
    // implica que el grupo original desaparecio por completo (el kernel no
    // reasigna un PID mientras siga en uso como id de grupo). Nada es nuestro.
    auto current_leader = read_proc_stat(pgid);
    if (current_leader && current_leader->start_time != leader.start_time) return 0;

    auto members = find_group_members(pgid, leader.start_time);
    for (const auto& m : members) ::kill(m.pid, SIGTERM);

    auto deadline = std::chrono::steady_clock::now() + kOrphanGracePeriod;
    std::vector<OrphanMember> pending = members;
    while (!pending.empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::erase_if(pending, [](const OrphanMember& m) { return !still_running(m); });
    }
    for (const auto& m : pending) {
        if (still_running(m)) ::kill(m.pid, SIGKILL);
    }
    return members.size();
}

}  // namespace jobrunner
