# Uso de IA — 2026-10-02 — arquitectura inicial

- Instrumento/herramienta: Claude (Claude Code)
- Objetivo: Redactar el documento de arquitectura inicial (`docs/technical-guide/arquitectura.md`) a partir del código y de los ADR.
- Resultado recibido: Un borrador de `arquitectura.md` con la estructura cliente-servidor, los módulos del servidor, el flujo de una solicitud, el ciclo de vida de un Job y un resumen del uso de `epoll` y `signalfd`.
- Revisión realizada: Se contrastó con ADR-001, ADR-002 y ADR-003 y con el código en `src/` (reactor, `JobManager`, lanzador de procesos y protocolo de tramas). Se preguntó explícitamente si el documento era coherente con los ADR y con lo implementado.
- Cambios aplicados: Se ajustó el documento para describir solo lo que existe hoy. El límite de concurrencia y la cola quedan como pendientes (Issue 4). Queda por decidir si se menciona el patrón Reactor en el documento y en el ADR-003.
- Prueba añadida / verificación: Revisión cruzada documento–código–ADR. No hay prueba automática asociada.
- Aprendizaje: El documento de arquitectura debe reflejar el estado real del código, y las diferencias con los ADR hay que registrarlas como pendientes, no ocultarlas.
- Resultado: MODIFICADO