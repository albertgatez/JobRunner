name: "Historia / Requisito"
description: "Story por requisito (RF/RNF) de la línea base o ampliación aprobada."
title: "[RF-XX] <título corto>"
labels: ["requisito"]
body:
  - type: markdown
    attributes:
      value: |
        Cada Issue de requisito debe ligarse a la línea base y a la matriz de trazabilidad.
        Se cierra sólo con evidencia que cumpla la Definición de Hecho.
  - type: input
    id: requirement_id
    attributes:
      label: Requisito (RF/RNF)
      placeholder: "RF-01"
    validations:
      required: true
  - type: textarea
    id: context
    attributes:
      label: Contexto y objetivo
      description: Descripción breve y motivación del requisito según el cliente.
    validations:
      required: true
  - type: textarea
    id: acceptance
    attributes:
      label: Criterios de aceptación (verificables)
      description: Resultados esperados medibles (vincular TC-XXX si aplica).
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
      description: Issues/requisitos/ADR de los que depende.
  - type: textarea
    id: evidence
    attributes:
      label: Evidencia de cierre
      description: Tests, resultados en verif/results/<run-id>/, matriz PASS, docs.