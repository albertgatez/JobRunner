# Uso de IA — 2026-10-02 — verificación y documentación del Avance 1

- Instrumento/herramienta: Claude (Claude Code)
- Objetivo: Preparar los entregables de verificación y documentación del Avance 1: pruebas automáticas, scripts de verificación, casos de prueba y documentación técnica.
- Resultado recibido: Pruebas unitarias con Catch2 y configuración de CMake, scripts de verificación (`tc-001.sh`, `tc-002.sh`, `tc-003.sh`), borradores de los casos TC-001 a TC-003 con evidencia de ejecución, `arquitectura.md`, README y `verif/README.md`. También un cambio en `request_handler.cpp` para exponer los tiempos de cada Job (`received_at`, `started_at`, `finished_at`).
- Revisión realizada: Se compararon los resultados con los requisitos y el Plan de Verificación, y se contrastó `arquitectura.md` con los ADR y con el código implementado. Se ejecutaron las pruebas y los scripts, y se comprobó que TC-003 falla si falta un campo.
- Cambios aplicados: Se corrigió el README para usar CMake, que es lo que fija el ADR-001. Se quitaron los archivos que no se necesitan para el Avance 1 y se añadió una nota sobre el uso de Python en los scripts.
- Prueba añadida / verificación: TC-001 y TC-003 pasan. TC-002 falla a propósito porque el límite de concurrencia y la cola aún no existen (Issue 4). La evidencia está en `verif/results/run-2026-10-02-02/`.
- Aprendizaje: Las pruebas unitarias con un lanzador de procesos falso no detectan errores en la respuesta JSON del servidor, y los scripts contra el servidor real sí. Un caso no se marca como PASS sin ejecución reproducible.
- Resultado: MODIFICADO