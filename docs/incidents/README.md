# Registro de incidentes

> Todo incidente (incluidos los **no anunciados** introducidos por el cliente
> en una revisión técnica) se documenta aquí. El evaluador califica el
> diagnóstico y la respuesta del equipo: se recolecta evidencia antes de
> modificar, se reduce a caso reproducible, se justifica la corrección o
> mitigación o la aceptación del riesgo, y se añade una prueba de regresión.

## Formato por incidente (`<NN>-<tema>.md`)

```markdown
# INC-<NN> — <tema>

- Fecha: <aaaa-mm-dd>
- Síntomas:
- Evidencia recolectada (antes de modificar):
- Hipótesis:
- Causa raíz:
- Corrección / mitigación / aceptación de riesgo:
- Prueba de regresión añadida:
- Estado: CERRADO | EN INVESTIGACIÓN
```

## Defectos

Los defectos de requisitos se registran en Issues (`defect`), no aquí; este
registro es para incidentes de operación/revisión.

## Estado

| Incidente | Estado | Qué falta |
| --- | --- | --- |
| [INC-01 — Proceso zombie](01-proceso-zombie.md) | EN INVESTIGACIÓN | Prueba de regresión |
| [INC-02 — Jobs huérfanos al apagar](02-jobs-huerfanos-al-apagar.md) | CERRADO | Nada |
| [INC-03 — Sockets heredados por los jobs](03-sockets-heredados-por-jobs.md) | EN INVESTIGACIÓN | Corrección y prueba de regresión |
| [INC-04 — Pruebas rotas en `main`](04-pruebas-rotas-en-main.md) | CERRADO | Nada |
| [INC-05 — `Makefile` con espacios](05-makefile-sin-tabuladores.md) | EN INVESTIGACIÓN | Ejecutar `make` en `main` |
| [INC-06 — Finales de línea mezclados](06-finales-de-linea-mezclados.md) | EN INVESTIGACIÓN | Decisión del equipo |
