name: "Tarea técnica"
description: "Trabajo técnico concreto (refactor, integración, tooling, pruebas)."
title: "[TASK] <título corto>"
labels: ["tarea técnica"]
body:
  - type: textarea
    id: context
    attributes:
      label: Contexto
      description: Qué se necesita hacer y por qué (ligar ADR/requisito si aplica).
    validations:
      required: true
  - type: textarea
    id: acceptance
    attributes:
      label: Criterios de aceptación (verificables)
    validations:
      required: true
  - type: input
    id: owner
    attributes:
      label: Responsable principal
    validations:
      required: true
  - type: dropdown
    id: priority
    attributes:
      label: Prioridad
      options:
        - "P0 (bloquea hito)"
        - "P1 (alta)"
        - "P2 (media)"
        - "P3 (baja)"
    validations:
      required: true
  - type: textarea
    id: dependencies
    attributes:
      label: Dependencias
  - type: textarea
    id: closing
    attributes:
      label: Evidencia de cierre (PRs, resultados, docs)