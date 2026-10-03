#!/usr/bin/env bash
# TC-003 — Estados, código de salida y cancelación (RF-06, RF-07, RNF-27)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-003.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-003; SOCK="/tmp/jobrunner-tc003.sock"
OUT="$RESULTS_DIR/tc-003-output.txt"
exec > >(tee "$OUT") 2>&1

state()  { cli status "$1" | json 'd["job"]["state"]'; }
excode() { cli status "$1" | json 'd["job"].get("exit_code")'; }

echo "== TC-003 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
echo "== Paso 1: pruebas unitarias [TC-003]"
"$BUILD_DIR/jobrunner_tests" "[TC-003]" | tee "$RESULTS_DIR/ctest-tc003.txt" | tail -4
check "pruebas unitarias [TC-003] en verde" test "${PIPESTATUS[0]}" -eq 0

start_server || exit 2

echo "== Paso 2: submit sleep 2 -> RUNNING"
ID=$(cli submit sleep 2 | json 'd["job_id"]'); ID_OK=$ID
S=$(state "$ID"); echo "estado inmediato: $S"
check "RUNNING justo después del submit" test "$S" = "RUNNING"

echo "== Paso 3: tras 3 s -> SUCCEEDED, exit_code 0"
sleep 3
S=$(state "$ID"); E=$(excode "$ID"); echo "estado: $S exit_code: $E"
check "SUCCEEDED" test "$S" = "SUCCEEDED"
check "exit_code 0" test "$E" = "0"

echo "== Paso 4: submit false -> FAILED, exit_code 1"
ID=$(cli submit false | json 'd["job_id"]'); sleep 1
S=$(state "$ID"); E=$(excode "$ID"); echo "estado: $S exit_code: $E"
check "FAILED" test "$S" = "FAILED"
check "exit_code 1" test "$E" = "1"

echo "== Paso 5: submit sleep 30 + cancel -> CANCELED y proceso muerto"
ID=$(cli submit sleep 30 | json 'd["job_id"]'); sleep 0.5
echo "hijos antes del cancel:"; ps --ppid "$SERVER_PID" -o pid,stat,cmd | tee "$RESULTS_DIR/ps-antes-cancel.txt"
CPID=$(ps --ppid "$SERVER_PID" -o pid=,cmd= | awk '/sleep 30/{print $1; exit}')
cli cancel "$ID"; sleep 1
S=$(state "$ID"); echo "estado: $S"
echo "hijos después del cancel:"; ps --ppid "$SERVER_PID" -o pid,stat,cmd | tee "$RESULTS_DIR/ps-despues-cancel.txt"
check "CANCELED" test "$S" = "CANCELED"
check "se identificó el proceso hijo de sleep 30 antes de cancelar" test -n "$CPID"
check "el proceso (pid $CPID) ya no existe" bash -c "[[ -n '$CPID' ]] && ! kill -0 '$CPID' 2>/dev/null"
sleep 1; S2=$(state "$ID")
check "sigue CANCELED tras terminar el proceso (RNF-27)" test "$S2" = "CANCELED"

echo "== Paso 6: tiempos received_at/started_at/finished_at en status"
ISO='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z$'
for LABEL_ID in "SUCCEEDED:$ID_OK" "CANCELED:$ID"; do
    LABEL=${LABEL_ID%%:*}; JID=${LABEL_ID##*:}
    R=$(cli status "$JID"); echo "$LABEL (job $JID):"
    echo "$R" | json '"  received_at=%s started_at=%s finished_at=%s" % (d["job"].get("received_at"), d["job"].get("started_at"), d["job"].get("finished_at"))'
    RA=$(echo "$R" | json 'd["job"].get("received_at","")'); SA=$(echo "$R" | json 'd["job"].get("started_at","")'); FA=$(echo "$R" | json 'd["job"].get("finished_at","")')
    check "$LABEL: los tres tiempos existen con formato ISO 8601" bash -c "[[ '$RA' =~ $ISO && '$SA' =~ $ISO && '$FA' =~ $ISO ]]"
    check "$LABEL: received_at <= started_at <= finished_at" bash -c "[[ -n '$RA' && -n '$SA' && -n '$FA' && ! '$RA' > '$SA' && ! '$SA' > '$FA' ]]"
done
D=$(python3 -c "
from datetime import datetime
f=lambda t: datetime.strptime(t,'%Y-%m-%dT%H:%M:%S.%fZ')
import json,sys
j=json.loads(sys.argv[1])['job']
try: print(round((f(j['finished_at'])-f(j['started_at'])).total_seconds(),1))
except KeyError: print(-1)" "$(cli status "$ID_OK")")
echo "duración medida del Job sleep 2: ${D}s"
check "la duración de sleep 2 es coherente (1.9 - 3.0 s)" python3 -c "import sys; sys.exit(0 if 1.9 <= float('$D') <= 3.0 else 1)"
Q=$(cli submit sleep 5 | json 'd["job_id"]')
RQ=$(cli status "$Q" | json '"started_at" in d["job"] and "finished_at" not in d["job"]')
check "un Job en RUNNING tiene started_at y no tiene finished_at" test "$RQ" = "True"
cli cancel "$Q" >/dev/null

stop_server
EXPECTED_CHECKS=16   # comprobaciones que ejecuta el script completo (si cambia, actualizar)
echo "== Comprobaciones ejecutadas: $CHECKS (esperadas: $EXPECTED_CHECKS) | fallos: $FAILS"
if [ "$CHECKS" -ne "$EXPECTED_CHECKS" ]; then
    echo "[FAIL] el número de comprobaciones ejecutadas no coincide con el esperado"; FAILS=$((FAILS+1))
fi
if [ "$FAILS" -eq 0 ]; then echo "RESULTADO TC-003: PASS"; exit 0; fi
echo "RESULTADO TC-003: FAIL"; exit 1
