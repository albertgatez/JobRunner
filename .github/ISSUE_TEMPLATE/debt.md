name: "Deuda / mejora"
description: "Mejora, deuda técnica o decisión abierta que no es requisito nuevo."
title: "[DEBT] <título corto>"
labels: ["deuda", "mejora"]
body:
  - type: textarea
    id: context
    attributes:
      label: Contexto
      description: Qué mejora/deuda y dónde vive (módulo/doc/ADR — contribución).
    validations:
      required: true
  - type: textarea
    id: acceptance
    attributes:
      label: Criterios de aceptación
    validations:
      required: true
  - type: input
    id: owner
    attributes:
      label: Responsable
    validations:
      required: true
  - type: dropdown
    id: priority
    attributes:
      label: Prioridad
      options:
        - "P1 (alta)"
        - "P2 (media)"
        - "P3 (baja)"
        - "P4 (hernia)"
    validations:
      required: true
  - type: textarea
    id: dependencies
    attributes:
      label: Dependencias
  - type: textarea
    id: closing
    attributes:
      label: Evidencia de cierre