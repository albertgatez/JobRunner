#pragma once

#include <sys/types.h>

#include <functional>
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
};

}  // namespace jobrunner