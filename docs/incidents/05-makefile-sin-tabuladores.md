# INC-05 — `make` fallaba por espacios en el `Makefile`

- Fecha: 2026-10-29
- Síntomas: `make` terminaba con `Makefile:31: *** missing separator.  Stop.`
- Evidencia recolectada (antes de modificar): `make -n` y `git show HEAD:Makefile | sed -n 31p | cat -A` en `ec9e92c`, que muestra espacios al inicio de la línea.
- Hipótesis:
  1. Las reglas usaban espacios en vez de tabuladores. **Confirmada.**
  2. Lo causaron cambios locales de finales de línea. **Descartada:** ya estaba así en el commit.
- Causa raíz: `make` exige un tabulador al inicio de cada comando de una regla.
- Corrección / mitigación / aceptación de riesgo: `403e652` cambió los espacios por tabuladores. Comprobado en el diff, sin ejecutar `make`.
- Prueba de regresión añadida: ninguna.
- Estado: EN INVESTIGACIÓN (falta ejecutar `make` en `main`)
