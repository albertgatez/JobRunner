# INC-01 — Proceso zombie con el servidor suspendido

- Fecha: 2026-10-30
- Síntomas: un job `sleep 15` terminó y siguió en `ps` como `[sleep] <defunct>`. No desapareció con `kill -9`, `kill` ni `SIGCHLD` al servidor.
- Evidencia recolectada (antes de modificar): historial de la terminal (`~/.bash_history`, líneas 991–1019) y una reproducción controlada (abajo). El historial no guarda salidas ni `Ctrl+Z`, así que la suspensión en la sesión original se deduce de que los síntomas coinciden.
- Hipótesis:
  1. El servidor no recoge a los hijos terminados. **Descartada:** sin suspenderlo no quedaron zombies en 20 jobs, y al reanudarlo recogió al zombie.
  2. Cerrar el servidor deja un zombie. **No se reprodujo:** deja un huérfano ([INC-02](02-jobs-huerfanos-al-apagar.md)).
  3. El servidor estaba suspendido cuando terminó el job. **Reproducida.**
- Causa raíz: con el servidor suspendido (`Ctrl+Z` o `SIGSTOP`) el event loop no corre, así que nadie llama a `waitpid`. Un zombie ya terminó y solo espera a que su padre lo recoja, por eso ninguna señal lo elimina. Es el comportamiento normal de Linux.
- Corrección / mitigación / aceptación de riesgo: **riesgo aceptado, sin cambios de código.** Se resuelve reanudando el servidor.
- Prueba de regresión añadida: ninguna. Propuesta abajo.
- Estado: EN INVESTIGACIÓN (falta la prueba de regresión)

## Reproducción

Salida real del 2026-10-30, con el código anterior a `403e652`:

```text
Job corriendo                       6773  PPID 6770  S   sleep 3
Servidor tras SIGSTOP               6770             T+  jobrunner-server
Job tras terminar                   6773  PPID 6770  Z   [sleep] <defunct>
Tras kill -9 y kill al zombie       6773  PPID 6770  Z   [sleep] <defunct>
Tras SIGCHLD al servidor            6773  PPID 6770  Z   [sleep] <defunct>
Tras kill -9 al servidor            el proceso ya no existe
```

Con `kill -CONT` en lugar de `kill -9`, el servidor recogió al zombie y el job quedó en `SUCCEEDED`.

Para repetirlo:

```bash
./build/jobrunner-server                                     # terminal 1
./build/jobrunner-cli /tmp/jobrunner.sock submit sleep 15    # terminal 2
# Terminal 1: Ctrl+Z antes de 15 s. Terminal 2, pasados los 15 s:
ps -eo pid,ppid,stat,cmd | awk '$3 ~ /Z/'
```

## Cómo resolverlo

| Situación | Acción |
|---|---|
| Servidor suspendido, misma terminal | `fg` |
| Servidor suspendido, otra terminal | `kill -CONT <PPID>` |
| No se puede reanudar (último recurso) | `kill -9 <PPID>` |

Evita `kill -9` si puedes. Según el código actual (no se ejecutó), se pierde el código de salida del job: sigue `RUNNING` en la base y al arrancar se marca `FAILED`, aunque hubiera terminado bien.

Para apagar el servidor se usa `Ctrl+C`, nunca `Ctrl+Z`.

## Prueba de regresión propuesta

Contra el servidor real: enviar `sleep 1`, `SIGSTOP` al servidor y esperar 1.5 s (el job debe estar en `Z`). Después, `SIGCONT` y esperar 0.5 s: el proceso ya no debe existir y el job debe estar en `SUCCEEDED`.

## Pendiente relacionado

`finished_at` guarda cuándo se recogió al hijo, no cuándo terminó: un `sleep 2` quedó registrado con 3.33 s. Corresponde a un Issue `defect`.
