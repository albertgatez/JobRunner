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

_(sin incidentes todavía)_