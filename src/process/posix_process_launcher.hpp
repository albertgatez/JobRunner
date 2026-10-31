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
      * Cierra temporizadores pendientes sin disparar sus callbacks.
      */
    ~PosixProcessLauncher();

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

     /**
      * Lee la huella del proceso desde /proc.
      * @param pid Proceso a identificar.
      * @return Huella o std::nullopt si el proceso no existe.
      */
    std::optional<ProcessFingerprint> fingerprint(pid_t pid) const override;

     /**
      * Envia SIGTERM a los miembros vivos del grupo, espera un periodo de
      * gracia y escala a SIGKILL. Bloquea hasta kOrphanGracePeriod; solo se
      * usa durante la recuperacion, antes de aceptar conexiones.
      * @param pgid Grupo de procesos del job.
      * @param leader Huella registrada del lider.
      * @return Cantidad de procesos terminados.
      */
    std::size_t terminate_orphan_group(pid_t pgid, const ProcessFingerprint& leader) override;

     /**
      * Programa on_fire con un timerfd no bloqueante registrado en el Reactor.
      * @param delay Tiempo de espera antes de disparar.
      * @param on_fire Callback a ejecutar al expirar.
      * @return Identificador para cancelar el temporizador.
      */
    TimerId schedule_once(std::chrono::milliseconds delay,
                          std::function<void()> on_fire) override;

     /**
      * Cancela un temporizador programado con schedule_once().
      * @param id Identificador a cancelar.
      */
    void cancel_timer(TimerId id) override;

   private:
    struct ChildInfo {
        int stdout_fd{-1};
        int stderr_fd{-1};
        OutputCallback on_output;
        ExitCallback on_exit;
    };

    struct PendingTimer {
        int fd{-1};
        std::function<void()> on_fire;
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
    std::unordered_map<TimerId, PendingTimer> timers_;
    TimerId next_timer_id_{1};
};

}  // namespace jobrunner