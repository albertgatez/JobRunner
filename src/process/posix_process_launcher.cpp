#include "./posix_process_launcher.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace jobrunner {

namespace {
// Marca un fd como no bloqueante.
// fd: descriptor a configurar.
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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

}  // namespace jobrunner
