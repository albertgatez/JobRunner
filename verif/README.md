# Verificación

Casos de prueba, scripts y evidencia de J.U.A.N. (JobRunner). Todos los comandos se ejecutan **desde la raíz del repositorio**.

## Contenido

| Carpeta | Qué hay |
|---|---|
| `test-cases/` | Casos de prueba (`TC-00X.md`) y la plantilla `TC-Template.md` |
| `scripts/` | Scripts que ejecutan un caso contra el servidor real (`tc-001.sh`, `tc-002.sh`, `tc-003.sh`, `tc-009.sh`) y `common.sh` con funciones compartidas |
| `results/<run-id>/` | Evidencia de cada corrida: salidas, bitácoras del servidor y resultados |
| `verification-plan/` | Matriz de trazabilidad (en construcción) |
| `test-data/` | Datos de prueba |

Las pruebas automatizadas C++ (Catch2) están en `tests/`, fuera de esta carpeta.

## Requisitos

- Linux, GCC con C++20 y CMake 3.14 o superior.
- `python3`, `ps` y `pgrep` son necesarios para los scripts de integración; `python3` arma tramas inválidas y mide tiempos, y `ps`/`pgrep` inspeccionan procesos. No se necesitan para compilar ni para `ctest`.
- La primera compilación con CMake necesita internet para descargar Catch2 y nlohmann/json, y SQLite si sus cabeceras no están instaladas.

## 1. Compilar

```bash
cmake -S . -B build
cmake --build build -j
```

Genera `build/jobrunner-server`, `build/jobrunner-cli` y `build/jobrunner_tests`. Los scripts necesitan los tres, así que compilar solo con los `g++` del README principal **no basta**: `jobrunner_tests` solo lo genera CMake.

Vuelve a compilar después de cualquier cambio en `src/`. Si no, los scripts se ejecutan contra el binario anterior.

## 2. Pruebas automáticas en C++ (Catch2)

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "TC-002"       # cola y límite de concurrencia
ctest --test-dir build -R "TC-007"       # persistencia y recuperación
ctest --test-dir build -R "TC-009"       # cierre controlado
./build/jobrunner_tests "[TC-001]"       # filtrar por etiquetas Catch2
./build/jobrunner_tests "[recovery]"
./build/jobrunner_tests --list-tests    # ver los casos Catch2
```

Las pruebas Catch2 se compilan en un ejecutable:

| Ejecutable | Qué prueba | Archivos |
|---|---|---|
| `jobrunner_tests` | Pruebas Catch2 de dominio y almacenamiento con dobles, además de pruebas de protocolo, conexiones, manejador de solicitudes y lanzador POSIX. El conjunto prueba desde reglas aisladas hasta componentes de I/O y procesos reales. | `tests/`, incluyendo `tests/domain/`, `tests/network/`, `tests/process/`, `tests/protocol/` y `tests/server/` |

Los scripts de `verif/scripts/` ejercitan el servidor completo desde el CLI y guardan evidencia reproducible en `verif/results/`.

## 3. Casos de prueba contra el servidor real

```bash
chmod +x verif/scripts/*.sh                       # solo la primera vez
RUN=run-$(date +%F)-01
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-001.sh
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-002.sh
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-003.sh
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-009.sh
```

| Variable | Significado | Por defecto |
|---|---|---|
| `BUILD_DIR` | Carpeta con los binarios compilados | `build` |
| `RUN_ID` | Nombre de la carpeta de evidencia en `verif/results/` | fecha y hora UTC |

Cada script arranca y detiene su propio servidor (un socket `/tmp/jobrunner-tc00X.sock` por caso), así que no hace falta tener nada corriendo. TC-002 y TC-003 tardan unos 15 s; TC-009 comprueba además el plazo de gracia de cierre.

**Cómo leer el resultado:** el script imprime `[OK]` o `[FAIL]` por cada comprobación y termina con `RESULTADO TC-00X: PASS` o `FAIL`. El código de salida es 0 solo si pasa, y 1 si falla cualquier comprobación o si no se ejecuta el número esperado de comprobaciones.

```bash
BUILD_DIR=build verif/scripts/tc-003.sh | tail -3
echo $?                                           # 0 = PASS, distinto de 0 = FAIL
```

## 4. Evidencia

Cada corrida guarda en `verif/results/<run-id>/`:

| Archivo | Contenido |
|---|---|
| `tc-00X-output.txt` | Salida completa de cada paso y comprobación |
| `tc-002-ps-inicial.txt`, `tc-002-muestreo.txt` | Procesos hijos tras el envío y muestreo del número de hijos (TC-002) |
| `tc-009-output.txt`, `tc-009-*-server.log` | Comprobaciones del cierre controlado y bitácora del servidor (TC-009) |
| `tc-00X-server.log` | Bitácora del servidor durante la corrida |
| `ctest-*.txt`, `unit-tests-*.txt` | Salidas guardadas de pruebas Catch2 en C++ (cuando se incluyen en la corrida) |
| `ps-antes-cancel.txt`, `ps-despues-cancel.txt` | Procesos hijos antes y después de cancelar (TC-003) |

Para que cuente como evidencia en la matriz, ejecuta los scripts sobre el commit fusionado en `main` y anota el hash en el caso de prueba.

## Estado de los casos

| Caso | Estado | Ejecución |
|---|---|---|
| TC-001 Envío de Jobs y solicitudes inválidas | PASS | `verif/scripts/tc-001.sh` |
| TC-002 Límite de concurrencia y cola | PASS | `verif/scripts/tc-002.sh` |
| TC-003 Estados, tiempos y código de salida | PASS | `verif/scripts/tc-003.sh` |
| TC-008 Aislamiento de fallo: error al crear el proceso | PASS | `verif/scripts/tc-008.sh` |
| TC-009 Cierre controlado sin procesos huérfanos | PASS (15/15) | `verif/scripts/tc-009.sh` |

TC-009 tiene evidencia en `verif/results/run-2026-10-30-inc02-3-correccion/` y `verif/results/run-2026-10-30-inc02-4-carpeta-de-trabajo/`. El caso formal está descrito en [`test-cases/TC-009.md`](test-cases/TC-009.md).

Pendiente la revisión de cada caso por un segundo integrante. El detalle está en `test-cases/`.

## Agregar un caso nuevo

1. Copia `test-cases/TC-Template.md` como `TC-0XX.md` y llénalo. La numeración sigue el Plan de Verificación (TC-001 a TC-015 mínimos, TC-016 a TC-024 adicionales).
2. Crea `scripts/tc-0XX.sh` con `source "$(dirname "$0")/common.sh"`, define `TC_NAME` y `SOCK`, y usa `check`, `start_server` y `stop_server`. Termina con `exit 1` si algo falla.
3. Ejecuta el script, guarda la evidencia en `results/<run-id>/` y registra el resultado en el caso y en la matriz.
4. No marques `PASS` sin evidencia ejecutada y reproducible.
