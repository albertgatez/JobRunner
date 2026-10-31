#!/usr/bin/env bash
# TC-009 — Cierre controlado sin procesos huérfanos (RF-15, RNF-25, RNF-30). Regresión de INC-02.
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-009.sh
source "$(dirname "$0")/common.sh"
SOCK="/tmp/jobrunner-tc009.sock"
OUT="$RESULTS_DIR/tc-009-output.txt"
exec > >(tee "$OUT") 2>&1

DATA_DIR=$(mktemp -d)
GROUPS_TO_CLEAN=()
cleanup() {
    for g in "${GROUPS_TO_CLEAN[@]}"; do kill -9 -- "-$g" 2>/dev/null; done
    kill -9 "$SERVER_PID" 2>/dev/null
    rm -rf "$DATA_DIR"
    rm -f "$SOCK"
}
trap cleanup EXIT

# Arranca el servidor con una base vacía para no recuperar jobs de otras corridas.
start_clean() {
    LOG="$RESULTS_DIR/tc-009-$1-server.log"
    rm -f "$SOCK" "$DATA_DIR"/*
    "$SERVER_BIN" "$SOCK" 3 --data-dir "$DATA_DIR" >"$LOG" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "ERROR: el servidor no abrió el socket" >&2; exit 2
}

# Espera la salida del servidor como máximo 15 s. Deja RC y ELAPSED desde el instante T0.
wait_exit() {
    for _ in $(seq 1 150); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.1; done
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "el servidor no terminó en 15 s"; kill -9 "$SERVER_PID"
    fi
    wait "$SERVER_PID"; RC=$?
    ELAPSED=$(python3 -c "import time,sys; print(round(time.time()-float(sys.argv[1]),1))" "$T0")
    sleep 0.3
    echo "código de salida: $RC, tiempo de cierre: ${ELAPSED}s"
}
between() { python3 -c "import sys; sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)" "$@"; }
group_gone() { ! pgrep -g "$1" >/dev/null; }
all_gone() { local p; for p in "$@"; do kill -0 "$p" 2>/dev/null && return 1; done; return 0; }
job_pid() { ps --ppid "$SERVER_PID" -o pid=,cmd= | awk -v c="$1" 'index($0,c){print $1; exit}'; }

echo "== TC-009 run-id=$RUN_ID binarios=${BIN_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null)}"

echo "== Paso 1: SIGINT con 3 jobs en ejecución y 1 en cola"
start_clean paso1
for s in 60 61 62 63; do cli submit sleep "$s" >/dev/null; done; sleep 0.3
P1=$(job_pid "sleep 60"); P2=$(job_pid "sleep 61"); P3=$(job_pid "sleep 62")
GROUPS_TO_CLEAN+=($P1 $P2 $P3)
ps --ppid "$SERVER_PID" -o pid,pgid,stat,cmd
check "se identificaron los 3 jobs en ejecución" test -n "$P1" -a -n "$P2" -a -n "$P3"
T0=$(date +%s.%N); kill -INT "$SERVER_PID"; wait_exit
ps -eo pid,ppid,pgid,stat,cmd | awk '$NF ~ /^6[0-3]$/ && /sleep/'
check "código de salida 0" test "$RC" -eq 0
check "cierre antes del plazo de gracia (< 4 s)" between "$ELAPSED" 0 4
check "los 3 jobs ya no existen (RNF-30)" all_gone $P1 $P2 $P3
check "el job en cola nunca se lanzó" test "$(grep -c 'en ejecucion' "$LOG")" -eq 3
check "el socket se eliminó (RNF-25)" test ! -e "$SOCK"

echo "== Paso 2: SIGTERM con un job que ignora SIGTERM -> escala a SIGKILL"
start_clean paso2
cli submit bash -c 'trap "" TERM; sleep 60; true' >/dev/null; sleep 0.5
P=$(job_pid "bash -c"); GROUPS_TO_CLEAN+=($P)
T0=$(date +%s.%N); kill -TERM "$SERVER_PID"; sleep 1
kill -0 "$SERVER_PID" 2>/dev/null; ALIVE=$?
check "el servidor sigue vivo durante el plazo de gracia" test "$ALIVE" -eq 0
check "el job sigue vivo durante el plazo de gracia" bash -c "pgrep -g '$P' >/dev/null"
wait_exit
check "el cierre esperó el plazo de gracia (4-8 s)" between "$ELAPSED" 4 8
check "código de salida 0" test "$RC" -eq 0
check "no queda ningún proceso del grupo" group_gone "$P"
check "la bitácora registra el SIGKILL" grep -q 'SIGKILL' "$LOG"

echo "== Paso 3: el líder muere con SIGTERM pero un subproceso lo ignora"
start_clean paso3
cli submit bash -c '(trap "" TERM; exec sleep 60) & wait' >/dev/null; sleep 0.5
P=$(job_pid "bash -c"); GROUPS_TO_CLEAN+=($P)
pgrep -g "$P" -a
check "el job tiene un subproceso en su grupo" test "$(pgrep -g "$P" | wc -l)" -ge 2
T0=$(date +%s.%N); kill -INT "$SERVER_PID"; wait_exit
pgrep -g "$P" -a
check "cierre antes del plazo de gracia (< 4 s)" between "$ELAPSED" 0 4
check "no queda ningún proceso del grupo (RNF-30)" group_gone "$P"

EXPECTED_CHECKS=15   # comprobaciones que ejecuta el script completo (si cambia, actualizar)
echo "== Comprobaciones ejecutadas: $CHECKS (esperadas: $EXPECTED_CHECKS) | fallos: $FAILS"
if [ "$CHECKS" -ne "$EXPECTED_CHECKS" ]; then
    echo "[FAIL] el número de comprobaciones ejecutadas no coincide con el esperado"; FAILS=$((FAILS+1))
fi
if [ "$FAILS" -eq 0 ]; then echo "RESULTADO TC-009: PASS"; exit 0; fi
echo "RESULTADO TC-009: FAIL"; exit 1
