#pragma once

#include <unistd.h>

namespace jobrunner::testing {

// Red de seguridad para pruebas que corren un Reactor real: si algo se
// cuelga (epoll_wait sin timeout), SIGALRM (accion por defecto) termina el
// proceso de prueba y CTest lo reporta como fallo en vez de quedarse
// esperando. SIGALRM no esta bloqueado por Reactor.
class Watchdog {
   public:
    explicit Watchdog(unsigned seconds = 10) { ::alarm(seconds); }
    ~Watchdog() { ::alarm(0); }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
};

}  // namespace jobrunner::testing
