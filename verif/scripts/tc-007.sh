#!/usr/bin/env bash
# TC-007 — Reinicio y recuperación (RF-12, RF-13, RNF-10, RNF-11, RNF-28, RNF-30, RNF-31)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-007.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-007; SOCK="/tmp/jobrunner-tc007.sock"
OUT="$RESULTS_DIR/tc-007-output.txt"
exec > >(tee "$OUT") 2>&1
cleanup() { pkill -f '^sleep 1(2[0-9])$' 2>/dev/null; cleanup_tc; }
trap cleanup EXIT

INTERRUPTED="interrumpido por reinicio del servicio"
SHUTDOWN="cancelado por cierre del servicio"
# Escribe una línea por milisegundo: hay transacciones en curso cuando llega el kill -9.
WRITER=$'import time\nfor i in range(10**6):\n    print(i, flush=True); time.sleep(0.001)'

dump_list() { cli list | python3 -c 'import sys,json
for j in json.load(sys.stdin)["jobs"]:
    print("  job %s %-8s %-10s %s" % (j["id"], j["command"], j["state"], j.get("diagnostic","")))' | tee -a "$RESULTS_DIR/tc-007-estados.txt"; }
integrity() { python3 -c 'import sqlite3,sys; print(sqlite3.connect(sys.argv[1]).execute("PRAGMA integrity_check").fetchone()[0])' "$DATA_DIR/jobrunner.db"; }

echo "== TC-007 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
: > "$RESULTS_DIR/tc-007-estados.txt"
echo "== Paso 1: pruebas unitarias [TC-007]"
"$BUILD_DIR/jobrunner_tests" "[TC-007]" | tee "$RESULTS_DIR/ctest-tc007.txt" | tail -4
check "pruebas unitarias [TC-007] en verde" test "${PIPESTATUS[0]}" -eq 0

echo "== Paso 2: Jobs en todos los estados antes de la caída (límite 3)"
start_tc paso2 3 || exit 2
OK=$(submit_id echo persistido); FA=$(submit_id sh -c 'echo err >&2; exit 4'); sleep 0.5
S1=$(submit_id sleep 120); W=$(submit_id python3 -c "$WRITER"); G=$(submit_id bash -c 'sleep 125 & wait')
Q=$(submit_id sleep 122); CA=$(submit_id sleep 123); cli cancel "$CA" >/dev/null
sleep 1.5
P_S1=$(job_pid "sleep 120"); P_W=$(job_pid "python3 -c"); GRANDCHILD=$(pgrep -f '^sleep 125$')
OK_REC=$(field "$OK" received_at); OK_FIN=$(field "$OK" finished_at)
echo "antes del kill -9:" | tee -a "$RESULTS_DIR/tc-007-estados.txt"; dump_list
check "estados antes de la caída: terminados, 3 RUNNING, 1 QUEUED y 1 CANCELED" lcheck "{j['id']: j['state'] for j in jobs}=={$OK:'SUCCEEDED', $FA:'FAILED', $S1:'RUNNING', $W:'RUNNING', $G:'RUNNING', $Q:'QUEUED', $CA:'CANCELED'}"
check "se identificaron los procesos de los Jobs en ejecución" test -n "$P_S1" -a -n "$P_W" -a -n "$GRANDCHILD"

echo "== Paso 3: caída abrupta del servidor (kill -9) mientras un Job escribe"
stop_tc KILL; sleep 0.5
check "el líder del Job sleep 120 murió con el servidor" pid_gone "$P_S1" 2
check "el Job que escribía murió con el servidor" pid_gone "$P_W" 2
if kill -0 "$GRANDCHILD" 2>/dev/null; then
    echo "el subproceso sleep 125 (pid $GRANDCHILD) sigue vivo tras la caída: lo debe terminar la recuperación"
fi
INT=$(integrity); echo "PRAGMA integrity_check: $INT"
check "la base no quedó dañada por la caída (RNF-11, RNF-28)" test "$INT" = "ok"

echo "== Paso 4: arranque sobre la misma base"
start_tc paso4 3 || exit 2
sleep 0.5
echo "tras reiniciar:" | tee -a "$RESULTS_DIR/tc-007-estados.txt"; dump_list
check "el Job SUCCEEDED conserva salida, código y tiempos (RF-12)" jcheck "$OK" "j['state']=='SUCCEEDED' and j['stdout']=='persistido\\n' and j.get('exit_code')==0 and j['received_at']=='$OK_REC' and j['finished_at']=='$OK_FIN'"
check "el Job FAILED conserva stderr y exit_code 4" jcheck "$FA" "j['state']=='FAILED' and j['stderr']=='err\\n' and j.get('exit_code')==4"
check "el Job CANCELED sigue CANCELED y nunca arrancó" jcheck "$CA" "j['state']=='CANCELED' and 'started_at' not in j"
check "los 3 Jobs interrumpidos pasan a FAILED con diagnóstico y finished_at (RF-13, RNF-10)" lcheck "len([j for j in jobs if j['id'] in ($S1, $W, $G) and j['state']=='FAILED' and j.get('diagnostic')=='$INTERRUPTED' and 'finished_at' in j])==3"
check "se conserva la salida que el Job escribió antes de la caída" jcheck "$W" "j['stdout'].startswith('0\\n1\\n')"
check "el subproceso huérfano sleep 125 fue terminado al arrancar (RNF-30)" pid_gone "$GRANDCHILD" 3
check "el Job en cola se reencoló y se ejecuta (RNF-31)" wait_state "$Q" RUNNING 3
check "solo el Job reencolado aparece RUNNING" lcheck "[j['id'] for j in jobs]==[$Q]" RUNNING
check "el Job RUNNING tiene un proceso vivo" test -n "$(job_pid 'sleep 122')"
N=$(submit_id sleep 124)
check "un Job nuevo recibe un id mayor que los anteriores ($N > $CA)" test "${N:-0}" -gt "$CA"
check "la bitácora registra la recuperación" grep -q "recuperacion completada: 1 reencolado(s), 3 interrumpido(s)" "$LOG"

echo "== Paso 5: cierre controlado y nuevo arranque"
M=$(submit_id sleep 126); P=$(submit_id sleep 127); sleep 0.3
check "con 3 Jobs en ejecución, el cuarto queda en cola" test "$(field "$P" state)" = "QUEUED"
stop_tc TERM
check "el servidor cerró con código 0" test "$SERVER_RC" -eq 0
start_tc paso5 3 || exit 2
sleep 0.5
echo "tras el cierre controlado y el arranque:" | tee -a "$RESULTS_DIR/tc-007-estados.txt"; dump_list
check "los Jobs que corrían quedan CANCELED con diagnóstico de cierre" lcheck "len([j for j in jobs if j['id'] in ($Q, $N, $M) and j['state']=='CANCELED' and j.get('diagnostic')=='$SHUTDOWN'])==3"
check "el Job en cola se conservó y arranca tras reiniciar" wait_state "$P" RUNNING 3
check "no queda vivo ningún proceso de los Jobs cancelados" bash -c "! pgrep -f '^sleep 12[246]$' >/dev/null"

stop_tc
finish_tc TC-007 22
