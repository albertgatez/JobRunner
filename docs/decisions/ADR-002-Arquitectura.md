# ADR-002 — Arquitectura

| Campo          | Valor                                                        |
| -------------- | ------------------------------------------------------------ |
| Estado         | Aprobado                                                     |
| Fecha          | 2026-09-30                                                   |
| Autor          | Equipo JUAN                                                  |
| Req. afectados | RF-01..RF-30, RNF-17, RNF-18, RNF-27, RNF-29, RNF-30, RNF-34 |
| Tipo           | Arquitectura                                                 |

## Contexto

JobRunner debe proporcionar una interfaz local y remota para registrar, ejecutar, consultar y cancelar Jobs.

El sistema debe manejar las siguientes responsabilidades:

- Recepción de solicitudes.
- Validación de solicitudes.
- Administración de Jobs.
- Cola de ejecución.
- Control de concurrencia.
- Creación y supervisión de procesos.
- Captura de stdout y stderr.
- Persistencia.
- Recuperación después de reinicio.
- Comunicación de red.
- Cancelación de Jobs.
- Logging.
- Configuración.
- Control de acceso remoto.

La arquitectura debe permitir separar responsabilidades y evitar que una operación de un componente afecte innecesariamente a los demás.

También debe permitir:

- Operación local.
- Operación remota mediante LAN/VPN.
- Múltiples clientes.
- Ejecución de Jobs mediante procesos independientes.
- Persistencia del estado.
- Recuperación después de reinicio.
- Control de recursos.

Se consideran tres alternativas principales:

1. Monolito modular.
2. Componentes separados.
3. Arquitectura cliente-servidor.

La arquitectura define la distribución general del sistema y la separación de responsabilidades. Las decisiones técnicas específicas relacionadas con concurrencia, persistencia, recuperación, cancelación, saturación, comunicación y seguridad serán desarrolladas en los ADR correspondientes.

## Alternativas consideradas

### Alternativa 1 — Monolito modular

Un único proceso contiene los diferentes módulos internos de JobRunner.

```text
JobRunner
├── Request Handler
├── Job Manager
├── Queue Manager
├── Process Manager
├── Persistence
├── Network
├── Configuration
└── Logging
```

Los módulos se comunican mediante interfaces internas.

**Ventajas:**

- Menor complejidad operacional.
- No requiere administrar múltiples servicios.
- Comunicación interna sencilla.
- Facilita la depuración.
- Menor consumo de recursos.
- Adecuado para una aplicación ejecutada en una sola máquina.
- Simplifica el despliegue.

**Desventajas y riesgos:**

- Un error grave en el proceso principal puede afectar diferentes módulos.
- Requiere disciplina para mantener las responsabilidades separadas.
- Un monolito mal estructurado puede convertirse en código difícil de mantener.
- No define por sí mismo una separación entre cliente y servicio.
- El acceso remoto requeriría añadir posteriormente mecanismos de comunicación.

### Alternativa 2 — Componentes separados

Separar las responsabilidades principales en diferentes procesos o servicios.

```text
Client
   │
   ▼
Request Service
   │
   ├── Job Manager
   ├── Execution Manager
   └── Persistence Service
```

**Ventajas:**

- Mayor aislamiento entre componentes.
- Los componentes pueden evolucionar independientemente.
- Algunos fallos pueden quedar contenidos dentro de un componente.
- Permite utilizar mecanismos IPC entre componentes.
- Permite distribuir responsabilidades entre diferentes procesos.

**Desventajas y riesgos:**

- Mayor complejidad de implementación.
- Requiere definir mecanismos adicionales de IPC.
- Aumenta el número de procesos que deben administrarse.
- Aumenta los puntos de fallo.
- Incrementa la complejidad de configuración y recuperación.
- Requiere más pruebas de integración.
- Puede introducir complejidad innecesaria para el alcance actual.
- Incrementa el consumo de recursos.

### Alternativa 3 — Arquitectura cliente-servidor

Separar explícitamente el cliente CLI del servicio JobRunner.

```text
┌──────────────┐
│ CLI Client   │
└──────┬───────┘
       │
       │ Comunicación
       │ 
       ▼
┌──────────────────────┐
│ JobRunner Server     │
│                      │
│ Request Handler      │
│ Job Manager          │
│ Queue                │
│ Process Manager      │
│ Persistence          │
│ Logging              │
└──────────────────────┘
```

**Ventajas:**

- Permite operación local y remota.
- Mantiene el servicio ejecutándose independientemente del cliente.
- Permite múltiples clientes.
- El mismo protocolo puede utilizarse local y remotamente.
- Separa la interfaz de usuario de la lógica del servicio.
- Encaja directamente con los requisitos de acceso remoto.
- Permite que el cliente se desconecte sin detener el servicio.
- Facilita futuras implementaciones de clientes diferentes al CLI.

**Desventajas y riesgos:**

- Requiere implementar un protocolo de comunicación.
- Se deben manejar conexiones parciales.
- Se deben manejar desconexiones.
- Introduce una superficie adicional de seguridad.
- Requiere validación de mensajes.
- Requiere controlar el acceso a la interfaz de red.
- Requiere pruebas adicionales de integración y comunicación.

## Decisión

Se utilizará una **arquitectura cliente-servidor con un servidor implementado como monolito modular**.

Esta decisión combina la separación necesaria entre cliente y servicio con una implementación centralizada que evita introducir procesos adicionales innecesarios.

La arquitectura propuesta será:

```text
                    ┌─────────────────┐
                    │   CLI Client    │
                    └────────┬────────┘
                             │ Comunicación
                             │
                             ▼
┌──────────────────────────────────────────────────────────┐
│                 JobRunner Server                         │
│                                                          │
│  ┌───────────────┐      ┌─────────────────────┐          │
│  │ Network/API   │─────►│    Job Manager      │          │
│  │ (sobre Reactor│      │  (Estado + Cola)    │          │
│  │  epoll, ADR-003)│      └──────────┬──────────┘          │
│  └───────────────┘                 │                     │
│                    ┌───────────────┼──────────┐          │
│                    ▼               ▼          ▼          │
│               ┌────────┐     ┌──────────┐ ┌──────────┐   │
│               │ Queue  │     │ Process  │ │Persistence│  │
│               │(dentro │     │ Manager  │ └──────────┘   │
│               │de Job  │     └──────────┘                │
│               │Manager)│                                 │
│               └────────┘                                 │
│                                                          │
│       Logging / Configuration / Recovery                 │
└──────────────────────────────────────────────────────────┘
```

El mecanismo de transporte y el protocolo de comunicación serán definidos en un ADR posterior (protocolo de comunicación, aún no redactado). La atención de eventos del servidor sigue el modelo de un solo hilo con `epoll` descrito en ADR-003.

El servidor estará dividido en módulos con responsabilidades claramente definidas.

### Network/API

Responsable de:

- Escuchar conexiones.
- Aceptar clientes.
- Recibir solicitudes.
- Validar el protocolo.
- Enviar respuestas.
- Manejar desconexiones.
- Aplicar las restricciones de acceso remoto (Hito 3).

### Job Manager

Responsable de:

- Crear Jobs.
- Mantener su estado.
- Coordinar la cola.
- Solicitar la ejecución.
- Coordinar cancelaciones.
- Actualizar resultados.

### Queue Manager

Responsable de:

- Mantener Jobs pendientes.
- Aportar el contador de Jobs activos para que Job Manager respete el límite de concurrencia (ver ADR-003).
- Gestionar la saturación.
- Rechazar nuevos Jobs cuando se alcance la capacidad.

**Decisión de implementación:** en el Hito 2 la cola vivirá dentro de `JobManager` (contador de Jobs activos y cola FIFO de Jobs en `QUEUED`), como submódulo lógico. Si su complejidad lo justifica (por ejemplo, con la política de saturación, aún no redactada), podrá extraerse a una clase `QueueManager` sin cambiar el protocolo ni los demás módulos.

### Process Manager

Responsable de:

- Crear procesos hijos.
- Supervisarlos.
- Capturar resultados.
- Detectar terminaciones anormales.
- Aplicar cancelaciones.
- Liberar recursos.

### Persistence

Responsable de:

- Guardar metadata.
- Recuperar Jobs.
- Actualizar estados.
- Mantener la consistencia de la información.

### Logging

Responsable de:

- Registrar eventos relevantes.
- Registrar errores.
- Asociar eventos con Job ID.
- Facilitar el diagnóstico de problemas.

### Configuration

Responsable de:

- Cargar la configuración.
- Validar valores.
- Proporcionar límites y parámetros operativos.

### Recovery

Responsable de:

- Reconstruir el estado después de un reinicio.
- Identificar Jobs que estaban en ejecución.
- Marcar coherentemente los Jobs interrumpidos.
- Evitar reportar como `RUNNING` un proceso que el servicio ya no controla.

Los Jobs serán ejecutados mediante procesos independientes y no directamente dentro del proceso principal del servidor.

La separación de módulos será inicialmente lógica. No se crearán procesos separados para cada módulo.

Una separación adicional podrá reconsiderarse mediante un Change Request si existe una necesidad técnica demostrable.

Las decisiones específicas relacionadas con concurrencia, persistencia, recuperación, cancelación, saturación, comunicación y seguridad serán desarrolladas en los ADR correspondientes.

### Estado de implementación

| Módulo                                      | Estado                              | Dónde está en el código                                                                  |
| ------------------------------------------- | ----------------------------------- | ---------------------------------------------------------------------------------------- |
| Network/API                                 | Implementado (Hito 1, solo local)   | `UnixSocketListener`, `Connection`, `FrameCodec`, `RequestHandler`                       |
| Event loop (`Reactor`)                      | Implementado (Hito 1)               | `Reactor` (`epoll` + `signalfd`), ver ADR-003                                            |
| Job Manager                                 | Implementado (Hito 1)               | `JobManager`, `Job`                                                                      |
| Process Manager                             | Implementado (Hito 1)               | `IProcessLauncher`, `PosixProcessLauncher`                                               |
| Persistence                                 | Parcial: solo en memoria            | `IJobStore`, `InMemoryJobStore` (persistencia en disco: Hito 2, ADR futuro)                 |
| Logging                                     | Implementado (Hito 1)               | `Logger`                                                                                 |
| Queue Manager                               | Pendiente (Hito 2)                  | Hoy `submit` lanza el proceso siempre, sin límite ni cola                                |
| Configuration                               | Pendiente (Hito 2)                  | Solo se recibe la ruta del socket como argumento                                         |
| Recovery                                    | Pendiente (Hito 2)                  | No existe                                                                                |
| Restricción de acceso remoto (LAN/VPN)      | Pendiente (Hito 3)                  | No existe; el transporte actual es un Unix socket local                                  |

## Consecuencias

### Positivas

- Permite operación local y remota.
- Existe una separación clara entre CLI y servidor.
- El servidor continúa funcionando aunque un cliente se desconecte.
- Los Jobs se aíslan mediante procesos hijos.
- Permite organizar el código por responsabilidades.
- Facilita las pruebas unitarias y de integración.
- Reduce la complejidad respecto a múltiples servicios independientes.
- Facilita el mantenimiento.
- Permite evolucionar los módulos internamente sin modificar necesariamente el protocolo.
- Permite utilizar el mismo servicio para múltiples clientes.
- Facilita la implementación de las operaciones de consulta, cancelación y administración.
- `JobManager` depende de las abstracciones `IJobStore` e `IProcessLauncher` y no conoce JSON ni sockets (solo `RequestHandler` conoce el esquema de mensajes). Por ello, persistencia y lanzador de procesos pueden sustituirse (por ejemplo, con un lanzador falso en pruebas) sin modificar el dominio (RNF-17, RNF-20).

### Negativas y riesgos

- El servidor continúa siendo un proceso central.
- Un fallo crítico del servidor puede afectar el servicio completo.
- Las interfaces internas deben mantenerse claras.
- La concurrencia dentro del servidor se controla con el modelo de un solo hilo y event loop `epoll` descrito en ADR-003 (sin locks); un callback lento afecta a todos los clientes.
- El protocolo de red introduce una superficie de ataque.
- Los procesos hijos deben supervisarse correctamente.
- Se debe evitar acoplamiento excesivo entre módulos.
- Se deben evitar dependencias circulares.
- Las desconexiones de clientes deben manejarse sin modificar incorrectamente el estado de los Jobs.
- La comunicación de red deberá validarse para evitar mensajes incompletos, inválidos o excesivamente grandes.
- Varios módulos del diseño aún no están implementados (ver Estado de implementación); hasta entonces los requisitos asociados no pueden considerarse cumplidos.

## Evidencia / prototipo

La implementación actual y las validaciones posteriores demostrarán progresivamente la arquitectura propuesta.

Para la primera validación se considerarán las capacidades disponibles en la implementación local:

1. Inicio del servidor.
2. Inicio del cliente CLI.
3. Comunicación entre cliente y servidor.
4. Recepción de una solicitud.
5. Creación de un Job.
6. Generación de un Job ID.
7. Ejecución del Job mediante un proceso independiente.
8. Consulta del estado.
9. Recuperación del resultado.
10. Captura de stdout y stderr.
11. Solicitud de cancelación.
12. Obtención del código de salida.
13. Manejo de una solicitud inválida.
14. Separación de responsabilidades entre los módulos principales.

La evidencia se almacenará en:

```text
verif/results/<run-id>/
```

Cada ejecución deberá identificar:

- Commit.
- Sistema operativo.
- Arquitectura.
- Configuración.
- Ruta del socket (Hito 1); dirección y puerto cuando exista el transporte TCP.
- Versión del protocolo (cuando se defina el protocolo en un ADR posterior).
- Comandos ejecutados.
- Resultado de las pruebas.
- Logs generados.

La validación incluirá pruebas de integración y revisión de las dependencias entre módulos.

## Requisitos afectados

Los módulos marcados como pendientes en el Estado de implementación (Queue Manager, Configuration, Recovery, persistencia en disco y restricción de acceso remoto) indican que los requisitos que dependen de ellos aún no están cumplidos.

|Requisito|Relación|
|---|---|
|RF-01|El servidor recibe solicitudes para crear Jobs.|
|RF-02|Network/API valida el formato de la solicitud y Job Manager valida su contenido (por ejemplo, comando vacío).|
|RF-03|El Queue Manager administra Jobs pendientes (pendiente, Hito 2).|
|RF-04|El Process Manager ejecuta Jobs mediante procesos independientes.|
|RF-05|El Queue Manager y Job Manager controlan la concurrencia (pendiente, Hito 2).|
|RF-06|El Job Manager administra los estados del ciclo de vida.|
|RF-07|Los tiempos y códigos de salida se guardan en el modelo del Job (Hito 1); pendiente exponerlos en `status`/`list` y persistirlos en disco.|
|RF-08|El servidor permite consultar Jobs por ID.|
|RF-09|El servidor permite listar Jobs.|
|RF-10|Job Manager y Process Manager coordinan la cancelación.|
|RF-11|Process Manager captura stdout y stderr por separado; el Job (vía `IJobStore`) los conserva.|
|RF-12|Persistence conservará metadata después de un reinicio (pendiente, Hito 2).|
|RF-13|Recovery reconstruirá el estado del servicio (pendiente, Hito 2).|
|RF-14|Logging registra eventos y errores.|
|RF-15|El servidor deja de aceptar conexiones al cerrar (implementado); terminar los Jobs en ejecución al apagar queda pendiente (ver RNF-30).|
|RF-16|Configuration administrará los parámetros del servicio (pendiente, Hito 2).|
|RF-17|El CLI proporciona la interfaz para el usuario.|
|RF-18|La arquitectura cliente-servidor permite operación remota (pendiente, Hito 3).|
|RF-19|Cliente local y remoto utilizan la misma lógica del servidor.|
|RF-20|Network/API aplicará restricciones de acceso (pendiente, Hito 3).|
|RF-21|Network/API implementa y valida el protocolo.|
|RF-22|El servidor continúa operando ante desconexiones del cliente.|
|RF-23|Los módulos de configuración y administración aplicarán límites (pendiente).|
|RF-24|El servidor podrá proporcionar información de salud del servicio (pendiente).|
|RF-25|Queue Manager controlará la saturación de la cola (pendiente, ADR futuro de saturación).|
|RF-26|Job Manager controla cancelaciones concurrentes; el event loop de un solo hilo las serializa (ADR-003).|
|RF-27|Job Manager aplica una ventana de deduplicación en memoria (3 s por comando, argumentos y origen del cliente); su semántica queda por documentar en un ADR posterior.|
|RF-28|Network/API y Job Manager definen el comportamiento ante desconexiones.|
|RF-29|Process Manager detecta terminaciones inesperadas.|
|RF-30|Process Manager implementará la política de cancelación (escalamiento pendiente, ADR futuro de cancelación).|
|RNF-17|La arquitectura separa responsabilidades mediante módulos.|
|RNF-18|Las interfaces y decisiones arquitectónicas serán documentadas.|
|RNF-27|El diseño debe mantener consistencia bajo concurrencia (modelo de un solo hilo, ADR-003).|
|RNF-29|La arquitectura permite aplicar límites y rechazo ante saturación (pendiente).|
|RNF-30|Process Manager debe liberar procesos y recursos; el cierre sin huérfanos está pendiente.|
|RNF-34|La decisión arquitectónica considera riesgos de seguridad y consistencia.|
