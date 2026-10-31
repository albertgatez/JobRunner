# INC-02 — Jobs huérfanos al apagar el servidor

- Fecha: 2026-10-30
- Síntomas: tras cerrar el servidor con `Ctrl+C` o `SIGTERM`, los jobs en ejecución seguían corriendo sin supervisión, adoptados por otro proceso. Incumple RF-15, RNF-25 y RNF-30.
- Evidencia recolectada (antes de modificar): corridas de TC-009 sobre `ec9e92c` y `80cf42d` (abajo).
- Hipótesis:
  1. Los jobs reciben la señal de cierre y la ignoran. **Descartada:** en `ec9e92c` el servidor no les enviaba ninguna señal.
  2. El servidor sale sin terminar ni esperar a sus jobs. **Confirmada** en `ec9e92c`.
  3. Con el cierre de `403e652`, ya no sobrevive ningún proceso. **Descartada:** un subproceso que ignora `SIGTERM` sigue vivo.
- Causa raíz: el problema tenía dos partes.
  1. En `ec9e92c`, el apagado detenía el event loop y salía sin atender a los jobs. Cada job tiene su propio grupo de procesos, así que tampoco le llegaba ninguna señal por otra vía.
  2. En `80cf42d`, el cierre solo vigila al proceso líder. Si el líder muere con `SIGTERM`, el servidor da el job por terminado y no escala a `SIGKILL`, aunque quede un subproceso vivo.
- Corrección / mitigación / aceptación de riesgo: **corregido.**
  1. `403e652` añadió el cierre controlado: `SIGTERM` a cada job, espera de hasta 5 s y escalamiento a `SIGKILL`.
  2. [job_manager.cpp](../../src/domain/job_manager.cpp): durante el cierre, `on_child_exit()` envía `SIGKILL` al resto del grupo cuando termina el líder.
- Prueba de regresión añadida: [verif/scripts/tc-009.sh](../../verif/scripts/tc-009.sh), contra el servidor real.
- Estado: CERRADO

## Resultados de la prueba

| Código | Resultado | Comprobaciones fallidas | Evidencia |
|---|---|---|---|
| `ec9e92c`, antes del cierre controlado | FAIL | 6 de 15 | `verif/results/run-2026-10-30-inc02-1-ec9e92c/` |
| `80cf42d` | FAIL | 1 de 15 | `verif/results/run-2026-10-30-inc02-2-80cf42d/` |
| `80cf42d` + corrección | **PASS** (2 corridas) | 0 de 15 | `verif/results/run-2026-10-30-inc02-3-correccion/` |

La corrección también pasó con los cambios de cancelación aún sin commit (`verif/results/run-2026-10-30-inc02-4-carpeta-de-trabajo/`).

El fallo que quedaba en `80cf42d`, con el comando exacto:

```bash
./build/jobrunner-cli /tmp/jobrunner.sock submit bash -c '(trap "" TERM; exec sleep 60) & wait'
# Tras apagar el servidor, sleep 60 seguía vivo.
```

## Qué comprueba TC-009

1. **Cierre normal:** con 3 jobs en ejecución y 1 en cola, los procesos desaparecen en menos de 4 s, el de la cola no se lanza y se elimina el socket.
2. **Job que ignora `SIGTERM`:** el servidor espera el plazo y escala a `SIGKILL` a los ~5 s, y queda registrado en la bitácora.
3. **Subproceso que ignora `SIGTERM`:** no queda ningún proceso del grupo.

Para ejecutarla:

```bash
BUILD_DIR=build verif/scripts/tc-009.sh
```
