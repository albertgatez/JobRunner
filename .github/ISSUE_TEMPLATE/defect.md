name: "Defecto"
description: "Defecto/falla detectado en verificación o por el cliente."
title: "[BUG] <descripción corta>"
labels: ["defecto", "bug"]
body:
  - type: textarea
    id: steps
    attributes:
      label: Pasos para reproducir
      description: Comandos, configuración y entorno (distro/commit) exactos.
      placeholder: |
        1. ...
        2. ...
    validations:
      required: true
  - type: textarea
    id: expected
    attributes:
      label: Resultado esperado
    validations:
      required: true
  - type: textarea
    id: observed
    attributes:
      label: Resultado observado
    validations:
      required: true
  - type: textarea
    id: severity
    attributes:
      label: Severidad e impacto
      description: Crítico / Alto / Medio / Bajo y en qué requisito afecta.
    validations:
      required: true
  - type: textarea
    id: evidence
    attributes:
      label: Evidencia
      description: Logs, capturas, archivos en verif/results/ (sin editar para ocultar fallas).
  - type: input
    id: owner
    attributes:
      label: Responsable (dueño)
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
      label: Evidencia de cierre / prueba de regresión