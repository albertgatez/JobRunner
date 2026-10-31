#!/usr/bin/env bash
# TC-008 — Aislamiento de fallo: error al crear el proceso de un Job (RNF-08, RNF-09).
# Provoca fallos reales de fork() bajando RLIMIT_NPROC solo al servidor.
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-008.sh
source "$(dirname "$0")/common.sh"
SOCK="/tmp/jobrunner-tc008.sock"
OUT="$RESULTS_DIR/tc-008-output.txt"
exec > >(tee "$OUT") 2>&1

DATA_DIR=$(mktemp -d)
SOFT_NPROC=$(ulimit -Su)
cleanup() {
    kill -9 "$SERVER_PID" 2>/dev/null
    rm -rf "$DATA_DIR"
    rm -f "$SOCK"
}
trap cleanup EXIT

# Arranca el servidor. El prefijo opcional (LIMIT_AT_START=1) lo inicia ya sin poder crear procesos.
start() {
    LOG="$RESULTS_DIR/tc-008-$1-server.log"
    rm -f "$SOCK"
    if [ -n "$LIMIT_AT_START" ]; then
        bash -c 'ulimit -Su 1; exec "$@"' _ "$SERVER_BIN" "$SOCK" "$2" --data-dir "$DATA_DIR" >"$LOG" 2>&1 &
    else
        "$SERVER_BIN" "$SOCK" "$2" --data-dir "$DATA_DIR" >"$LOG" 2>&1 &
    fi
    SERVER_PID=$!
    for _ in $(seq 1 30); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    return 1
}
stop() { kill -INT "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; }
block_fork()   { prlimit --pid "$SERVER_PID" --nproc=1:; }
unblock_fork() { prlimit --pid "$SERVER_PID" --nproc="$SOFT_NPROC":; }
alive() { kill -0 "$SERVER_PID" 2>/dev/null; }

# Lee un campo de una respuesta JSON sin fallar si el servidor no responde.
get() { python3 -c 'import sys,json
try: d=json.load(sys.stdin)
except Exception: sys.exit(0)
for k in sys.argv[1:]:
    d=d.get(k) if isinstance(d,dict) else None
print("" if d is None else d)' "$@"; }
submit_id() { cli submit "$@" 2>/dev/null | get job_id; }
job_field() { cli status "$1" 2>/dev/null | get job "$2"; }
contains() { [[ "$1" == *"$2"* ]]; }

echo "== TC-008 run-id=$RUN_ID binarios=${BIN_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null)}"

echo "== Paso 1: fork falla con cupo libre"
start paso1 3 || { echo "ERROR: el servidor no arrancó"; exit 2; }
block_fork
R=$(cli submit echo uno 2>/dev/null); echo "respuesta: $R"
ID=$(echo "$R" | get job_id)
check "el cliente recibe el id del job" test -n "$ID"
S=$(job_field "$ID" state); D=$(job_field "$ID" diagnostic); echo "estado: $S, diagnóstico: $D"
check "el job queda FAILED" test "$S" = "FAILED"
check "el diagnóstico indica el fallo de fork" contains "$D" "fork"
check "tiene finished_at" test -n "$(job_field "$ID" finished_at)"
check "no tiene started_at" test -z "$(job_field "$ID" started_at)"
check "no queda en la lista de QUEUED" test "$(cli list QUEUED 2>/dev/null | grep -c '"id"')" -eq 0
R2=$(cli submit echo uno 2>/dev/null); echo "reenvío inmediato: $R2"
ID2=$(echo "$R2" | get job_id)
check "el reenvío crea un job nuevo, no un duplicado" bash -c "[[ -n '$ID2' && '$ID2' != '$ID' ]]"
check "el reenvío no se marca como duplicado" test "$(echo "$R2" | get was_duplicate)" = "False"
unblock_fork
ID3=$(submit_id echo dos); sleep 0.5
check "al recuperarse el sistema, un job nuevo funciona" test "$(job_field "$ID3" state)" = "SUCCEEDED"
stop

echo "== Paso 2: fork falla al despachar desde la cola"
rm -f "$DATA_DIR"/*
start paso2 1 || { echo "ERROR: el servidor no arrancó"; exit 2; }
A=$(submit_id sleep 1); B=$(submit_id echo cola1); C=$(submit_id echo cola2)
echo "jobs: $A (en ejecución), $B y $C (en cola)"
check "los dos últimos están en cola" test "$(job_field "$B" state)$(job_field "$C" state)" = "QUEUEDQUEUED"
block_fork; sleep 1.5
check "el servidor sigue vivo" alive
check "el job en ejecución terminó bien" test "$(job_field "$A" state)" = "SUCCEEDED"
SB=$(job_field "$B" state); SC=$(job_field "$C" state); echo "estados en cola tras el fallo: $SB $SC"
check "los jobs de la cola quedan FAILED" test "$SB$SC" = "FAILEDFAILED"
check "con diagnóstico" contains "$(job_field "$B" diagnostic)" "fork"
if alive; then unblock_fork; fi
ID4=$(submit_id echo tres); sleep 0.5
check "el cupo no quedó ocupado: un job nuevo funciona" test "$(job_field "$ID4" state)" = "SUCCEEDED"
stop

echo "== Paso 3: fork falla durante la recuperación al arrancar"
rm -f "$DATA_DIR"/*
start paso3 1 || { echo "ERROR: el servidor no arrancó"; exit 2; }
A=$(submit_id sleep 30); B=$(submit_id echo recuperar)
check "hay un job en cola antes del cierre" test "$(job_field "$B" state)" = "QUEUED"
stop
LIMIT_AT_START=1 start paso3-reinicio 1; STARTED=$?
check "el servidor arranca aunque no pueda crear procesos" test "$STARTED" -eq 0
check "el job de la cola queda FAILED" test "$(job_field "$B" state)" = "FAILED"
stop

EXPECTED_CHECKS=18   # comprobaciones que ejecuta el script completo (si cambia, actualizar)
echo "== Comprobaciones ejecutadas: $CHECKS (esperadas: $EXPECTED_CHECKS) | fallos: $FAILS"
if [ "$CHECKS" -ne "$EXPECTED_CHECKS" ]; then
    echo "[FAIL] el número de comprobaciones ejecutadas no coincide con el esperado"; FAILS=$((FAILS+1))
fi
if [ "$FAILS" -eq 0 ]; then echo "RESULTADO TC-008: PASS"; exit 0; fi
echo "RESULTADO TC-008: FAIL"; exit 1
