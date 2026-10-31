# INC-04 — La suite de pruebas no compila en `main`

- Fecha: 2026-10-30
- Síntomas: `CMakeLists.txt` (líneas 83–85) declara `test_sqlite_job_store.cpp`, `test_recovery.cpp` y `test_orphan_termination.cpp`, que no están en el repositorio. La compilación de las pruebas no se ejecutó: el fallo se deduce de la lectura.
- Evidencia recolectada (antes de modificar): `git cat-file` no encuentra los archivos en `80cf42d` ni en `origin/Alberto`, y `git check-ignore` no los marca.
- Hipótesis:
  1. Git ignora los archivos. **Descartada.**
  2. No se incluyeron en el commit `403e652`. **Probable:** falta confirmar si existen en el equipo de quien lo hizo.
- Causa raíz: `403e652` declaró pruebas sin versionarlas y añadió métodos a `IProcessLauncher` sin actualizar `FakeProcessLauncher`. No hay integración continua que lo detecte.
- Corrección / mitigación / aceptación de riesgo: **sin corregir en `main`.** Para compilar el servidor y el cliente: `cmake -S . -B build -DJOBRUNNER_BUILD_TESTS=OFF` (comprobado). Los cambios locales sin commit ya actualizan `FakeProcessLauncher`; siguen faltando los tres archivos.
- Prueba de regresión añadida: ninguna. Propuesta: un flujo de integración continua que compile y ejecute `ctest` en cada PR.
- Estado: EN INVESTIGACIÓN
