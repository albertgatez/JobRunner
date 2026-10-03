# Verificación

Casos de prueba, scripts y evidencia de J.U.A.N. (JobRunner). Todos los comandos se ejecutan **desde la raíz del repositorio**.

## Contenido

| Carpeta | Qué hay |
|---|---|
| `test-cases/` | Casos de prueba (`TC-00X.md`) y la plantilla `TC-Template.md` |
| `scripts/` | Scripts que ejecutan un caso contra el servidor real (`tc-001.sh`, `tc-002.sh`, `tc-003.sh`) y `common.sh` con funciones compartidas |
| `results/<run-id>/` | Evidencia de cada corrida: salidas, bitácoras del servidor y resultados |
| `verification-plan/` | Matriz de trazabilidad (en construcción) |
| `test-data/` | Datos de prueba |

Las pruebas unitarias (Catch2) están en `tests/`, fuera de esta carpeta.

## Requisitos

- Linux, GCC con C++20 y CMake 3.14 o superior.
>  `python3` y `ps` (paquete `procps`), usados por los scripts.`python3` arma las tramas crudas de las solicitudes inválidas y `ps` cuenta los procesos hijos. No se necesitan para compilar ni para `ctest`.
- La primera compilación con CMake necesita internet para descargar Catch2 y nlohmann/json, salvo que ya estén instalados.

## 1. Compilar

```bash
cmake -S . -B build
cmake --build build -j
```

Genera `build/jobrunner-server`, `build/jobrunner-cli` y `build/jobrunner_tests`. Los scripts necesitan los tres, así que compilar solo con los `g++` del README principal **no basta**: `jobrunner_tests` solo lo genera CMake.

Vuelve a compilar después de cualquier cambio en `src/`. Si no, los scripts se ejecutan contra el binario anterior.

## 2. Pruebas automáticas en C++ (Catch2)

```bash
ctest --test-dir build --output-on-failure -E "TC-002"   # las 27 que deben pasar
ctest --test-dir build -R "TC-002"  # las 6 de TC-002: FALLAN hasta implementar el Issue 4
ctest --test-dir build  # todas (33): hoy 6 fallan, las de TC-002
ctest --test-dir build -R "sistema" # solo las de sistema (servidor real)
./build/jobrunner_tests "[TC-001]"  # unitarias de apoyo de TC-001 (también [TC-002], [TC-003])
./build/jobrunner_tests "[cancel]"  # por tema: submit, dedup, list, status, output, cancel, lifecycle, state
./build/jobrunner_tests --list-tests    # ver todas las unitarias
```

Hay dos ejecutables:

| Ejecutable | Qué prueba | Archivos |
|---|---|---|
| `jobrunner_tests` | **Unitarias**: `Job` y `JobManager` con un lanzador de procesos falso (sin `fork`). No pasan por la capa JSON (`request_handler.cpp`). | `tests/test_*.cpp` |
| `jobrunner_system_tests` | **De sistema**: arrancan el `jobrunner-server` real, hablan con él por el Unix socket y observan los procesos hijos en `/proc`. Hacen lo mismo que los scripts de la sección 3. | `tests/system/` |

Las pruebas unitarias pueden pasar aunque la respuesta del servidor esté mal; las de sistema y los scripts sí lo detectan.

## 3. Casos de prueba contra el servidor real

```bash
chmod +x verif/scripts/*.sh                       # solo la primera vez
RUN=run-$(date +%F)-01
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-001.sh
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-002.sh   # hoy FAIL: la cola aún no existe
BUILD_DIR=build RUN_ID=$RUN verif/scripts/tc-003.sh
```

| Variable | Significado | Por defecto |
|---|---|---|
| `BUILD_DIR` | Carpeta con los binarios compilados | `build` |
| `RUN_ID` | Nombre de la carpeta de evidencia en `verif/results/` | fecha y hora UTC |

Cada script arranca y detiene su propio servidor (un socket `/tmp/jobrunner-tc00X.sock` por caso), así que no hace falta tener nada corriendo. TC-002 y TC-003 tardan unos 15 s.

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
| `tc-00X-server.log` | Bitácora del servidor durante la corrida |
| `ctest-tc003.txt`, `unit-tests-all.txt`, `unit-tests-tc002.txt`, `system-tests-cpp.txt` | Salida de las pruebas unitarias y de sistema en C++ |
| `ps-antes-cancel.txt`, `ps-despues-cancel.txt` | Procesos hijos antes y después de cancelar (TC-003) |

Para que cuente como evidencia en la matriz, ejecuta los scripts sobre el commit fusionado en `main` y anota el hash en el caso de prueba.

## Estado de los casos

| Caso | Estado | Ejecución |
|---|---|---|
| TC-001 Envío de Jobs y solicitudes inválidas | PASS | `verif/scripts/tc-001.sh` |
| TC-002 Límite de concurrencia y cola | **FAIL** (la cola aún no existe; pasará con el Issue 4) | `verif/scripts/tc-002.sh` |
| TC-003 Estados, tiempos y código de salida | PASS | `verif/scripts/tc-003.sh` |

Pendiente la revisión de cada caso por un segundo integrante. El detalle está en `test-cases/`.

## Agregar un caso nuevo

1. Copia `test-cases/TC-Template.md` como `TC-0XX.md` y llénalo. La numeración sigue el Plan de Verificación (TC-001 a TC-015 mínimos, TC-016 a TC-024 adicionales).
2. Crea `scripts/tc-0XX.sh` con `source "$(dirname "$0")/common.sh"`, define `TC_NAME` y `SOCK`, y usa `check`, `start_server` y `stop_server`. Termina con `exit 1` si algo falla.
3. Ejecuta el script, guarda la evidencia en `results/<run-id>/` y registra el resultado en el caso y en la matriz.
4. No marques `PASS` sin evidencia ejecutada y reproducible.
