# ADR-004: Persistencia

| Campo          | Valor                                                                      |
| -------------- | -------------------------------------------------------------------------- |
| Estado         | Implementado (Hito 2)                                                      |
| Fecha          | 2026-10-28                                                                 |
| Autor          | Equipo JUAN                                                                |
| Req. afectados | RF-07, RF-11, RF-12, RF-13, RNF-05, RNF-06, RNF-11, RNF-28, RNF-31, RNF-34 |
| Tipo           | Persistencia                                                               |

## Contexto

Hasta ahora `InMemoryJobStore` (ADR-003, Hito 1) mantiene todos los Jobs en un `std::map` en memoria. Es correcto para demostrar concurrencia, pero no sobrevive a un reinicio del servidor: se pierde el historial completo (comandos, tiempos, códigos de salida, stdout/stderr) cada vez que el proceso termina.

El Avance 02 exige:

- Conservar metadatos y resultados mínimos después de reiniciar el servicio (RF-12).
- Permitir recuperar ese historial al arrancar (RF-13, desarrollado en ADR-005).
- Que las actualizaciones persistentes sean atómicas o recuperables: una interrupción a mitad de una escritura no debe dejar el repositorio inutilizable (RNF-11, RNF-28).
- Soportar al menos 500 registros de Jobs sin pérdida de metadatos (RNF-06).
- Responder una consulta de estado en máximo 1 segundo con 100 Jobs almacenados (RNF-05).

El modelo de concurrencia (ADR-003) ya fija una restricción favorable: todo el servidor corre en un solo hilo (el event loop `epoll`), así que **nunca hay más de un escritor concurrente** sobre el almacenamiento. El problema de persistencia no necesita resolver concurrencia entre hilos; solo necesita sobrevivir una escritura interrumpida (crash, `kill -9`, corte de energía) sin corromper el archivo.

`IJobStore` ya existe como abstracción (inyección de dependencias, DIP): `JobManager` solo conoce la interfaz, no la implementación concreta, así que cualquier backend puede reemplazar a `InMemoryJobStore` sin tocar la lógica de orquestación.

## Alternativas consideradas

### Alternativa 1. Un archivo por Job

Cada Job se guarda como un archivo individual (p. ej. `data/jobs/<id>.json`), más un índice o un `scandir` del directorio para listar.

**Ventajas:**

- Muy simple de inspeccionar manualmente (cat de un archivo).
- Sin dependencias externas.
- Actualizar un Job solo reescribe su propio archivo, no afecta a los demás.

**Desventajas y riesgos:**

- `list()` con filtro por estado (RF-09) obliga a abrir y parsear todos los archivos. Costoso al acercarse a RNF-06 (500 registros) y a RNF-05 (1s de respuesta).
- La atomicidad de cada archivo individual debe implementarse a mano (escribir a un temporal y `rename()`), y hay que repetir esa disciplina en cada punto de escritura.
- No hay forma simple de consultar "todos los Jobs en estado RUNNING" sin mantener un índice aparte, que a su vez puede desincronizarse del contenido real.

### Alternativa 2. Un único archivo JSON (snapshot completo)

Todo el estado se mantiene en un solo archivo (p. ej. `data/jobs.json`), que se reescribe completo cada vez que cambia algo, escribiendo primero a un archivo temporal y haciendo `rename()` para que el remplazo sea atómico a nivel de sistema de archivos.

**Ventajas:**

- Lectura de recuperación simple: un solo `open`/`parse` reconstruye todo el estado (RF-13).
- La atomicidad se resuelve una sola vez (`write tmp` + `rename`), no por cada Job.
- Sin dependencias externas más allá de una librería JSON (ya presente en el proyecto: `nlohmann::json`).

**Desventajas y riesgos:**

- Cada actualización (una línea de stdout, un cambio de estado) reescribe el archivo completo: costo O(n) en el número de Jobs y en el tamaño acumulado de `stdout_data`/`stderr_data`, que crece con cada Job de larga duración.
- No ofrece consultas indexadas: listar por estado sigue siendo un recorrido lineal en memoria después de cargar el archivo (aceptable para 500 registros, pero sin margen de crecimiento).
- Reescribir el archivo completo en cada evento de I/O del Job (cada fragmento de stdout) multiplica la escritura a disco de forma innecesaria si no se agrupan las actualizaciones.

### Alternativa 3. SQLite (archivo único con transacciones)

Una tabla `jobs` en un archivo `.db` de SQLite, con una fila por Job. Cada `update()` de `IJobStore` se traduce en una transacción (`BEGIN`/`COMMIT`).

**Ventajas:**

- Atomicidad y durabilidad resueltas por el motor: una transacción interrumpida se revierte sola al reabrir la base (journal o modo WAL), sin que el equipo deba reinventar la lógica de "escribir a temporal y renombrar" (RNF-11, RNF-28).
- Consultas indexadas: `SELECT ... WHERE state = ?` resuelve `list(state_filter)` sin recorrer ni parsear todo el histórico, lo que da margen para RNF-05 y RNF-06 incluso si el número de Jobs crece más allá de lo exigido.
- Cada `UPDATE` afecta solo la fila del Job modificado, no reescribe el archivo completo como en la Alternativa 2.
- Es la ruta que el propio código ya anticipa: `IJobStore` está documentado como reemplazable por `SqliteJobStore` en Hito 2, y `InMemoryJobStore` ya referencia esa intención en su comentario.
- Un único archivo de datos, fácil de ubicar junto al directorio de datos configurable (RF-16).

**Desventajas y riesgos:**

- Agrega una dependencia (`libsqlite3` o un wrapper C++).
- Requiere definir un esquema y, si cambia en el futuro, una migración explícita.
- Hay que decidir el modo de journaling (WAL vs. `DELETE`/rollback journal por defecto) y documentar esa decisión.
- El equipo debe mapear `JobState` y los campos opcionales de `Job` (timestamps, `pid`, `exit_code`, `exit_signal`) a columnas, cuidando los `NULL` para los campos que aún no existen (p. ej. `finished_at` mientras el Job está `RUNNING`).

## Decisión

Se utilizará **SQLite** como backend de persistencia (Alternativa 3), implementado como una nueva clase `SqliteJobStore` que satisface `IJobStore`, reemplazando a `InMemoryJobStore` en `main.cpp` sin tocar `JobManager` (DIP/OCP, ya vigente en la arquitectura).

### Razones de la decisión

1. **Atomicidad sin reinventar el mecanismo.** RNF-11 y RNF-28 exigen que una interrupción a mitad de una escritura no deje el repositorio inutilizable. Las transacciones de SQLite resuelven esto directamente; las alternativas de archivos (1 y 2) requieren que el equipo implemente y mantenga correctamente el patrón "escribir a temporal + `rename()`" en cada punto de mutación.
2. **Consultas eficientes a medida que crece el historial.** RF-09 (listar por estado) y RNF-05 (responder en ≤1s con 100 Jobs) se benefician de un índice por `state`, algo que un archivo JSON plano no ofrece sin reconstruir una estructura en memoria equivalente.
3. **Costo de escritura proporcional al cambio, no al tamaño total.** A diferencia de la Alternativa 2, actualizar un Job (p. ej. un fragmento nuevo de stdout) no implica reescribir el historial completo de los demás Jobs.
4. **Consistencia con el diseño ya anticipado.** `IJobStore`, `InMemoryJobStore` y los comentarios de `main.cpp` ya nombran `SqliteJobStore` como el backend de Hito 2; este ADR formaliza esa intención en lugar de introducir un mecanismo nuevo no previsto.
5. **No se necesita concurrencia entre escritores.** El modelo de un solo hilo (ADR-003) significa que SQLite no se usa aquí por sus capacidades de concurrencia multi-proceso/multi-hilo, sino únicamente por su atomicidad transaccional y sus consultas indexadas. Es una herramienta más simple de operar correctamente que mantener la disciplina de atomicidad a mano sobre archivos.

**Diseño propuesto (resumen):**

- Un archivo `jobrunner.db` en el directorio de datos configurable (RF-16) con `--data-dir` (por defecto `./data`).
- Tabla `jobs`

| Campo | Tipo | Restricción |
|---|---|---|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| command | TEXT | NOT NULL |
| args | TEXT | NOT NULL |
| state | TEXT | NOT NULL, CHECK en los 5 estados |
| received_at | INTEGER | NOT NULL |
| started_at | INTEGER | NULL |
| finished_at | INTEGER | NULL |
| exit_code | INTEGER | NULL |
| exit_signal | INTEGER | NULL |
| pid | INTEGER | NULL |
| pid_start_time | INTEGER | NULL |
| boot_id | TEXT | NOT NULL DEFAULT '' |
| stdout_data | TEXT | NOT NULL DEFAULT '' |
| stderr_data | TEXT | NOT NULL DEFAULT '' |
| diagnostic | TEXT | NOT NULL DEFAULT '' |

- `args` se serializa como JSON (ya se usa `nlohmann::json` en el proyecto).
- Los instantes se guardan como nanosegundos desde epoch (UTC): ida y vuelta exacta con `std::chrono::system_clock` y ordenables como enteros.
- `pid_start_time` y `boot_id` forman la huella del proceso que ADR-005 usa para reconocer, tras una caída, si un proceso vivo es realmente el del Job.
- `diagnostic` explica desenlaces que no decidió el proceso (interrumpido por reinicio, cancelado por cierre).
- `AUTOINCREMENT` impide reutilizar ids de Jobs borrados; `--reset-db` vacía la tabla y reinicia la secuencia.
- Índice `idx_jobs_state` sobre `state` para `list(state_filter)`.
- La versión del esquema se guarda en `PRAGMA user_version` (hoy `1`). Una base con versión mayor se rechaza al abrir; una migración futura incrementa la versión.
- Un `flock` sobre `jobrunner.db.lock` impide que dos servidores usen la misma base (la recuperación de uno marcaría como interrumpidos los Jobs vivos del otro). Se usa un archivo aparte, y no el modo de bloqueo exclusivo de SQLite, para que la base pueda inspeccionarse con la CLI `sqlite3` mientras el servidor corre.

- Cada `IJobStore::update()` se ejecuta dentro de una transacción (`BEGIN IMMEDIATE` / `COMMIT`), de modo que una escritura incompleta se revierte sola al reabrir la base.
- Modo `WAL` para evitar bloquear lecturas durante una escritura y para que una terminación abrupta del proceso deje la base en un estado consistente al recuperar el WAL en el siguiente arranque. Se usa `synchronous = NORMAL`: un commit sobrevive a la caída del proceso (`kill -9`), y ante un corte de energía puede perderse la última transacción pero la base nunca se corrompe. Se acepta para no hacer `fsync` por cada fragmento de stdout/stderr.
- Si el mutador de `update()` lanza (p. ej. una transición de estado inválida), la transacción se revierte y la fila queda intacta.
- `list(state_filter)` usa `SELECT ... WHERE state = ?` cuando `state_filter` tiene valor, y `SELECT *` sin filtro, evitando cargar y parsear todo el historial en C++.

### Estado de implementación

| Elemento                                                                       | Estado                |
| ------------------------------------------------------------------------------ | --------------------- |
| `IJobStore` como abstracción inyectada en `JobManager`                         | Implementado (Hito 1) |
| `InMemoryJobStore` (backend sin persistencia, se conserva para pruebas)        | Implementado (Hito 1) |
| Esquema de tabla `jobs` y serialización de `Job` ↔ fila                        | Implementado (Hito 2) |
| `SqliteJobStore` (implementación de `IJobStore` respaldada por SQLite)         | Implementado (Hito 2) |
| Transacciones por `update()` (atomicidad, RNF-11/RNF-28)                       | Implementado (Hito 2) |
| Modo WAL                                                                       | Implementado (Hito 2) |
| Sustitución en `main.cpp` de `InMemoryJobStore` por `SqliteJobStore`           | Implementado (Hito 2) |
| `--data-dir` y `--reset-db` en `jobrunner-server`                              | Implementado (Hito 2) |
| Candado contra dos instancias sobre la misma base                              | Implementado (Hito 2) |
| Verificación con `kill -9` a mitad de una escritura (evidencia en `verif/`)    | Pendiente             |
| Medición RNF-05 (100 Jobs, ≤1 s)                                               | Pendiente             |

## Consecuencias

### Positivas

- La atomicidad de cada actualización queda garantizada por el motor de base de datos, sin lógica adicional de "archivo temporal + rename" repetida por el equipo.
- Las consultas por estado (RF-09) y el tiempo de respuesta (RNF-05) escalan mejor que con archivos planos.
- Un solo archivo de datos, fácil de respaldar, copiar o inspeccionar con herramientas estándar (`sqlite3` CLI).
- La recuperación al arrancar (ADR-005) se apoya en consultas SQL simples en vez de parsers JSON hechos a mano.
- El cambio de backend no afecta a `JobManager` ni a la lógica de dominio, gracias a `IJobStore` (DIP/OCP ya vigentes).

### Negativas y riesgos

- Agrega una dependencia nueva al proyecto (`libsqlite3`), documentada en el manual de usuario (RNF-02): el `Makefile` requiere las cabeceras del sistema (`sqlite-devel` / `libsqlite3-dev`); CMake las usa si existen y, si no, descarga la amalgamación oficial de SQLite 3.50.4 con hash fijado.
- Un esquema mal diseñado desde el inicio puede requerir una migración más adelante; debe revisarse junto con ADR-005 antes de implementar.
- Si el modo WAL no se configura correctamente, una terminación abrupta podría dejar archivos `-wal`/`-shm` que deben limpiarse o conciliarse al reabrir la base, debe verificarse explícitamente en las pruebas de recuperación (RNF-28).
- El equipo debe decidir qué hacer con `stdout_data`/`stderr_data` muy grandes (¿se truncan? ¿se almacenan completos?). No resuelto en este ADR, queda como riesgo documentado para una revisión futura si se observa un problema de tamaño. Hoy cada fragmento de salida reescribe la fila completa, así que un Job con mucha salida tiene un costo de escritura que crece con el tamaño acumulado.

## Evidencia / prototipo

La validación deberá demostrar progresivamente:

1. Crear, actualizar y consultar Jobs usando `SqliteJobStore` con los mismos casos de prueba unitarios ya existentes para `InMemoryJobStore` (mismo contrato `IJobStore`).
2. Confirmar que `list(state_filter)` devuelve resultados correctos para cada estado.
3. Interrumpir el proceso (`kill -9`) a mitad de una transacción de escritura y confirmar, al reabrir la base, que no queda en un estado corrupto (la transacción interrumpida se revierte, no se aplica parcialmente).
4. Medir el tiempo de una consulta de estado con al menos 100 Jobs almacenados (RNF-05).
5. Insertar al menos 500 Jobs y confirmar que no hay pérdida de metadatos (RNF-06).

La evidencia se almacenará en `verif/results/<run-id>/`, siguiendo el mismo formato acordado en ADR-003.

## Requisitos afectados

| Requisito | Relación                                                                                |
| --------- | --------------------------------------------------------------------------------------- |
| RF-07     | Los timestamps y el código de salida se persisten junto con el resto del Job.           |
| RF-11     | stdout/stderr capturados se conservan como parte del registro persistido.               |
| RF-12     | SQLite conserva metadatos y resultados tras reiniciar el servicio.                      |
| RF-13     | La base persistida es la fuente que se lee al recuperar el historial al arrancar.       |
| RNF-05    | Las consultas indexadas por estado buscan cumplir el límite de 1s con 100 Jobs.         |
| RNF-06    | El esquema soporta al menos 500 registros sin pérdida de metadatos.                     |
| RNF-11    | Las transacciones evitan dejar un estado parcialmente actualizado.                      |
| RNF-28    | Las actualizaciones son atómicas o recuperables ante una interrupción.                  |
| RNF-31    | La base persistida es la entrada que permitirá distinguir Jobs en ADR-005.              |
| RNF-34    | Los riesgos de consistencia de esta decisión requieren prueba asociada (ver Evidencia). |