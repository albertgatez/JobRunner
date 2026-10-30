# ADR-005: Recuperación

| Campo          | Valor                                                       |
| -------------- | ----------------------------------------------------------- |
| Estado         | Implementado (Hito 2)                                       |
| Fecha          | 2026-10-28 (revisado 2026-10-29)                            |
| Autor          | Equipo JUAN                                                 |
| Req. afectados | RF-06, RF-12, RF-13, RF-14, RNF-10, RNF-30, RNF-31, RNF-34  |
| Tipo           | Recuperación                                                |

## Contexto

ADR-004 decide persistir los Jobs en SQLite. Este ADR define qué hace el servidor con ese historial **al arrancar**, antes de aceptar conexiones nuevas.

El problema no es solo "leer lo que había": es decidir qué significa, para cada Job, que el proceso que lo administraba ya no existe.

- Un Job **terminal** (`SUCCEEDED`, `FAILED`, `CANCELED`) es simplemente historia: su información ya es definitiva y no depende de ningún proceso vivo.
- Un Job **`QUEUED`** nunca llegó a tener un proceso asociado: estaba esperando cupo cuando el servidor se detuvo.
- Un Job **`RUNNING`** sí tenía un proceso hijo (`pid`) y pipes de stdout/stderr registrados en el `Reactor` de la instancia anterior del servidor. Esa instancia ya no existe: su `epoll`, sus pipes y su `signalfd` murieron con ella. El proceso hijo puede seguir vivo (huérfano) o puede haber terminado sin que nadie lo recolectara.

RNF-10 exige explícitamente que, tras un reinicio, el sistema **no reporte como `RUNNING` un proceso que ya no controla**. RNF-31 exige distinguir Jobs pendientes, terminados e interrumpidos, sin reportar una ejecución inexistente. Esto hace que "recargar tal cual" sea insuficiente: los Jobs que estaban `RUNNING` necesitan una reclasificación explícita, no solo una relectura.

## Alternativas consideradas

```
### Alternativa 1. Sin recuperación (estado solo en memoria)

No se lee nada al arrancar; cada reinicio empieza desde cero.

**Ventajas:** ninguna complejidad adicional.

**Desventajas y riesgos:** viola directamente RF-12 y RF-13; es la situación actual con `InMemoryJobStore` y es exactamente lo que Avance 02 pide resolver. Se descarta.
```

### Alternativa 2. Recargar todo tal cual, incluyendo `RUNNING`

Leer el historial persistido y restaurar cada Job con el último estado guardado, sin tocar los que estaban `RUNNING`.

**Ventajas:** la más simple de implementar (una sola lectura, sin lógica de reclasificación).

**Desventajas y riesgos:** afecta a RNF-10 directamente. El servidor reportaría `RUNNING` para un proceso que no supervisa, cuyo PID puede incluso haber sido reasignado por el sistema operativo a otro proceso completamente distinto. Una consulta de estado o un intento de cancelación sobre ese Job operaría sobre información falsa. Se descarta.

### Alternativa 3. Recargar y reclasificar: `QUEUED` se reencola, `RUNNING` se marca interrumpido

Leer el historial persistido y, antes de aceptar conexiones:

- Los Jobs terminales se cargan sin cambios.
- Los Jobs `QUEUED` se reinsertan en la cola FIFO (en el orden de `received_at`), para que compitan de nuevo por cupo bajo el límite de concurrencia de la nueva instancia.
- Los Jobs `RUNNING` se marcan como `FAILED`, con `finished_at` igual al momento de la recuperación y un mensaje de diagnóstico ("interrumpido por reinicio del servicio"), sin intentar verificar ni señalar el PID original.

**Ventajas:**

- Cumple RNF-10 explícitamente: ningún Job queda reportado como `RUNNING` sin un proceso bajo control real.
- Cumple RNF-31: distingue con claridad pendientes (reencolados), terminados (sin cambios) e interrumpidos (`RUNNING` → `FAILED` con diagnóstico).
- No depende de que el PID siga siendo válido ni de que no haya sido reutilizado por el sistema operativo.
- Reutiliza una transición de estado que ya es válida en el modelo (`RUNNING → FAILED`); no requiere agregar un estado nuevo ni una transición nueva a `is_valid_transition()`.

**Desventajas y riesgos:**

- Si el proceso huérfano de un Job `RUNNING` interrumpido sigue vivo en el sistema operativo, el servidor deja de supervisarlo: no lo cancela, no espera su terminación, y no lo vuelve a vincular a ningún Job. Queda como un proceso del sistema operativo fuera del conocimiento de JobRunner (relacionado con RNF-30, cierre sin huérfanos, **ver Consecuencias**).

### Alternativa 4. Reconectar (re-adoptar) los Jobs `RUNNING` por PID

Al arrancar, para cada Job `RUNNING` persistido, comprobar si `/proc/<pid>` existe y reanudar su supervisión (reabrir pipes, re-registrar en el `Reactor`).

**Ventajas:** en teoría conservaría Jobs en ejecución a través de un reinicio.

**Desventajas y riesgos:** se descarta. Los pipes de stdout/stderr de la instancia anterior ya fueron cerrados junto con ese proceso del servidor. No hay manera de "reabrir" un pipe que pertenece a otro proceso ya terminado; solo quedaría acceso al PID, no a su E/S. Además, el sistema operativo puede haber reasignado ese PID a un proceso completamente distinto una vez que el original (sea el Job o el propio servidor) terminó, haciendo que `/proc/<pid>` exista pero no corresponda al Job. Esta alternativa requeriría un mecanismo de identificación más robusto (p. ej. verificar también el nombre del comando o un token único) y aun así seguiría sin poder recuperar la E/S ya perdida. Complejidad no justificada para el alcance del proyecto.

### Alternativa 5. Alternativa 3 + terminar los restos verificados del proceso, y cierre controlado que cancela

Igual que la Alternativa 3 (reclasificar `RUNNING → FAILED`, reencolar `QUEUED`), pero sin abandonar los procesos que hayan sobrevivido, y con un cierre controlado que no deja Jobs `RUNNING`:

- **Cierre controlado** (`SIGINT`/`SIGTERM` al servidor): los Jobs `RUNNING` se cancelan (`CANCELED`, con `finished_at`) y su grupo de procesos recibe `SIGTERM`; el servidor sigue recolectando hijos hasta que terminan, y escala a `SIGKILL` tras un plazo. Los `QUEUED` se conservan.
- **Caída abrupta** (`kill -9`, *crash*): cada Job se lanza con `prctl(PR_SET_PDEATHSIG, SIGKILL)`, así que el kernel mata al proceso líder en el mismo instante en que muere el servidor.
- **Al arrancar**, para cada Job `RUNNING`: se termina lo que quede vivo de su grupo de procesos (p. ej. nietos, que no reciben la señal de `PR_SET_PDEATHSIG`), **solo si se verifica que es el mismo proceso**, y después se marca `FAILED`.

La verificación responde a la objeción de la Alternativa 4 sobre PIDs reciclados: al lanzar cada Job se guarda su **huella** `(boot_id, starttime)`, donde `boot_id` es `/proc/sys/kernel/random/boot_id` y `starttime` es el campo 22 de `/proc/<pid>/stat` (instante de creación del proceso, en ticks desde el arranque). Dentro de un mismo arranque del equipo, un PID reciclado tiene necesariamente otro `starttime`. A diferencia de la Alternativa 4, no se intenta re-adoptar el proceso ni recuperar su E/S: solo se termina.

**Ventajas:** todas las de la Alternativa 3, más: ningún proceso de un Job queda vivo indefinidamente fuera del control de JobRunner (RNF-30), sin riesgo de señalar a un proceso ajeno.

**Desventajas y riesgos:** lógica dependiente de Linux (`/proc`, `prctl`); el arranque puede bloquearse hasta ~2 s por cada Job con procesos huérfanos que ignoren `SIGTERM` (ver Consecuencias).

## Decisión

Se adopta la **Alternativa 5** (la Alternativa 3 complementada para no abandonar procesos). La versión original de este ADR adoptaba la Alternativa 3 sin señalar el PID original; se revisó porque dejaba como riesgo aceptado los huérfanos de RNF-30, y la huella `(boot_id, starttime)` elimina el problema de PIDs reciclados que motivaba esa cautela.

### Al arrancar

Antes de crear el listener (es decir, antes de aceptar conexiones de clientes), `JobManager::recover()`:

1. Leer todos los Jobs persistidos desde `IJobStore` (ADR-004).
2. Para cada Job en estado `QUEUED`: reinsertarlo en `queue_` en orden de `received_at` (más antiguo primero), sin crear un nuevo registro, se usa su `id` existente.
3. Para cada Job en estado `RUNNING`:
   1. Si el equipo no se reinició (`boot_id` coincide), terminar los procesos vivos de su grupo (`pgid` = PID del líder): primero `SIGTERM`, y `SIGKILL` a los que sigan vivos tras 2 s. Un proceso se considera del Job si pertenece al grupo y su `starttime` es igual o posterior al del líder registrado. Si el PID del líder existe pero con otro `starttime`, el PID se recicló, lo que implica que el grupo original desapareció por completo (el kernel no reasigna un PID mientras siga en uso como id de grupo): no se señala nada.
   2. Aplicar la transición `RUNNING → FAILED`, fijar `finished_at` al momento de la recuperación y guardar el diagnóstico "interrumpido por reinicio del servicio" en el campo `diagnostic` del Job (visible en `status`/`list`) y en la bitácora (RF-14), junto con cuántos procesos huérfanos se terminaron.
4. Para cada Job en un estado terminal (`SUCCEEDED`, `FAILED`, `CANCELED`): cargarlo sin modificaciones.
5. El cupo ocupado (`live_processes_` en `JobManager`, que reemplaza a `running_count_`) arranca vacío, consistente con la premisa de que ningún proceso está bajo control al momento de arrancar.
6. Una vez reconstruido el estado, intentar despachar la cola (`dispatch_next_queued()`) respetando el límite de concurrencia configurado para esta instancia, que puede ser distinto al de la instancia anterior.

### En el cierre controlado

Al recibir `SIGINT`/`SIGTERM`, después de dejar de aceptar conexiones (`main.cpp`):

1. `JobManager::begin_shutdown()`: los Jobs `RUNNING` pasan a `CANCELED` con `finished_at` y diagnóstico "cancelado por cierre del servicio", y su grupo recibe `SIGTERM`. Desde ese momento se rechaza cualquier `submit` de clientes aún conectados (RF-15) y la cola deja de despacharse.
2. El event loop sigue girando (`Reactor::poll_once`) hasta que todos los procesos se recolectan con `waitpid`, o hasta 5 s (el mismo plazo propuesto en ADR-006).
3. A los que sigan vivos se les envía `SIGKILL` y se espera 1 s más.
4. Los Jobs `QUEUED` no se tocan: quedan persistidos y se reencolan al siguiente arranque.

Así, un reinicio planificado no deja Jobs `RUNNING` en la base, y la reclasificación a `FAILED` queda reservada a caídas reales.

### En una caída abrupta

Cada proceso hijo ejecuta `prctl(PR_SET_PDEATHSIG, SIGKILL)` antes de `execvp`, y comprueba `getppid()` para cubrir la carrera en la que el servidor muere antes de esa llamada. El kernel mata así al líder del Job junto con el servidor, sin esperar a que este se reinicie. Lo que no cubre (nietos del Job, que no reciben esa señal) lo termina la recuperación del paso 3.

### Razones de la decisión

1. **Cumple RNF-10 sin ambigüedad.** Ningún Job queda reportado como `RUNNING` sin que el servidor realmente lo supervise.
2. **No señala procesos ajenos.** La huella `(boot_id, starttime)` distingue un PID reciclado del proceso original, que era la objeción a la Alternativa 4; y a diferencia de ella no se intenta re-adoptar el proceso ni su E/S, solo terminarlo.
3. **No deja huérfanos (RNF-30).** El cierre controlado espera a sus hijos, `PR_SET_PDEATHSIG` cubre la caída del servidor y la recuperación cubre lo que sobreviva a ambos.
4. **Reutiliza el modelo de estados existente.** `RUNNING → FAILED` ya es una transición válida (ver `is_valid_transition()` en `job.hpp`); no se necesita un estado nuevo tipo "interrumpido" para cumplir este ADR.
5. **Preserva el orden FIFO de la cola.** Los Jobs `QUEUED` no pierden su posición relativa al reencolarse por `received_at`.
6. **Es coherente con ADR-003.** El cupo ya se define en ADR-003 como "procesos realmente vivos, no estado del Job"; al arrancar, no hay ningún proceso vivo bajo control de la nueva instancia, así que un cupo vacío es la única cifra consistente con esa regla.

### Estado de implementación

| Elemento                                                                                   | Estado                |
| ------------------------------------------------------------------------------------------ | --------------------- |
| Lectura de Jobs persistidos antes de aceptar conexiones (`JobManager::recover()`)          | Implementado (Hito 2) |
| Reencolado de Jobs `QUEUED` en orden `received_at`                                         | Implementado (Hito 2) |
| Transición `RUNNING → FAILED` con diagnóstico de interrupción al arrancar                  | Implementado (Hito 2) |
| Huella `(boot_id, starttime)` por Job y terminación verificada de restos de su grupo       | Implementado (Hito 2) |
| `PR_SET_PDEATHSIG` en cada Job (el kernel lo mata si el servidor muere)                    | Implementado (Hito 2) |
| Carga sin cambios de Jobs terminales                                                       | Implementado (Hito 2) |
| Cupo (`live_processes_`) vacío tras recuperación                                           | Implementado (Hito 2) |
| Registro en bitácora del evento de recuperación (reencolados/interrumpidos/huérfanos)      | Implementado (Hito 2) |
| Cierre controlado: `RUNNING → CANCELED` + `SIGTERM`, espera y escalamiento a `SIGKILL`     | Implementado (Hito 2) |
| Rechazo de `submit` durante el cierre (RF-15)                                              | Implementado (Hito 2) |

## Consecuencias

### Positivas

- El servidor nunca reporta un Job como `RUNNING` sin supervisarlo realmente (RNF-10).
- El historial completo (incluyendo Jobs interrumpidos) queda disponible para consulta después de un reinicio (RF-12, RF-13).
- La reclasificación es determinista y no depende de inspeccionar el sistema operativo ni de suposiciones sobre PIDs.
- Los Jobs que esperaban cupo no se pierden: vuelven a competir por ejecución en la nueva instancia.

### Negativas y riesgos

- Los restos de un Job solo se terminan si el servidor vuelve a arrancar. Entre la caída y el reinicio, los nietos de un Job (no su líder, que muere por `PR_SET_PDEATHSIG`) siguen vivos.
- Riesgo residual de identificación: si el líder ya no existe y el grupo original desapareció por completo, el PID pudo reciclarse para un proceso que a su vez creó su propio grupo con hijos, todos posteriores al líder original; esos procesos cumplirían el criterio de pertenencia. Requiere que todo eso ocurra entre la caída y el reinicio; se considera aceptable.
- El arranque puede bloquearse hasta ~2 s por cada Job interrumpido cuyos restos ignoren `SIGTERM`, antes de aceptar conexiones.
- `PR_SET_PDEATHSIG` no se conserva al ejecutar un binario *setuid*/*setgid*; esos Jobs dependen solo de la recuperación al arrancar.
- Toda la lógica de identificación depende de Linux (`/proc`, `prctl`), consistente con el alcance del proyecto (ADR-001).
- Un Job cancelado por el cliente cuyo proceso seguía vivo (ignorando `SIGTERM`) cuando el servidor cayó queda `CANCELED` y no se revisa al arrancar; solo lo cubre `PR_SET_PDEATHSIG`. El escalamiento de ADR-006 reduce esa ventana.
- Si el reencolado de `QUEUED` no respeta el orden original, se podría alterar la equidad FIFO documentada en ADR-003; debe verificarse explícitamente en las pruebas.
- Un cambio en `max_concurrent_jobs` entre instancias (p. ej. se arranca con un límite menor) puede dejar más Jobs en `QUEUED` de los que había antes del reinicio (comportamiento esperado), pero debe documentarse para que no se interprete como una pérdida de información.

## Evidencia / prototipo

La validación deberá demostrar progresivamente:

1. Persistir Jobs en los tres grupos de estado (terminal, `QUEUED`, `RUNNING`) y reiniciar el servidor (`kill -9` seguido de un nuevo arranque, simulando una interrupción no controlada).
2. Confirmar que los Jobs terminales se leen sin cambios.
3. Confirmar que los Jobs `QUEUED` se reencolan en el mismo orden relativo y vuelven a competir por cupo.
4. Confirmar que los Jobs `RUNNING` quedan en `FAILED` con `finished_at` posterior al reinicio y un mensaje de diagnóstico distinguible.
5. Confirmar, con `ps`, que tras un `kill -9` al servidor el líder de cada Job muere de inmediato (`PR_SET_PDEATHSIG`), y que al reiniciar se terminan los nietos que hubieran quedado (p. ej. `sh -c 'sleep 300 & wait'`); y que un proceso con el mismo PID pero otro `starttime`, o de otro arranque del equipo, no recibe señales (pruebas `[orphan]`).
6. Confirmar que el cupo arranca vacío y que el despacho posterior respeta el límite configurado de la nueva instancia.
7. Detener el servidor con `SIGTERM` con Jobs `RUNNING` y `QUEUED`: los primeros quedan `CANCELED` con `finished_at` y sin procesos vivos; los segundos siguen `QUEUED` y se ejecutan al reiniciar.

La evidencia se almacenará en `verif/results/<run-id>/`, siguiendo el formato acordado en ADR-003.

## Requisitos afectados

| Requisito | Relación                                                                                                                      |
| --------- | ----------------------------------------------------------------------------------------------------------------------------- |
| RF-06     | Los estados reclasificados (`QUEUED`, `FAILED` por interrupción) siguen siendo válidos dentro del modelo de 5 estados.        |
| RF-12     | La recuperación es la contraparte de la persistencia: lee lo conservado tras el reinicio.                                     |
| RF-13     | Define explícitamente cómo se "recupera el historial... y se marcan coherentemente los trabajos interrumpidos".               |
| RF-14     | El evento de recuperación y la reclasificación de cada Job se registran en la bitácora.                                       |
| RNF-10    | Ningún Job interrumpido queda reportado como `RUNNING` tras el reinicio.                                                      |
| RNF-30    | Cierre controlado que espera a los Jobs, `PR_SET_PDEATHSIG` y terminación verificada de restos al arrancar.                    |
| RNF-31    | Distingue explícitamente pendientes, terminados e interrumpidos sin reportar ejecución inexistente.                           |
| RNF-34    | Esta decisión de consistencia/recuperación requiere análisis de riesgo y prueba asociada (ver Evidencia).                     |