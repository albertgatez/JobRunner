# INC-06 — Finales de línea mezclados en el repositorio

- Fecha: 2026-10-29
- Síntomas: `git add --renormalize .` marcó 10 archivos como modificados, con 1035 líneas cambiadas sin diferencias de contenido.
- Evidencia recolectada (antes de modificar): `git diff --cached --stat` y `git ls-files --eol`. En `80cf42d` siguen 9 archivos con CRLF, entre ellos `src/domain/job_manager.hpp` y `.gitignore`.
- Hipótesis:
  1. Los archivos locales tenían CRLF. **Descartada:** no se encontró ninguno.
  2. Estaban versionados con CRLF. **Confirmada.**
- Causa raíz: el repositorio no define una política de finales de línea y el equipo trabaja entre Windows y Linux.
- Corrección / mitigación / aceptación de riesgo: la normalización se revirtió. En un equipo se configuró `core.autocrlf=input` y `core.eol=lf`, lo que no protege a los demás. No se conoce ningún fallo de ejecución; el riesgo es que un diff de todo el archivo oculte el cambio real.
- Prueba de regresión añadida: ninguna. Depende de que el equipo adopte un `.gitattributes` común.
- Estado: EN INVESTIGACIÓN
