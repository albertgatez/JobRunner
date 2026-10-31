# Arquitectura de JobRunner (J.U.A.N.)

> **Alcance:** describe la arquitectura local implementada en Hito 2. El acceso remoto y el archivo de configuración siguen pendientes.
> **Decisiones de origen:** [ADR-001](../decisions/ADR-001-Lenguaje_y_herramientas.md) (herramientas), [ADR-002](../decisions/ADR-002-Arquitectura.md) (arquitectura), [ADR-003](../decisions/ADR-003-Concurrencia.md) (concurrencia), [ADR-004](../decisions/ADR-004-Persistencia.md) (persistencia), [ADR-005](../decisions/ADR-005-Recuperacion.md) (recuperación) y [ADR-006](../decisions/ADR-006-Cancelacion.md) (cancelación). Este documento no las reemplaza: resume cómo quedaron en el código.
> **Documentos relacionados:** [Modelo de estados](modelo-de-estados.md).

## 1. Visión general

JobRunner es un sistema **cliente-servidor**. El servidor es un **monolito modular** de un solo proceso y un solo hilo, que atiende solicitudes con un event loop `epoll`. Cada Job se ejecuta como un **proceso hijo independiente**, no dentro del servidor.

```mermaid
flowchart LR
    CLI["jobrunner-cli<br/>(cliente)"] -- "Unix socket<br/>trama = 4 bytes + JSON" --> NET

    subgraph SRV["jobrunner-server (1 proceso, 1 hilo)"]
        direction TB
        NET["Network/API<br/>UnixSocketListener · Connection<br/>FrameCodec · RequestHandler"]
        JM["Job Manager<br/>JobManager · Job"]
        STORE[("IJobStore<br/>SqliteJobStore")]
        PL["Process Manager<br/>IProcessLauncher<br/>PosixProcessLauncher"]
        LOG["Logger"]
        RX{{"Reactor<br/>epoll + signalfd + timerfd"}}
        NET --> JM
        JM --> STORE
        JM --> PL
        JM -.-> LOG
        RX -. "eventos de fd y señales" .-> NET
        RX -. "salida, señales y timeouts" .-> PL
    end

    PL -- "fork / execvp<br/>pipes stdout y stderr" --> P1["Job 1<br/>(proceso hijo)"]
    PL --> P2["Job N<br/>(proceso hijo)"]
```

Dos ideas guían el diseño:

- **El cliente no ejecuta nada.** Solo envía solicitudes y muestra respuestas; el servidor sigue funcionando aunque el cliente se desconecte.
- **El dominio no conoce la infraestructura.** `JobManager` no sabe de JSON ni de sockets, y accede al almacenamiento y a los procesos solo mediante interfaces.

## 2. Módulos

| Módulo (ADR-002) | Clases | Ubicación | Responsabilidad |
|---|---|---|---|
| **Network/API** | `UnixSocketListener`, `Connection`, `FrameCodec`, `RequestHandler` | `src/network/`, `src/protocol/`, `src/server/request_handler.*` | Acepta clientes, arma y valida tramas, traduce JSON ↔ llamadas al dominio y responde. |
| **Event loop** | `Reactor` | `src/io/` | Único bucle del servidor: `epoll` para sockets, pipes y temporizadores; `signalfd` para `SIGCHLD`, `SIGINT` y `SIGTERM`. |
| **Job Manager** | `JobManager`, `Job` | `src/domain/` | Reglas de negocio: validación, detección de duplicados, estados, cancelación. |
| **Persistencia** | `IJobStore`, `SqliteJobStore` | `src/domain/` | Guarda Jobs y resultados en SQLite con actualizaciones transaccionales. `InMemoryJobStore` se conserva para pruebas. |
| **Process Manager** | `IProcessLauncher`, `PosixProcessLauncher` | `src/process/` | `fork`/`execvp`, captura separada de stdout y stderr, señales al grupo de procesos, temporizadores de cancelación y detección de la salida del hijo. |
| **Logging** | `Logger` | `src/common/` | Bitácora con marca de tiempo e ID de Job. |
| **Cliente** | `main.cpp` + `FrameCodec` | `src/client/` | CLI: arma la solicitud, la envía y muestra la respuesta. |

El punto de entrada del servidor (`src/server/main.cpp`) construye los objetos, los conecta y entrega el control al `Reactor`.

## 3. Dependencias y límites entre capas

```mermaid
flowchart TB
    A["Network/API<br/>(sabe de JSON y sockets)"] --> B["JobManager<br/>(reglas de negocio)"]
    B --> C["IJobStore<br/>(interfaz)"]
    B --> D["IProcessLauncher<br/>(interfaz)"]
    C -.implementa.-> E["SqliteJobStore<br/>(servidor)"]
    C -.implementa.-> H["InMemoryJobStore<br/>(pruebas)"]
    D -.implementa.-> F["PosixProcessLauncher"]
    D -.implementa.-> G["FakeProcessLauncher<br/>(solo pruebas)"]
```

- **Solo `RequestHandler` conoce el esquema JSON.** El dominio trabaja con tipos de C++.
- **`JobManager` depende de abstracciones** (`IJobStore`, `IProcessLauncher`). Se puede sustituir el almacenamiento (por ejemplo, por uno en disco) o el lanzador sin tocar las reglas de negocio.
- **Las pruebas unitarias usan `FakeProcessLauncher`** (`tests/fakes/`), que no hace `fork`: permite simular salidas, códigos y señales de forma determinista.

## 4. Protocolo cliente-servidor

- **Transporte:** Unix socket local (por defecto `/tmp/jobrunner.sock`). El servidor obtiene el origen del cliente con `SO_PEERCRED` (`"pid:N"`).
- **Trama:** prefijo de **4 bytes big-endian** con la longitud, seguido de un JSON UTF-8. Tamaño máximo **1 MiB**; una trama mayor se rechaza.
- **Solicitudes** (campo `op`):

| `op` | Campos | Respuesta |
|---|---|---|
| `submit` | comando y argumentos | `ok`, `job_id`, `was_duplicate` |
| `status` | `id` | `ok`, `job` (id, comando, args, estado, `exit_code`, stdout, stderr) |
| `list` | `state` (opcional) | `ok`, `jobs` |
| `cancel` | `id` | `ok` |

- **Errores:** toda solicitud inválida (JSON mal formado, falta `op`, comando vacío, id desconocido) devuelve `{"ok": false, "error": "..."}` y el servidor sigue operando.

## 5. Flujos principales

### 5.1 Envío de un Job

```mermaid
sequenceDiagram
    participant C as CLI
    participant N as Network/API
    participant M as JobManager
    participant S as IJobStore
    participant L as PosixProcessLauncher
    participant H as Proceso hijo

    C->>N: trama {op: submit, command, args}
    N->>M: submit(command, args, origin)
    M->>M: valida y busca duplicado (ventana de 3 s)
    M->>S: create() → Job en QUEUED
    alt hay cupo
        M->>L: launch(command, args, callbacks)
        L->>H: fork + execvp (pipes stdout/stderr)
        M->>S: update() → RUNNING, pid, started_at
    else no hay cupo
        M->>M: agrega id a la cola FIFO
    end
    M-->>N: job_id
    N-->>C: {ok: true, job_id}
```

### 5.2 Terminación de un Job

```mermaid
sequenceDiagram
    participant H as Proceso hijo
    participant R as Reactor
    participant L as PosixProcessLauncher
    participant M as JobManager

    H-->>R: escribe en el pipe / termina (SIGCHLD)
    R->>L: evento de fd o de señal
    L->>M: on_output(...) / on_exit(código, señal)
    M->>M: RUNNING → SUCCEEDED o FAILED (si no estaba CANCELED)
```

### 5.3 Cancelación

`cancel` envía `SIGTERM` al **grupo de procesos** del Job y lo marca `CANCELED` de inmediato. Un temporizador no bloqueante escala a `SIGKILL` tras 5 segundos si el proceso líder sigue registrado; si el líder termina antes, el temporizador se cancela. Actualmente no se comprueba en esa ruta si quedaron descendientes vivos que ignoraron `SIGTERM` (la ruta de cierre del servidor sí los termina). `on_child_exit` conserva el estado `CANCELED` y libera el cupo cuando recolecta al líder. Cancelar un Job ya terminado es un éxito sin efecto (idempotente). Detalle en el [modelo de estados](modelo-de-estados.md) y [ADR-006](../decisions/ADR-006-Cancelacion.md).

## 5.4 Persistencia y recuperación

`SqliteJobStore` conserva los Jobs en `DATA_DIR/jobrunner.db`, usando transacciones por actualización. Al arrancar, antes de aceptar clientes, `JobManager::recover()` deja intactos los estados terminales, reencola los `QUEUED` en orden de llegada y marca `FAILED` los que quedaron `RUNNING`, con diagnóstico de interrupción. Antes de marcar esos Jobs, intenta terminar los procesos supervivientes del grupo solo cuando su huella de proceso coincide.

## 5.5 Cierre controlado

Al recibir `SIGINT` o `SIGTERM`, el servidor deja de aceptar conexiones, rechaza nuevos Jobs y envía `SIGTERM` a los Jobs en ejecución. Mantiene el reactor activo para recolectar sus procesos durante hasta 5 segundos; después envía `SIGKILL` a los grupos que sigan vivos y espera hasta 1 segundo adicional. Los Jobs `QUEUED` permanecen persistidos para el siguiente arranque. La prueba de regresión es [TC-009](../../verif/test-cases/TC-009.md).

## 6. Modelo de concurrencia

- **Un solo hilo.** El `Reactor` atiende sockets, pipes y señales en el mismo bucle. Las estructuras compartidas (`JobManager`, `IJobStore`, ventana de duplicados) no necesitan *locks* porque nunca se acceden desde dos hilos.
- **Una consecuencia directa:** un callback lento bloquea la atención de todos los clientes, así que ninguna operación del servidor debe bloquear.
- **Un proceso por Job.** El aislamiento de los Jobs lo da el sistema operativo. En el hijo se restablece la máscara de señales antes de `exec` y se crea un grupo de procesos propio (`setpgid`) para poder cancelarlo completo.
- **Cierre ordenado.** `SIGINT`/`SIGTERM` llegan por `signalfd`: el servidor deja de aceptar clientes y sale del bucle.

Justificación y alternativas descartadas: ver [ADR-003](../decisions/ADR-003-Concurrencia.md).

## 7. Estado de implementación

| Elemento | Estado | Nota |
|---|---|---|
| Network/API, `Reactor`, `JobManager`, Process Manager, Logging | Implementado | Hito 2, servicio local de un solo hilo |
| Cola FIFO y límite de concurrencia | Implementado | Predeterminado: 3 Jobs en ejecución y hasta 10 en espera; exceder la capacidad rechaza el envío |
| Persistencia | Implementado | SQLite en `./data/jobrunner.db` por defecto; se configura con `--data-dir` |
| Recuperación tras reinicio | Implementado | Reencola `QUEUED` y marca `RUNNING` como `FAILED` después de limpiar restos verificados |
| Cancelación con escalamiento | Parcial | `SIGTERM`, luego `SIGKILL` tras 5 s mientras el líder sigue activo; la limpieza de descendientes al terminar antes el líder requiere una prueba/corrección |
| Cierre sin procesos huérfanos | Implementado | `SIGTERM`, espera hasta 5 s, `SIGKILL` y recolección |
| Acceso remoto (LAN/VPN) y control de acceso | **Pendiente** | El transporte actual es un Unix socket local |
| Archivo de configuración | **Pendiente** | Se usan argumentos de línea de comandos y valores predeterminados |

Mientras estos elementos estén pendientes, los requisitos que dependen de ellos no pueden considerarse cumplidos (ver tabla de requisitos en ADR-002).

## 8. Estructura del código

```text
src/
├── client/      Cliente CLI (jobrunner-cli)
├── server/      Punto de entrada y RequestHandler
├── domain/      Job, JobManager, IJobStore, SqliteJobStore
├── process/     IProcessLauncher, PosixProcessLauncher
├── io/          Reactor (epoll + signalfd; timerfd a través del launcher)
├── network/     UnixSocketListener, Connection
├── protocol/    FrameCodec
└── common/      Logger
tests/           Pruebas Catch2, por módulo, y dobles de prueba (fakes)
verif/           Casos de prueba, scripts de sistema y evidencia
```

## 9. Evolución prevista

La evolución pendiente incluye transporte remoto, control de acceso y archivo de configuración. Cualquier separación de módulos en procesos distintos requiere un Change Request (ADR-002).
