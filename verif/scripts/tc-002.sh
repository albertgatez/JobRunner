#!/usr/bin/env bash
# TC-002 — Límite de concurrencia y cola FIFO (RF-03, RF-05, RNF-07)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-002.sh
#
# Requiere la tarea técnica "Límite de concurrencia y cola" (Issue 4). Mientras no exista,
# este caso FALLA: es el resultado esperado y queda como evidencia de lo pendiente.
# El servidor aún no tiene configuración de max_concurrent_jobs; el caso asume un límite de 3.
# Cuando exista el mecanismo de configuración, aplicarlo en start_server (common.sh).
source "$(dirname "$0")/common.sh"
TC_NAME=tc-002; SOCK="/tmp/jobrunner-tc002.sock"
OUT="$RESULTS_DIR/tc-002-output.txt"
exec > >(tee "$OUT") 2>&1

MAX_JOBS=3
EXPECTED_CHECKS=10

count_state() { cli list | json "sum(1 for j in d['jobs'] if j['state']=='$1')"; }
state_of()    { cli status "$1" | json 'd["job"]["state"]'; }
children()    { ps --ppid "$SERVER_PID" -o cmd= | grep -c '^sleep'; }
is_terminal() { case "$1" in SUCCEEDED|FAILED|CANCELED) return 0;; *) return 1;; esac; }

echo "== TC-002 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null) límite esperado=$MAX_JOBS"
echo "== Paso 1: iniciar servidor"
start_server || exit 2

echo "== Paso 2: enviar 5 Jobs (sleep 6..10, distintos para evitar la ventana de duplicados) y listar"
for n in 6 7 8 9 10; do cli submit sleep $n >/dev/null; done
sleep 0.5
RUN_N=$(count_state RUNNING); Q_N=$(count_state QUEUED)
echo "RUNNING=$RUN_N QUEUED=$Q_N"
cli list | json '"\n".join("  job %s sleep %s -> %s" % (j["id"], j["args"][0], j["state"]) for j in d["jobs"])'
check "hay exactamente $MAX_JOBS Jobs en RUNNING" test "$RUN_N" = "$MAX_JOBS"
check "hay exactamente 2 Jobs en QUEUED" test "$Q_N" = "2"
FIRST3=$(for i in 1 2 3; do state_of $i; done | sort -u | tr '\n' ' ')
check "los Jobs 1, 2 y 3 (los primeros en llegar) son los que corren" test "$FIRST3" = "RUNNING "

echo "== Paso 3: procesos hijos simultáneos"
CH=$(children); echo "procesos sleep hijos del servidor: $CH"
ps --ppid "$SERVER_PID" -o pid,stat,cmd > "$RESULTS_DIR/tc-002-ps-inicial.txt"
check "hay exactamente $MAX_JOBS procesos hijos" test "$CH" = "$MAX_JOBS"

echo "== Paso 4: cancelar el Job 1 (RUNNING) -> el Job 4 debe pasar a RUNNING y el 5 seguir en QUEUED"
cli cancel 1 >/dev/null; sleep 1
S1=$(state_of 1); S4=$(state_of 4); S5=$(state_of 5)
echo "job1=$S1 job4=$S4 job5=$S5"
check "el Job 1 queda CANCELED" test "$S1" = "CANCELED"
check "el Job 4 (el más antiguo en cola) pasa a RUNNING" test "$S4" = "RUNNING"
check "el Job 5 sigue en QUEUED" test "$S5" = "QUEUED"

echo "== Paso 5: muestreo cada 0.5 s hasta que terminen todos (máx. 40 s)"
MAXSEEN=0; SAMPLES="$RESULTS_DIR/tc-002-muestreo.txt"; : > "$SAMPLES"
for _ in $(seq 1 80); do
    C=$(children); [ "$C" -gt "$MAXSEEN" ] && MAXSEEN=$C
    echo "$(date +%T.%N | cut -c1-12) hijos=$C" >> "$SAMPLES"
    ALL=1; for i in 1 2 3 4 5; do is_terminal "$(state_of $i)" || ALL=0; done
    [ "$ALL" = 1 ] && break
    sleep 0.5
done
echo "máximo de procesos simultáneos observado: $MAXSEEN"
check "nunca hubo más de $MAX_JOBS procesos simultáneos" test "$MAXSEEN" -le "$MAX_JOBS"
check "los 5 Jobs llegan a un estado terminal" test "$ALL" = 1
cli list | json '"\n".join("  job %s sleep %s -> %s" % (j["id"], j["args"][0], j["state"]) for j in d["jobs"])'
S5_START=$(cli status 5 | json '"started_at" in d["job"]')
check "el Job 5 llegó a ejecutarse (tiene started_at)" test "$S5_START" = "True"

pkill -P "$SERVER_PID" 2>/dev/null
stop_server
echo "== Comprobaciones ejecutadas: $CHECKS (esperadas: $EXPECTED_CHECKS) | fallos: $FAILS"
if [ "$CHECKS" -ne "$EXPECTED_CHECKS" ]; then
    echo "[FAIL] el número de comprobaciones ejecutadas no coincide con el esperado"; FAILS=$((FAILS+1))
fi
if [ "$FAILS" -eq 0 ]; then echo "RESULTADO TC-002: PASS"; exit 0; fi
echo "RESULTADO TC-002: FAIL"; exit 1
