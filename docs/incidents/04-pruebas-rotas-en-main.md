# INC-04 — La suite de pruebas no compila en `main`

- Fecha: 2026-10-30
- Síntomas: `CMakeLists.txt` (líneas 83–85) declara `test_sqlite_job_store.cpp`, `test_recovery.cpp` y `test_orphan_termination.cpp`, que no están en el repositorio. La compilación de las pruebas no se ejecutó: el fallo se deduce de la lectura.
- Evidencia recolectada (antes de modificar): `git cat-file` no encuentra los archivos en `80cf42d` ni en `origin/Alberto`, y `git check-ignore` no los marca.
- Hipótesis:
  1. Git ignora los archivos. **Descartada.**
  2. No se incluyeron en el commit `403e652`. **Confirmada:** llegaron después, en `32808ca`.
- Causa raíz: `403e652` declaró pruebas sin versionarlas y añadió métodos a `IProcessLauncher` sin actualizar `FakeProcessLauncher`. No hay integración continua que lo detecte.
- Corrección / mitigación / aceptación de riesgo: **corregido.** `32808ca` (PR #59) añadió los tres archivos y `eab887d` actualizó `FakeProcessLauncher`. Al integrarlos, `kill_live_processes escala a SIGKILL solo los que siguen vivos` falló porque no contemplaba el `SIGKILL` de INC-02; se ajustó la prueba sin cambiar lo que comprueba.
- Prueba de regresión añadida: la propia suite, que compila y se ejecuta con `ctest`. Sigue sin haber integración continua que lo compruebe en cada PR.
- Estado: CERRADO
