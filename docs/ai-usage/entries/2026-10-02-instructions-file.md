# Uso de IA — 2026-10-02 — instructions file

- Instrumento/herramienta: Copilot
- Objetivo: Crear y ajustar un archivo de instrucciones del repositorio para guiar el trabajo futuro con foco en requisitos, trazabilidad y estilo.
- Resultado recibido: Un archivo `.github/copilot-instructions.md` con reglas de trabajo generales, estructura para archivos nuevos y recordatorios específicos del proyecto.
- Revisión realizada: Se contrastó con `Indicaciones/2._Requisitos_Funcionales.md`, `Indicaciones/3._Requisitos_No_Funcionales.md`, `Indicaciones/4._Estándares_de_Repositorio_e_Ingenieria.md`, y `docs/ai-usage/README.md` para mantener el formato y la intención del repo.
- Cambios aplicados: Se añadieron reglas de ahorro de tokens, código legible para humanos, pedir confirmación ante ambigüedad, usar la documentación del repo, avisar faltantes, y mantener el formato del archivo editado. También se definió una estructura sugerida para nuevos archivos C++.
- Prueba añadida / verificación: Se validó el diff con `git diff --check` sobre `.github/copilot-instructions.md` sin errores.
- Aprendizaje: Para este repo conviene registrar instrucciones como evidencia de IA cuando afectan la forma de trabajar y no solo el contenido técnico.
- Resultado: ACEPTADO tras verificación
