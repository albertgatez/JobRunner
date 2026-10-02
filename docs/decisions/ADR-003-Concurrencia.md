# ADR-003 — Concurrencia

| Campo          | Valor                                                                                                                                        |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Estado         | Aprobado                                                                                                                                     |
| Fecha          | 2026-10-01                                                                                                                                   |
| Autor          | Equipo JUAN                                                                                                                                  |
| Req. afectados | RF-03, RF-04, RF-05, RF-06, RF-10, RF-22, RF-25, RF-26, RF-29, RF-30, RNF-04, RNF-07, RNF-08, RNF-09, RNF-27, RNF-29, RNF-30, RNF-33, RNF-34 |
| Tipo           | Arquitectura / Concurrencia                                                                                                                  |
## Contexto

JobRunner debe permitir ejecutar múltiples Jobs de manera concurrente sin bloquear la recepción de nuevas solicitudes.

El sistema debe:

- Mantener una cola de Jobs pendientes.
- Ejecutar varios Jobs simultáneamente.
- Respetar un límite configurable de Jobs activos.
- Evitar crear una cantidad ilimitada de procesos o hilos.
- Permitir consultar Jobs mientras otros están ejecutándose.
- Permitir cancelar Jobs en ejecución.
- Detectar la terminación de procesos hijos.
- Liberar recursos después de terminar cada Job.
- Mantener estados consistentes bajo operaciones concurrentes.
- Continuar funcionando cuando un cliente se desconecta.
- Evitar que un Job defectuoso afecte a otros Jobs.
- Manejar correctamente solicitudes concurrentes sobre un mismo Job.

El diseño debe distinguir dos problemas diferentes:

1. **Concurrencia de Jobs:** cómo ejecutar los programas solicitados por el usuario.
2. **Concurrencia de atención del servidor:** cómo atender conexiones, solicitudes y eventos mientras los Jobs se ejecutan.

Los mecanismos `select`, `poll` y `epoll` son mecanismos de multiplexación de I/O y no sustituyen por sí mismos a los procesos o hilos utilizados para ejecutar Jobs. Por ello, se consideran como mecanismos complementarios dentro de las alternativas.

## Alternativas consideradas

### Alternativa 1 — Un proceso por Job

Cada Job aceptado es ejecutado mediante un proceso hijo independiente.

```text
JobRunner Server
      │
      ├── Job 1 → Process 1
      ├── Job 2 → Process 2
      ├── Job 3 → Process 3
      └── Job 4 → Process 4
```

El servidor mantiene la cola y crea procesos únicamente cuando existe capacidad disponible.

**Ventajas:**

- Aislamiento natural entre Jobs.
- Un fallo de un proceso no debería terminar el servidor.
- El sistema operativo administra los procesos independientemente.
- Facilita la obtención del código de salida.
- Permite utilizar señales para cancelación.
- Es adecuado para ejecutar comandos externos.
- Facilita la liberación de recursos asociados al Job.

**Desventajas y riesgos:**

- Crear procesos tiene un costo mayor que crear hilos.
- Una política incorrecta puede generar demasiados procesos.
- Es necesario realizar `wait`/`waitpid` correctamente.
- Se deben evitar procesos huérfanos o zombies.
- Se requiere controlar grupos de procesos cuando un Job puede generar descendientes.
- La cantidad de procesos debe estar limitada.

### Alternativa 2 — Un hilo por Job

Cada Job sería ejecutado desde un hilo independiente dentro del proceso del servidor.

```text
JobRunner Server
      │
      ├── Thread 1 → Job 1
      ├── Thread 2 → Job 2
      ├── Thread 3 → Job 3
      └── Thread 4 → Job 4
```

**Ventajas:**

- Crear y administrar hilos suele tener menor costo que crear procesos.
- Los hilos comparten memoria.
- La comunicación entre hilos puede realizarse mediante estructuras internas.
- Puede simplificar algunas operaciones de coordinación.

**Desventajas y riesgos:**

- Un error de memoria en un Job puede afectar todo el proceso del servidor.
- Los Jobs no quedan tan aislados como con procesos separados.
- Los accesos concurrentes a memoria requieren sincronización.
- Un Job mal diseñado podría bloquear o afectar otros componentes.
- El control de procesos externos seguiría siendo necesario para ejecutar comandos del sistema.
- Aumenta el riesgo de condiciones de carrera.
- Un número elevado de hilos puede consumir recursos excesivos.

### Alternativa 3 — Multiplexación de I/O con `select`, `poll` o `epoll`

Utilizar un mecanismo de multiplexación para atender múltiples sockets y eventos desde un mismo flujo de ejecución.

```text
                 ┌── Client 1
                 │
Event Loop ──────┼── Client 2
                 │
                 └── Client 3
```

Los mecanismos considerados son:

- `select`
- `poll`
- `epoll`

**Ventajas:**

- Permite atender múltiples conexiones sin crear un hilo por cliente.
- Reduce el número de hilos utilizados para I/O.
- `poll` y especialmente `epoll` son adecuados para múltiples descriptores en Linux.
- Facilita la gestión de conexiones concurrentes.
- Puede reducir el overhead de crear un hilo por conexión.

**Desventajas y riesgos:**

- No resuelve por sí mismo la ejecución concurrente de Jobs.
- La implementación del event loop es más compleja.
- Las operaciones bloqueantes deben controlarse cuidadosamente.
- El diseño puede resultar innecesariamente complejo para una cantidad pequeña de clientes.
- Requiere integrar correctamente sockets, procesos, pipes y eventos.
- `epoll` introduce una dependencia específica de Linux, aunque Linux ya es la plataforma objetivo.

### Alternativa 4 — Modelo híbrido con hilos (procesos para Jobs + hilos o `poll` para la red)

Separar la concurrencia de atención del servidor de la concurrencia de ejecución de Jobs.

Propuesta:

```text
                    ┌───────────────┐
                    │ CLI Clients   │
                    └───────┬───────┘
                            │
                            ▼
                  ┌──────────────────┐
                  │ Network Handler  │
                  │ threads / poll   │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │   Job Manager    │
                  │  Queue + State   │
                  └────────┬─────────┘
                           │
             ┌─────────────┼─────────────┐
             ▼             ▼             ▼
         Process 1     Process 2     Process 3
           Job 1         Job 2         Job 3
```

En este modelo:

- Los Jobs se ejecutan mediante procesos.
- La atención de clientes utiliza un mecanismo de concurrencia separado.
- La cantidad de Jobs simultáneos está limitada.
- La cola coordina la disponibilidad de ejecución.
- La sincronización protege el estado compartido.

**Ventajas:**

- Aislamiento de Jobs mediante procesos.
- Permite atender solicitudes mientras existen Jobs en ejecución.
- Separa claramente I/O, administración de Jobs y ejecución.
- Permite limitar de manera independiente los Jobs activos.
- Facilita la implementación de cancelación mediante señales.
- Evita crear un hilo dedicado por cada Job.
- Permite utilizar `poll`/`epoll` posteriormente si la carga de conexiones lo requiere.
- Mantiene una arquitectura compatible con el ADR-002.

**Desventajas y riesgos:**

- Mayor complejidad que utilizar solamente procesos.
- Requiere sincronización del estado compartido.
- Requiere coordinación entre eventos de red y procesos hijos.
- Debe evitarse que un hilo de atención bloquee al administrador de Jobs.
- Se deben diseñar cuidadosamente las responsabilidades entre componentes.
- El uso de varios mecanismos de concurrencia aumenta la necesidad de pruebas.

### Alternativa 5 — Un proceso por Job + event loop `epoll` de un solo hilo

Los Jobs se ejecutan como procesos hijos. Un único hilo atiende, mediante `epoll`, las conexiones de clientes, los pipes de stdout/stderr de los Jobs y las señales (`SIGCHLD`, `SIGINT`, `SIGTERM`), que se reciben como eventos de lectura mediante `signalfd`.

```text
                  epoll (un solo hilo)
      ┌────────────────┼──────────────────────┐
      ▼                ▼                      ▼
   sockets        pipes stdout/stderr      signalfd
 (clientes)        (Jobs = procesos)     (SIGCHLD, SIGTERM)
```

**Ventajas:**

- Aislamiento de Jobs mediante procesos.
- Un solo flujo de control: el estado de los Jobs solo se modifica desde el event loop, por lo que no se requieren locks ni hay condiciones de carrera entre hilos (RNF-27).
- La terminación de procesos (`SIGCHLD`), la salida de los Jobs y la red comparten el mismo mecanismo de eventos, lo que simplifica la supervisión (RF-29, RNF-30).
- Evita crear hilos por cliente o por Job.
- Comportamiento determinista y más fácil de probar y de depurar.

**Desventajas y riesgos:**

- Un callback lento o bloqueante detiene la atención de todos los clientes y Jobs.
- No aprovecha varios núcleos para atender solicitudes (suficiente para la carga académica del proyecto).
- Requiere mantener todos los descriptores en modo no bloqueante.
- `epoll` y `signalfd` son específicos de Linux, plataforma objetivo del proyecto.

## Comparación

|Criterio|Procesos|Hilos|`select/poll/epoll`|Híbrido con hilos|Procesos + `epoll` (un hilo)|
|---|--:|--:|--:|--:|--:|
|Aislamiento entre Jobs|Alto|Bajo|No aplica directamente|Alto|Alto|
|Ejecución de comandos externos|Alto|Medio|No aplica directamente|Alto|Alto|
|Manejo de señales|Directo|Indirecto|No aplica directamente|Directo|Directo (`signalfd`)|
|Atención de múltiples clientes|Medio|Alto|Alto|Alto|Alto|
|Sincronización necesaria|Poca|Mucha|No aplica|Mucha|Ninguna (un hilo)|
|Complejidad|Media|Media|Media/Alta|Alta|Media|
|Riesgo de corrupción del servidor|Menor|Mayor|Depende de implementación|Menor|Menor|
|Riesgo de bloqueo por callback lento|Bajo|Bajo|Alto|Medio|Alto|
|Control de Jobs simultáneos|Bueno|Bueno|No resuelve por sí solo|Bueno|Bueno|
|Escalabilidad de conexiones|Media|Media/Alta|Alta|Alta|Alta para la carga del proyecto|
|Facilidad de cancelación|Alta|Media|No aplica directamente|Alta|Alta|
|Adecuado para JobRunner|Sí|Parcial|Complementario|Parcial|**Sí (elegida)**|

## Decisión

Se utilizará un modelo de **procesos para la ejecución de Jobs + event loop `epoll` de un solo hilo para la atención de clientes y eventos del servidor**.

Los Jobs serán ejecutados mediante procesos hijos independientes.

La atención de sockets, pipes, señales y otros eventos del servidor utilizará un único hilo con multiplexación de I/O. La implementación actual utiliza `epoll` y `signalfd`, por lo que se mantendrá este mecanismo como base del modelo de concurrencia del servidor.

No se utilizará un hilo independiente por cada Job como mecanismo principal de ejecución.

En términos de las alternativas anteriores, se elige la **Alternativa 5** (procesos + `epoll` en un solo hilo) y se descarta la Alternativa 4 (híbrido con hilos).

### Razones de la decisión

1. **Consistencia sin locks.** Con un único hilo, el estado de los Jobs solo cambia dentro del event loop. Las transiciones de estado y las cancelaciones concurrentes se serializan por construcción, sin `mutex` ni condiciones de carrera.
2. **Aislamiento.** Cada Job corre en su propio proceso. Un Job defectuoso no puede corromper la memoria del servidor.
3. **Supervisión unificada.** `SIGCHLD` llega por `signalfd` como un evento más del loop, junto con sockets y pipes, lo que evita handlers asíncronos y simplifica liberar recursos y evitar zombies.
4. **Alcance del proyecto.** La carga esperada es académica, por lo que el paralelismo de hilos en el servidor no aporta un beneficio que compense su complejidad (alternativa 4).

**Costo asumido:** un callback que bloquee o tarde demasiado detiene la atención de todos los clientes. Por eso los descriptores deben ser no bloqueantes y el event loop no debe ejecutar operaciones largas.

### Estado de implementación

| Elemento                                                                                           | Estado                      |
| -------------------------------------------------------------------------------------------------- | --------------------------- |
| Event loop `epoll`, `signalfd`, pipes y sockets no bloqueantes                                     | Implementado (Hito 1)       |
| Un proceso por Job con grupo de procesos propio y captura separada de stdout/stderr                | Implementado (Hito 1)       |
| Restablecimiento de la máscara de señales en el hijo antes de `exec`                               | Implementado (Hito 1)       |
| Detección de terminación de hijos y registro del código de salida                                  | Implementado (Hito 1)       |
| Límite configurable `max_concurrent_jobs` y cola con capacidad máxima                              | Pendiente (Hito 2)          |
| Escalamiento de cancelación (`SIGTERM` → `SIGKILL`)                                                | Pendiente (Hito 2, ADR futuro de cancelación) |
| Política de saturación y backpressure                                                              | Pendiente (ADR futuro de saturación)         |
| Manejo de error al crear el proceso (job marcado `FAILED` con diagnóstico, sin quedar en `QUEUED`) | Pendiente (Hito 2)          |
| Cierre controlado sin procesos huérfanos (terminar y recolectar los Jobs en ejecución al apagar)   | Pendiente (Hito 2)          |

### Modelo propuesto

```text
                         CLIENTES
                            │
              ┌─────────────┼─────────────┐
              │             │             │
              ▼             ▼             ▼
          Cliente 1     Cliente 2     Cliente N
              │             │             │
              └─────────────┼─────────────┘
                            │
                            ▼
                  ┌──────────────────┐
                  │ Network/API      │
                  │ Concurrencia I/O │
                  │ epoll            │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │   Job Manager    │
                  │                  │
                  │  Estado + Cola   │
                  └────────┬─────────┘
                           │
                  Límite de concurrencia
                           │
             ┌─────────────┼─────────────┐
             ▼             ▼             ▼
          Process       Process       Process
           Job A         Job B         Job C
             │             │             │
             └─────────────┼─────────────┘
                           ▼
                   Resultados / Estado
```

### Ejecución de Jobs

Cada Job que pase de `QUEUED` a `RUNNING` será asociado con un proceso hijo.

El servidor deberá:

1. Crear el proceso.
2. Configurar stdout y stderr, crear un grupo de procesos propio para el Job y restablecer en el proceso hijo la máscara de señales heredada del servidor antes de `exec`.
3. Registrar el PID asociado.
4. Actualizar el estado a `RUNNING`.
5. Supervisar la terminación.
6. Obtener el código de salida cuando corresponda.
7. Registrar `finished_at`.
8. Actualizar el estado final.
9. Liberar recursos.
10. Intentar ejecutar el siguiente Job de la cola.

No se permitirá crear más procesos de Job que el límite configurado.

Por ejemplo:

```text
max_concurrent_jobs = 3

RUNNING:
  Job A
  Job B
  Job C

QUEUED:
  Job D
  Job E
  Job F
```

Cuando termine uno de los tres Jobs activos:

```text
Job B → SUCCEEDED

RUNNING:
  Job A
  Job C
  Job D
```

El cambio deberá ser coordinado de manera atómica respecto al estado interno del Job Manager.

### Multiplexación de I/O

El event loop será responsable de atender los eventos asociados a:

- Conexiones de clientes.
- Recepción de solicitudes.
- Envío de respuestas.
- stdout de los procesos.
- stderr de los procesos.
- Descriptores asociados con la supervisión de procesos.
- Eventos de cierre o desconexión.

Los descriptores utilizados para I/O deberán operar de forma no bloqueante cuando corresponda.

El event loop no deberá ejecutar operaciones que bloqueen indefinidamente la atención de otros clientes o Jobs.

### Sincronización

Debido a que el modelo principal utiliza un event loop controlado y procesos separados para la ejecución de Jobs, se evitará introducir sincronización entre múltiples hilos mientras no exista una necesidad técnica.

El estado administrado por Job Manager deberá actualizarse de manera consistente dentro del flujo de procesamiento de eventos.

Si posteriormente se incorporan hilos para una necesidad específica, deberán definirse mecanismos de sincronización adecuados y documentarse mediante el cambio correspondiente.

Cuando sea necesario proteger datos compartidos entre hilos en una futura implementación, podrán utilizarse mecanismos como:

- `std::mutex`
- `std::lock_guard`
- `std::unique_lock`
- `std::condition_variable`
- estructuras thread-safe diseñadas específicamente para JobRunner

No se introducirán estos mecanismos únicamente por anticipación si la implementación no requiere múltiples hilos.

### Límite de concurrencia

El número máximo de Jobs simultáneos será configurable.

El Job Manager será el responsable de garantizar:

```text
running_jobs <= max_concurrent_jobs
```

El contador de cupos se calcula a partir de **procesos realmente vivos**, no a partir del estado del Job. Un Job cancelado pasa a `CANCELED` de inmediato, pero su proceso sigue existiendo hasta recibir la señal y ser recolectado con `waitpid`. Por ello, el cupo se libera únicamente al procesar la terminación del proceso (`on_child_exit`), incluso si el Job ya estaba en `CANCELED`, y es en ese momento cuando se despacha el siguiente Job de la cola. Cancelar un Job en `QUEUED` no ocupa cupo: solo se retira de la cola.

La condición deberá mantenerse incluso cuando:

- lleguen varias solicitudes simultáneamente;
- varios Jobs terminen casi al mismo tiempo;
- se soliciten cancelaciones concurrentes;
- existan múltiples clientes;
- se produzcan errores durante la creación del proceso.

### Cola

La cola será utilizada para mantener Jobs aceptados que todavía no cuentan con capacidad de ejecución.

Cuando la capacidad de Jobs activos esté ocupada, un nuevo Job podrá permanecer en `QUEUED` hasta que exista capacidad disponible.

La capacidad máxima de la cola y la política de comportamiento cuando esta se encuentre llena serán definidas en un ADR posterior (saturación, aún no redactado).

No se establecerá en este ADR una política específica de rechazo o backpressure.

### Cancelación

La arquitectura permitirá que el Job Manager solicite la cancelación de un proceso mediante señales.

La política concreta de:

- `SIGTERM`;
- timeout;
- `SIGKILL`;
- grupos de procesos;
- estados finales;
- escalamiento;

será definida en un ADR posterior (cancelación, aún no redactado).

### Supervisión de procesos

El Process Manager deberá detectar:

- terminación normal;
- código de salida distinto de cero;
- terminación por señal;
- terminación inesperada;
- errores durante la creación del proceso.

Cuando un proceso termine, el sistema deberá liberar los recursos asociados y actualizar el estado correspondiente.

No deberán quedar procesos zombies.

La detección de terminación deberá integrarse con el mecanismo de eventos utilizado por el servidor.

### Atención de clientes

El servidor deberá poder atender solicitudes mientras existen Jobs en ejecución.

La atención de clientes no dependerá de esperar sincrónicamente a que termine un Job.

La implementación utilizará el event loop para procesar eventos de múltiples conexiones y recursos asociados.

No se creará un hilo ilimitado por cliente.

La incorporación futura de hilos deberá justificarse mediante una necesidad técnica y documentarse mediante el proceso de cambio correspondiente.

## Reglas de concurrencia

Se establecen las siguientes reglas:

1. Un Job solo puede tener un estado activo a la vez.
2. Un Job no puede pasar de un estado terminal a `RUNNING`.
3. El contador de Jobs activos debe coincidir con los procesos de Job realmente vivos (un Job `CANCELED` cuyo proceso aún no ha sido recolectado sigue ocupando cupo).
4. La creación de un proceso debe respetar el límite de concurrencia.
5. La terminación de un proceso debe liberar su cupo.
6. Dos solicitudes de cancelación sobre el mismo Job deben producir un resultado coherente.
7. Una desconexión del cliente no debe cancelar automáticamente un Job aceptado.
8. Un error de un Job no debe terminar el servidor.
9. Todo proceso creado debe tener un mecanismo de supervisión.
10. Todo recurso asociado a un Job terminado debe ser liberado.
11. El event loop no deberá quedar bloqueado por la ejecución de un Job.
12. Los eventos de un Job no deberán impedir indefinidamente la atención de otros clientes.
13. No se crearán procesos de Job por encima del límite configurado.
14. Los Jobs pendientes no deberán ser ejecutados hasta que exista capacidad disponible.

## Consecuencias

### Positivas

- Los Jobs quedan aislados mediante procesos.
- El servidor puede continuar atendiendo solicitudes durante la ejecución.
- El límite de concurrencia es explícito.
- La cola permite administrar Jobs que esperan capacidad de ejecución.
- La cancelación puede utilizar señales del sistema operativo.
- Se reduce el riesgo de que un Job afecte directamente la memoria del servidor.
- El diseño soporta múltiples clientes.
- `epoll` permite atender múltiples descriptores sin crear un hilo por conexión.
- Se pueden integrar sockets y pipes dentro de un mecanismo común de eventos.
- El modelo es consistente con la implementación local existente.
- Se evita introducir hilos innecesarios en el servidor.
- Facilita demostrar conceptos de procesos, señales, multiplexación de I/O y concurrencia.

### Negativas y riesgos

- La implementación requiere coordinar procesos, sockets, pipes y eventos.
- Un error en el event loop puede afectar la atención de múltiples clientes.
- Deben evitarse operaciones bloqueantes dentro del event loop.
- La supervisión de procesos debe ser rigurosa.
- Deben controlarse procesos zombies y recursos asociados.
- El límite de concurrencia debe aplicarse correctamente bajo solicitudes simultáneas.
- `epoll` limita este mecanismo específico de I/O a plataformas compatibles con Linux.
- Será necesario realizar pruebas de concurrencia y carga.
- Una futura incorporación de hilos aumentaría la complejidad de sincronización y requeriría una revisión de esta decisión.
- Al ser un solo hilo, un callback lento o bloqueante afecta la atención de todos los clientes y Jobs.
- **Máscara de señales heredada (mitigado).** El servidor bloquea `SIGCHLD`, `SIGINT` y `SIGTERM` para usar `signalfd`, y esa máscara se hereda por `fork` y se conserva tras `exec`. Si el hijo no la restablece, el Job ignora `SIGTERM` y la cancelación no surte efecto. `PosixProcessLauncher` ya la restablece en el hijo antes de `exec`. Debe mantenerse una prueba de regresión que cancele un Job en ejecución y compruebe que el proceso realmente terminó.
- **Fusión de `SIGCHLD` (mitigado).** Varias señales `SIGCHLD` pueden fusionarse en una sola lectura de `signalfd`. La recolección de hijos repite `waitpid(-1, WNOHANG)` hasta que no queden hijos terminados, lo que cubre el caso.
- Si falla la creación del proceso (`fork` o `pipe`), el Job ya fue registrado. El Job Manager debe capturar el error, pasar el Job a `FAILED` con un mensaje de diagnóstico, registrarlo en la bitácora y retirarlo de la ventana de duplicados, para que ni quede en `QUEUED` indefinidamente ni se reutilice en un reenvío. Nota: hoy `is_valid_transition` no permite `QUEUED → FAILED`, por lo que al implementarlo habrá que agregar esa transición al modelo de estados. **Pendiente de implementar** (ver Estado de implementación).
- Actualmente el apagado (`SIGINT`/`SIGTERM`) detiene el event loop sin terminar los Jobs en ejecución. Como cada Job tiene su propio grupo de procesos, no recibe `SIGHUP` al cerrar el servidor y podría quedar huérfano (RNF-30). **Pendiente de implementar** (ver Estado de implementación).

## Evidencia / prototipo

La validación del modelo deberá demostrar progresivamente:

1. Ejecutar un Job correctamente.
2. Ejecutar múltiples Jobs simultáneamente cuando el límite lo permita.
3. Verificar que un Job adicional permanece en `QUEUED` cuando se alcanza el límite.
4. Verificar que no se ejecutan más Jobs que el límite configurado.
5. Procesar una consulta mientras existen Jobs en ejecución.
6. Atender múltiples conexiones sin bloquearse esperando la terminación de un Job.
7. Cancelar un Job mientras existen otros Jobs activos.
8. Procesar solicitudes sobre diferentes Jobs mientras otros Jobs se ejecutan.
9. Detectar la terminación normal de un proceso.
10. Detectar una terminación anormal.
11. Confirmar que un Job fallido no afecta a otros Jobs.
12. Confirmar que los procesos terminados no permanecen como zombies.
13. Confirmar la liberación de recursos después de terminar Jobs.
14. Confirmar que una desconexión de cliente no termina el servidor.
15. Confirmar que el límite de procesos se respeta bajo solicitudes concurrentes.
16. Confirmar que stdout y stderr pueden procesarse sin bloquear indefinidamente al servidor.
17. Ejecutar una prueba de carga para observar procesos, memoria, descriptores y sockets.

La evidencia se almacenará en:

```text
verif/results/<run-id>/
```

Deberá incluir:

- Commit probado.
- Sistema operativo.
- Configuración.
- `max_concurrent_jobs`.
- Capacidad de cola.
- Número de clientes.
- Comandos ejecutados.
- Logs.
- Estados observados.
- Recursos utilizados.
- Resultado PASS/FAIL.

Los resultados deberán registrarse en los casos de prueba correspondientes y posteriormente reflejarse en la matriz de trazabilidad.

La existencia de un caso de prueba documentado no implica que el requisito haya sido verificado. El resultado deberá permanecer pendiente hasta contar con evidencia de ejecución correspondiente al commit evaluado.

## Requisitos afectados

|Requisito|Relación|
|---|---|
|RF-03|La cola mantiene Jobs cuando no existe capacidad inmediata.|
|RF-04|Los Jobs se ejecutan mediante procesos independientes.|
|RF-05|El Job Manager aplica el límite de concurrencia.|
|RF-06|La concurrencia debe mantener estados válidos.|
|RF-10|La arquitectura permite cancelar procesos en ejecución.|
|RF-22|La desconexión de un cliente no termina los Jobs aceptados.|
|RF-25|La cola limitada permite rechazar solicitudes cuando está llena.|
|RF-26|El event loop de un solo hilo serializa las cancelaciones concurrentes sobre un mismo Job.|
|RF-29|El Process Manager detecta terminaciones inesperadas.|
|RF-30|El modelo permite implementar una política de escalamiento de cancelación.|
|RNF-04|Permite demostrar al menos tres Jobs simultáneos con límite 3.|
|RNF-07|Garantiza que no se creen más procesos de Job que el límite.|
|RNF-08|Los errores de clientes no deben terminar el servicio.|
|RNF-09|Un Job anormal no debe afectar otros Jobs.|
|RNF-27|El modelo de un solo hilo mantiene la consistencia de estados sin locks.|
|RNF-29|El sistema limita procesos, clientes y otros recursos.|
|RNF-30|El diseño contempla liberación de procesos y recursos; el cierre sin huérfanos está pendiente (ver Estado de implementación).|
|RNF-33|Las pruebas de estrés verifican liberación de recursos.|
|RNF-34|Los riesgos de concurrencia deben tener pruebas asociadas.|
