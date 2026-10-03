#pragma once

#include <unordered_map>

#include "../io/reactor.hpp"
#include "./process_launcher.hpp"

namespace jobrunner {

/**
 * Implementacion POSIX de IProcessLauncher.
 */
class PosixProcessLauncher : public IProcessLauncher {
   public:
     /**
      * Construye el launcher POSIX.
      * @param reactor Loop de eventos del servidor.
      */
    explicit PosixProcessLauncher(Reactor& reactor);

     /**
      * Lanza un proceso hijo y enlaza callbacks.
      * @param command Ejecutable/comando a correr.
      * @param args Argumentos del comando.
      * @param on_output Callback para salida stdout/stderr.
      * @param on_exit Callback al terminar.
      * @return pid del proceso hijo.
      */
    pid_t launch(const std::string& command, const std::vector<std::string>& args,
                 OutputCallback on_output, ExitCallback on_exit) override;

     /**
      * Envia una senal al grupo del proceso hijo.
      * @param pid Identificador del proceso objetivo.
      * @param signal Senal POSIX a enviar.
      */
    void send_signal(pid_t pid, int signal) override;

   private:
    struct ChildInfo {
        int stdout_fd{-1};
        int stderr_fd{-1};
        OutputCallback on_output;
        ExitCallback on_exit;
    };

    /**
     * Recolecta hijos finalizados y dispara callbacks on_exit.
     */
    void reap_exited_children();

    /**
     * Drena un pipe de salida de un hijo.
     * @param pid Proceso propietario del pipe.
     * @param fd Descriptor del pipe.
     * @param is_stderr true si corresponde a stderr.
     */
    void drain_pipe(pid_t pid, int fd, bool is_stderr);

    Reactor& reactor_;
    std::unordered_map<pid_t, ChildInfo> children_;
};

}  // namespace jobrunner