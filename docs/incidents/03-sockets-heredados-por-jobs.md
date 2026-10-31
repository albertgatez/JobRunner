# INC-03 — Los jobs heredan los sockets del servidor

- Fecha: 2026-10-30
- Síntomas: durante el cierre del servidor, un cliente nuevo se quedó esperando en lugar de recibir un rechazo. Se cortó por timeout a los 2 s (`exit=124`).
- Evidencia recolectada (antes de modificar): en `/proc/<pid>/fd` de un job `sleep 3` aparecen dos sockets del servidor (`5 -> socket:[14871]` y `6 -> socket:[14872]`). Se observó antes de `403e652`; en `80cf42d` esas llamadas siguen igual, pero no se repitió la prueba.
- Hipótesis:
  1. El servidor no cierra el socket al apagar. **Descartada:** `stop_accepting()` lo cierra.
  2. Otro proceso mantiene abierto el socket. **Confirmada:** el job lo heredó.
- Causa raíz: [unix_socket_listener.cpp](../../src/network/unix_socket_listener.cpp) crea los sockets sin `SOCK_CLOEXEC` (líneas 32 y 62), así que sobreviven al `execvp`. Mientras el job siga vivo, el socket acepta conexiones que nadie atiende.
- Corrección / mitigación / aceptación de riesgo: **sin corregir.** Propuesta: añadir `SOCK_CLOEXEC` en `socket()` y `accept4()`.
- Prueba de regresión añadida: ninguna. Propuesta: comprobar que `/proc/<pid>/fd` del job no tiene sockets y que un cliente nuevo durante el cierre falla de inmediato.
- Estado: EN INVESTIGACIÓN
