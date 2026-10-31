# ADR-006: Cancelación

| Campo          | Valor                                                                           |
| -------------- | ------------------------------------------------------------------------------- |
| Estado         | Implementado (Hito 2; pruebas específicas de cancelación pendientes)             |
| Fecha          | 2026-10-28                                                                      |
| Autor          | Equipo JUAN                                                                     |
| Req. afectados | RF-10, RF-26, RF-29, RF-30, RNF-09, RNF-30, RNF-33, RNF-34                      |
| Tipo           | Cancelación                                                                     |

## Contexto

`JobManager::cancel()` ya existe y cubre dos de los tres casos relevantes:

- Job en `QUEUED`: se retira de la cola y pasa a `CANCELED` de inmediato. No hay proceso al que señalar.
- Job en estado terminal: operación idempotente, no hace nada (RF-26).
- Job en `RUNNING`: envía una sola señal (`SIGTERM`, vía `IProcessLauncher::send_signal`, que internamente ya hace `kill(-pid, signal)` sobre el **grupo de procesos** del Job, no solo sobre el PID líder, el `setpgid(0,0)` de `PosixProcessLauncher`, documentado en ADR-003, existe justamente para esto) y marca el Job como `CANCELED` de inmediato en el store, sin esperar a que el proceso realmente termine.

Lo que falta y lo que en el ADR-003 deja explícitamente pendiente para un ADR futuro de cancelación es qué pasa si el proceso **ignora** el `SIGTERM` (lo tiene bloqueado, está en un cálculo largo sin puntos de interrupción, etc.). RF-30 exige una política de escalamiento documentada y verificable para ese caso. Sin ella, un Job cancelado puede quedar corriendo indefinidamente aunque el cliente ya reciba `CANCELED` como respuesta, una divergencia entre el estado reportado y la realidad del sistema operativo.

El `Reactor` actual (ADR-003) no tiene todavía ningún mecanismo de temporizador: solo atiende descriptores listos para lectura/escritura y señales vía `signalfd`. Cualquier escalamiento con timeout necesita que el event loop pueda "despertar" después de un tiempo determinado sin bloquear la atención de otros clientes o Jobs, esto es un prerrequisito de implementación, no solo una decisión de política.

## Alternativas consideradas

### Alternativa 1. Solo `SIGTERM` (comportamiento actual)

No se agrega escalamiento; `cancel()` se queda como está.

**Ventajas:** ya implementado, simple, respeta la convención POSIX de terminación "amigable".

**Desventajas y riesgos:** no cumple RF-30. Un Job que ignora o bloquea `SIGTERM` nunca termina; el servidor lo reporta como `CANCELED` mientras el proceso sigue vivo y consumiendo recursos, contradice RF-29 (detectar y liberar recursos de procesos) y arriesga RNF-30 (huérfanos) a largo plazo.

### Alternativa 2. `SIGKILL` inmediato, sin `SIGTERM` previo

Cancelar siempre mata con `SIGKILL` directamente.

**Ventajas:** garantiza terminación; implementación trivial, sin necesidad de temporizador.

**Desventajas y riesgos:** `SIGKILL` no puede ser capturado ni ignorado por el proceso hijo, así que nunca tiene oportunidad de liberar sus propios recursos, cerrar archivos abiertos o escribir un resultado parcial coherente antes de morir. Es innecesariamente agresivo cuando el proceso sí responde a una solicitud ordenada, lo cual, es el caso esperado la mayoría de las veces.

### Alternativa 3. `SIGTERM` + timeout configurable + escalamiento a `SIGKILL` (dirigido al grupo de procesos)

Enviar `SIGTERM` primero (como ya ocurre); si el proceso no termina dentro de un timeout configurable, enviar `SIGKILL` al mismo grupo de procesos.

**Ventajas:**

- Da oportunidad de terminación ordenada en el caso común, sin sacrificar la garantía de terminación en el caso en que el Job ignora la señal inicial.
- Reutiliza el mecanismo de señal por grupo (`kill(-pid, signal)`) ya implementado en `PosixProcessLauncher`, que ya cubre también a hijos que el comando del Job haya generado.
- El timeout es un parámetro de configuración, no un valor fijo en código, consistente con cómo ya se configuran `max_concurrent_jobs` y `max_queue_size`.

**Desventajas y riesgos:**

- Requiere un mecanismo de temporizador integrado al event loop (`Reactor`), que hoy no existe, es trabajo de infraestructura adicional, no solo de `JobManager`.
- Introduce un estado intermedio ("cancelación en curso, esperando escalamiento") que debe gestionarse sin violar la regla de que `CANCELED` ya es el estado final reportado al cliente desde el primer `SIGTERM` (ver Decisión).

### Alternativa 4. `SIGTERM` + timeout + `SIGKILL`, dirigido solo al PID líder

Igual que la Alternativa 3, pero sin usar `kill(-pid, ...)` sobre el grupo, solo señalar el PID del proceso líder.

**Ventajas:** ninguna sobre la Alternativa 3; sería un paso atrás respecto a lo que ya implementa `PosixProcessLauncher::send_signal`.

**Desventajas y riesgos:** deja vivos a los procesos hijos que el comando del Job haya generado (p. ej. un script que a su vez lanza subprocesos), contradiciendo el propósito explícito de `setpgid(0,0)` ya presente en el diseño (ADR-003) y arriesgando RNF-30 (huérfanos) incluso en el caso de cancelación exitosa del proceso líder. Se descarta: no aporta nada frente a la Alternativa 3 y renuncia a una protección que el código ya tiene.

## Decisión

Se adopta la **Alternativa 3**. Al cancelar un Job en `RUNNING`:

1. Enviar `SIGTERM` al grupo de procesos del Job (comportamiento ya implementado, sin cambios).
2. Marcar el Job como `CANCELED` de inmediato en el store (comportamiento ya implementado, sin cambios). El cliente no espera a que el proceso termine realmente para recibir esa respuesta; esto es consistente con RF-10 y con el hecho de que el cupo de concurrencia, según ADR-003, se libera por separado en `on_child_exit`, no en `cancel()`.
3. Armar un temporizador de escalamiento asociado al Job, con un timeout configurable (propuesto: 5 segundos por defecto, ajustable igual que `max_concurrent_jobs`).
4. Si `on_child_exit` ocurre antes de que el temporizador expire, cancelar el temporizador: terminación ordenada exitosa, sin acción adicional.
5. Si el temporizador expira sin que `on_child_exit` haya ocurrido, enviar `SIGKILL` al mismo grupo de procesos.
6. El estado final del Job permanece `CANCELED` en ambos casos. El cliente no necesita distinguir si el proceso terminó por `SIGTERM` o por el `SIGKILL` de escalamiento, solo que el Job ya no está en ejecución (RF-10). La señal que realmente lo terminó se registra en `exit_signal` (ya lo hace `on_child_exit` con el `term_signal` reportado por el sistema) para fines de diagnóstico (RF-07, bitácora).

**Prerrequisito de infraestructura:** el `Reactor` necesita un mecanismo de temporizador (p. ej. `timerfd_create` registrado como un descriptor más de `epoll`, consistente con el modelo de un solo hilo de ADR-003) para poder expirar un timeout sin bloquear la atención de otros clientes o Jobs. Esto se implementa como parte de este ADR, no como un cambio aparte a ADR-003, porque es exclusivamente para soportar el escalamiento de cancelación.

### Razones de la decisión

1. **Cumple RF-30 sin renunciar a una terminación ordenada cuando es posible** (a diferencia de la Alternativa 2).
2. **Reutiliza protección ya existente contra huérfanos de sub-procesos** (el grupo de procesos de ADR-003), evitando el retroceso de la Alternativa 4.
3. **No cambia el contrato observable por el cliente.** `CANCELED` sigue siendo inmediato; el escalamiento es un detalle interno de cómo el servidor se asegura de que el proceso realmente termine, consistente con el principio ya establecido en ADR-003 de que el cupo se libera solo cuando el proceso muere de verdad, no cuando se solicita la cancelación.
4. **El timeout configurable evita un valor arbitrario fijo en código**, siguiendo el mismo patrón que `max_concurrent_jobs`/`max_queue_size`.

### Estado de implementación

| Elemento                                                                                   | Estado                |
| ------------------------------------------------------------------------------------------ | --------------------- |
| Envío de `SIGTERM` al grupo de procesos de un Job `RUNNING` cancelado                      | Implementado (Hito 1) |
| `CANCELED` como estado final inmediato en el store tras la solicitud                       | Implementado (Hito 1) |
| `setpgid(0,0)` por Job (grupo de procesos propio)                                          | Implementado (Hito 1) |
| Señal dirigida al grupo completo (`kill(-pid, signal)`), no solo al líder                  | Implementado (Hito 1) |
| Mecanismo de temporizador en `Reactor` mediante `timerfd`                                  | Implementado (Hito 2) |
| Temporizador no bloqueante del `Reactor` mediante `timerfd`                                | Implementado (Hito 2) |
| Timeout de escalamiento                                                                    | Implementado: parámetro interno; 5 s por defecto, sin opción CLI |
| Envío de `SIGKILL` al grupo si vence el timeout y el líder sigue activo                    | Implementado (Hito 2) |
| Cancelación del temporizador cuando `on_child_exit` ocurre antes del timeout               | Implementado (Hito 2) |
| Detección/terminación de descendientes vivos si el líder sale antes del timeout             | Pendiente: actualmente el timer se cancela al recolectar al líder |
| Registro en bitácora del escalamiento a `SIGKILL`                                          | Implementado (Hito 2) |
| Persistencia de la señal final en `exit_signal` para Jobs ya cancelados                    | Pendiente: `on_child_exit` conserva `CANCELED` y no actualiza ese campo |

> **Implementación (Hito 2):** la cancelación solicitada por el cliente usa `timerfd` registrado en el `Reactor`; el valor predeterminado es 5 s. El cierre controlado también aplica `SIGTERM`, espera hasta 5 s y escala a `SIGKILL`, usando `Reactor::poll_once` para seguir recolectando hijos tras detener el event loop. El timeout no tiene aún una opción de configuración en la línea de comandos.

> **Evidencia y limitaciones pendientes:** TC-009 verifica el escalamiento durante el cierre controlado, pero no sustituye una prueba de regresión específica para la cancelación solicitada por un cliente. En esa ruta, si el líder termina antes del timeout, su callback cancela el temporizador sin comprobar si quedaron descendientes vivos. Además, el código actual no persiste `exit_signal` después de que un Job ya pasó a `CANCELED`; por ello, el punto 6 de la decisión aún no está cumplido por completo.

## Consecuencias

### Positivas

- Garantiza que un Job cancelado termine en un tiempo acotado, incluso si ignora `SIGTERM`, cumpliendo RF-30.
- Mantiene la respuesta inmediata al cliente (`CANCELED`) sin introducir una espera sincrónica.
- Protege contra huérfanos generados por subprocesos del comando del Job, al señalar siempre el grupo completo.
- El timeout configurable permite ajustar el comportamiento sin recompilar, igual que el límite de concurrencia.

### Negativas y riesgos

- Requiere agregar temporizadores al `Reactor`, que hoy no existen, es un cambio de infraestructura, no solo de política, y debe probarse con el mismo rigor que el resto del event loop (sin bloquear otros descriptores).
- Un timeout demasiado corto puede escalar a `SIGKILL` procesos que solo necesitaban un poco más de tiempo para una limpieza ordenada (p. ej. flush de buffers); un timeout demasiado largo retrasa la liberación real de recursos. El valor por defecto propuesto (5s) debe validarse con evidencia, no asumirse como correcto sin prueba.
- Si dos solicitudes de cancelación concurrentes llegan sobre el mismo Job mientras el temporizador ya está armado, debe garantizarse que no se arme un segundo temporizador duplicado ni se envíe `SIGKILL` dos veces, debe verificarse explícitamente (RF-26).
- `SIGKILL` no genera un código de salida "normal"; el Job queda en `CANCELED` pero con `exit_signal = SIGKILL`, lo cual debe quedar claramente documentado en la guía técnica para que no se interprete como una terminación anómala no relacionada con la cancelación solicitada.

## Evidencia / prototipo

La validación de la política deberá demostrar:

1. Cancelar un Job que responde a `SIGTERM` con normalidad: confirmar que termina antes del timeout y que el temporizador se cancela sin llegar a enviar `SIGKILL`.
2. Cancelar un Job que bloquea o ignora `SIGTERM` (p. ej. un script que lo captura y no sale): confirmar que, tras el timeout configurado, recibe `SIGKILL` y termina.
3. Confirmar, con `ps`/`pgrep` sobre el grupo de procesos, que no quedan subprocesos vivos del Job después del escalamiento.
4. Confirmar que el Job queda en `CANCELED` en ambos casos (terminación por `SIGTERM` o por escalamiento), y que `exit_signal` refleja la señal real que lo terminó.
5. Enviar dos solicitudes de cancelación concurrentes sobre el mismo Job y confirmar un resultado coherente, sin doble `SIGKILL` ni temporizadores duplicados (RF-26).
6. Medir que el event loop sigue atendiendo otras conexiones y Jobs normalmente mientras un temporizador de escalamiento está pendiente.

La evidencia se almacenará en `verif/results/<run-id>/`, siguiendo el formato acordado en ADR-003. TC-009 cubre la ruta de cierre del servidor; falta un caso dedicado a cancelación individual y a la persistencia de `exit_signal`.

## Requisitos afectados

| Requisito | Relación                                                                                                 |
| --------- | -------------------------------------------------------------------------------------------------------- |
| RF-10     | El resultado de la cancelación (estado final `CANCELED`) se mantiene consistente con o sin escalamiento. |
| RF-26     | Dos cancelaciones concurrentes sobre el mismo Job no deben duplicar el temporizador ni la señal.         |
| RF-29     | El escalamiento a `SIGKILL` asegura la liberación de recursos incluso si el proceso ignora `SIGTERM`.    |
| RF-30     | Este ADR define explícitamente la política de escalamiento exigida.                                      |
| RNF-09    | La terminación forzada de un Job no debe afectar a otros Jobs en ejecución.                              |
| RNF-30    | Reduce el riesgo de procesos huérfanos al garantizar terminación acotada en el tiempo.                   |
| RNF-33    | Las pruebas de estrés deben confirmar liberación de procesos tras el escalamiento.                       |
| RNF-34    | Esta decisión de recuperación/cancelación requiere análisis de riesgo y prueba asociada (ver Evidencia). |