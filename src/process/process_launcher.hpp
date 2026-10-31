#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jobrunner {

/**
 * Callback ejecutado cuando un proceso hijo termina.
 */
using ExitCallback = std::function<void(pid_t pid, int exit_code, int term_signal)>;

/**
 * Callback ejecutado cuando llega salida de stdout/stderr.
 */
using OutputCallback = std::function<void(pid_t pid, bool is_stderr, std::string_view data)>;

/**
 * Identidad de un proceso que sobrevive al reciclaje de PIDs: dentro de un
 * mismo arranque del equipo (boot_id), el instante de inicio del proceso
 * (starttime) no se repite para el mismo PID.
 */
struct ProcessFingerprint {
    std::string boot_id;         // /proc/sys/kernel/random/boot_id
    std::uint64_t start_time{};  // campo 22 de /proc/<pid>/stat (ticks desde el arranque)
};

/**
 * Interfaz para lanzar y senializar procesos de jobs.
 */
class IProcessLauncher {
   public:
     /**
      * Destructor virtual para uso polimorfico.
      */
    virtual ~IProcessLauncher() = default;

     /**
      * Lanza un proceso hijo.
      * @param command Ejecutable/comando a correr.
      * @param args Argumentos del comando.
      * @param on_output Callback de salida stdout/stderr.
      * @param on_exit Callback al terminar el proceso.
      * @return pid del proceso hijo.
      */
    virtual pid_t launch(const std::string& command, const std::vector<std::string>& args,
                          OutputCallback on_output, ExitCallback on_exit) = 0;

     /**
      * Envia una senal al proceso/grupo objetivo.
      * @param pid Identificador del proceso objetivo.
      * @param signal Numero de senal POSIX.
      */
    virtual void send_signal(pid_t pid, int signal) = 0;

     /**
      * Obtiene la huella de un proceso vivo.
      * @param pid Proceso a identificar.
      * @return Huella del proceso o std::nullopt si ya no existe.
      */
    virtual std::optional<ProcessFingerprint> fingerprint(pid_t pid) const = 0;

     /**
      * Termina lo que siga vivo del grupo de procesos de un job lanzado por
      * una instancia anterior del servidor (ADR-005). Solo senializa procesos
      * cuya identidad es compatible con la huella del lider; si el PID fue
      * reciclado o el equipo se reinicio, no toca nada.
      * @param pgid Grupo de procesos del job (= PID del lider).
      * @param leader Huella registrada del lider al lanzarlo.
      * @return Cantidad de procesos que se encontraron vivos y se terminaron.
      */
    virtual std::size_t terminate_orphan_group(pid_t pgid, const ProcessFingerprint& leader) = 0;
};

}  // namespace jobrunner