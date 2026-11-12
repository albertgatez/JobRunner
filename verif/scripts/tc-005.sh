#!/usr/bin/env bash
# TC-005 — Cancelar Jobs en cola y en ejecución (RF-10; el paso 6 cubre el escalamiento de RF-30)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-005.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-005; SOCK="/tmp/jobrunner-tc005.sock"
OUT="$RESULTS_DIR/tc-005-output.txt"
exec > >(tee "$OUT") 2>&1
TERM_GROUP=""
cleanup() { [ -n "$TERM_GROUP" ] && kill -9 -- "-$TERM_GROUP" 2>/dev/null; cleanup_tc; }
trap cleanup EXIT

# ok_rc <respuesta> <rc>: la respuesta trae ok=true y el cliente salió con 0.
ok_rc() { [ "$2" -eq 0 ] && rcheck "$1" "d['ok'] is True"; }

echo "== TC-005 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
echo "== Paso 1: pruebas unitarias [TC-005]"
"$BUILD_DIR/jobrunner_tests" "[TC-005]" | tee "$RESULTS_DIR/ctest-tc005.txt" | tail -4
check "pruebas unitarias [TC-005] en verde" test "${PIPESTATUS[0]}" -eq 0

echo "== Paso 2: límite 1 -> A en ejecución, B y C en cola"
start_tc paso2 1 || exit 2
A=$(submit_id sleep 30); B=$(submit_id sleep 31); C=$(submit_id sleep 32); sleep 0.3
PA=$(job_pid "sleep 30")
S0="$(field "$A" state) $(field "$B" state) $(field "$C" state)"; echo "A=$A B=$B C=$C -> $S0 (pid de A: $PA)"
check "estado inicial: A RUNNING, B y C QUEUED" test "$S0" = "RUNNING QUEUED QUEUED"
check "se identificó el proceso de A" test -n "$PA"
ps --ppid "$SERVER_PID" -o pid,pgid,stat,cmd | tee "$RESULTS_DIR/tc-005-ps-antes.txt"

echo "== Paso 3: cancelar B (QUEUED)"
R=$(cli cancel "$B"); RC=$?; echo "$R (rc=$RC)"
check "cancel de un Job en cola: ok=true y rc=0" ok_rc "$R" "$RC"
check "B queda CANCELED con finished_at y sin started_at" jcheck "$B" "j['state']=='CANCELED' and 'finished_at' in j and 'started_at' not in j"
check "A sigue RUNNING y C sigue QUEUED" test "$(field "$A" state) $(field "$C" state)" = "RUNNING QUEUED"
check "nunca se creó un proceso para B" test -z "$(job_pid 'sleep 31')"

echo "== Paso 4: cancelar A (RUNNING)"
R=$(cli cancel "$A"); RC=$?; echo "$R (rc=$RC)"
SA=$(field "$A" state); echo "estado de A justo después: $SA"
check "cancel de un Job en ejecución: ok=true y rc=0" ok_rc "$R" "$RC"
check "A se reporta CANCELED de inmediato" test "$SA" = "CANCELED"
check "el proceso de A (pid $PA) termina en menos de 3 s" pid_gone "$PA" 3
check "C, el siguiente en cola, pasa a RUNNING" wait_state "$C" RUNNING 3
check "B sigue CANCELED y nunca arrancó" jcheck "$B" "j['state']=='CANCELED' and 'started_at' not in j"
ps --ppid "$SERVER_PID" -o pid,pgid,stat,cmd | tee "$RESULTS_DIR/tc-005-ps-despues.txt"

echo "== Paso 5: cancelaciones sin efecto"
cli cancel "$C" >/dev/null
for _ in $(seq 1 30); do lcheck "len(jobs)==0" RUNNING && break; sleep 0.1; done
F=$(submit_id echo fin); wait_terminal "$F" 3
R=$(cli cancel "$F"); RC=$?; echo "cancel de un Job terminado: $R (rc=$RC)"
check "cancelar un Job terminado: ok=true y rc=0" ok_rc "$R" "$RC"
check "el Job terminado conserva SUCCEEDED y exit_code 0" jcheck "$F" "j['state']=='SUCCEEDED' and j.get('exit_code')==0"
FIN_A=$(field "$A" finished_at)
R=$(cli cancel "$A"); RC=$?; echo "segundo cancel de A: $R (rc=$RC)"
check "cancelar dos veces el mismo Job: ok=true y rc=0" ok_rc "$R" "$RC"
check "el segundo cancel no cambia el estado ni finished_at" jcheck "$A" "j['state']=='CANCELED' and j['finished_at']=='$FIN_A'"
R=$(cli cancel 999999); RC=$?; echo "id desconocido: $R (rc=$RC)"
check "cancelar un id desconocido: ok=false con mensaje" rcheck "$R" "d['ok'] is False and d.get('error')"
check "cancelar un id desconocido: rc del cliente != 0" test "$RC" -ne 0

echo "== Paso 6: Job que ignora SIGTERM -> SIGKILL a los 5 s (RF-30)"
T=$(submit_id bash -c 'trap "" TERM; sleep 60; true'); sleep 0.5
TERM_GROUP=$(job_pid "bash -c"); echo "Job $T, grupo $TERM_GROUP"
check "se identificó el proceso del Job" test -n "$TERM_GROUP"
T0=$(date +%s.%N); cli cancel "$T" >/dev/null
check "se reporta CANCELED de inmediato" test "$(field "$T" state)" = "CANCELED"
sleep 2
check "2 s después el grupo sigue vivo (ignoró SIGTERM)" bash -c "pgrep -g '$TERM_GROUP' >/dev/null"
for _ in $(seq 1 80); do pgrep -g "$TERM_GROUP" >/dev/null || break; sleep 0.1; done
EL=$(python3 -c "import time,sys; print(round(time.time()-float(sys.argv[1]),1))" "$T0")
echo "el grupo desapareció a los ${EL} s"
check "no queda ningún proceso del grupo" bash -c "! pgrep -g '$TERM_GROUP' >/dev/null"
check "el escalamiento ocurrió entre 4 y 8 s después del cancel" between "$EL" 4 8
check "la bitácora registra el SIGKILL del Job $T" grep -q "job=$T .*SIGKILL" "$LOG"
check "el Job sigue CANCELED tras el SIGKILL" test "$(field "$T" state)" = "CANCELED"

stop_tc
finish_tc TC-005 25
