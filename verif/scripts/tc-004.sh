#!/usr/bin/env bash
# TC-004 — Consultar y listar Jobs (RF-08, RF-09, RNF-05)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-004.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-004; SOCK="/tmp/jobrunner-tc004.sock"
OUT="$RESULTS_DIR/tc-004-output.txt"
exec > >(tee "$OUT") 2>&1
trap cleanup_tc EXIT

LOAD_JOBS=100

echo "== TC-004 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
echo "== Paso 1: pruebas unitarias [TC-004]"
"$BUILD_DIR/jobrunner_tests" "[TC-004]" | tee "$RESULTS_DIR/ctest-tc004.txt" | tail -4
check "pruebas unitarias [TC-004] en verde" test "${PIPESTATUS[0]}" -eq 0

echo "== Paso 2: preparar un Job en cada estado (límite 3)"
start_tc paso2 3 || exit 2
OK=$(submit_id echo hola); FA=$(submit_id false); sleep 0.5
R1=$(submit_id sleep 30); R2=$(submit_id sleep 31); R3=$(submit_id sleep 32)
Q=$(submit_id sleep 33); CA=$(submit_id sleep 34)
cli cancel "$CA" >/dev/null
echo "ids: SUCCEEDED=$OK FAILED=$FA RUNNING=$R1,$R2,$R3 QUEUED=$Q CANCELED=$CA"
cli list | json '"\n".join("  job %s %s %s -> %s" % (j["id"], j["command"], " ".join(j["args"]), j["state"]) for j in d["jobs"])'

echo "== Paso 3: status por ID (RF-08)"
R=$(cli status "$OK"); RC=$?; echo "$R"
check "status de un Job existente: rc del cliente = 0" test "$RC" -eq 0
check "devuelve id, comando y argumentos" jcheck "$OK" "j['id']==$OK and j['command']=='echo' and j['args']==['hola']"
check "devuelve estado SUCCEEDED, exit_code 0 y stdout" jcheck "$OK" "j['state']=='SUCCEEDED' and j.get('exit_code')==0 and j['stdout']=='hola\\n'"
check "devuelve received_at, started_at y finished_at" jcheck "$OK" "all(k in j for k in ('received_at','started_at','finished_at'))"
check "un Job RUNNING tiene started_at y no finished_at" jcheck "$R1" "j['state']=='RUNNING' and 'started_at' in j and 'finished_at' not in j"
check "un Job QUEUED no tiene started_at" jcheck "$Q" "j['state']=='QUEUED' and 'started_at' not in j"
R=$(cli status 999999); RC=$?; echo "id desconocido: $R (rc=$RC)"
check "id desconocido: ok=false con mensaje" rcheck "$R" "d['ok'] is False and d.get('error')"
check "id desconocido: rc del cliente != 0" test "$RC" -ne 0

echo "== Paso 4: list y filtro por estado (RF-09)"
check "list sin filtro devuelve los 7 Jobs" lcheck "sorted(j['id'] for j in jobs)==sorted([$OK,$FA,$R1,$R2,$R3,$Q,$CA])"
for pair in "SUCCEEDED:$OK" "FAILED:$FA" "RUNNING:$R1,$R2,$R3" "QUEUED:$Q" "CANCELED:$CA"; do
    ST=${pair%%:*}; IDS=${pair#*:}
    check "list $ST devuelve solo los Jobs $IDS" lcheck "all(j['state']=='$ST' for j in jobs) and sorted(j['id'] for j in jobs)==sorted([$IDS])" "$ST"
done
R=$(cli list NO_EXISTE); RC=$?; echo "estado inválido: $R (rc=$RC)"
check "estado inválido: ok=false con mensaje" rcheck "$R" "d['ok'] is False and 'NO_EXISTE' in d.get('error','')"
check "estado inválido: rc del cliente != 0" test "$RC" -ne 0
# Primero el de la cola, para que no arranque al liberarse un cupo.
for id in $Q $R1 $R2 $R3; do cli cancel "$id" >/dev/null; done

echo "== Paso 5: status con al menos 100 Jobs almacenados (RNF-05)"
for _ in $(seq 1 50); do lcheck "len(jobs)==0" RUNNING && break; sleep 0.1; done
RETRIES=0
for i in $(seq 1 $LOAD_JOBS); do
    # La cola admite 10 Jobs: si está llena se reintenta, el rechazo es esperado.
    for _ in $(seq 1 100); do
        [ -n "$(submit_id true "$i")" ] && break
        RETRIES=$((RETRIES+1)); sleep 0.1
    done
done
echo "Jobs de carga enviados: $LOAD_JOBS (reintentos por cola llena: $RETRIES)"
for _ in $(seq 1 300); do lcheck "not any(j['state'] in ('RUNNING','QUEUED') for j in jobs)" && break; sleep 0.1; done
TOTAL=$(cli list | json 'len(d["jobs"])')
check "hay al menos 100 Jobs almacenados ($TOTAL)" test "$TOTAL" -ge 100
check "los $LOAD_JOBS Jobs de carga terminaron SUCCEEDED" lcheck "len([j for j in jobs if j['command']=='true'])==$LOAD_JOBS" SUCCEEDED

TIMES="$RESULTS_DIR/tc-004-tiempos.txt"; : > "$TIMES"
MAX_MS=0
for id in 1 $((TOTAL / 2)) "$TOTAL"; do
    T0=$(date +%s%N); cli status "$id" >/dev/null; T1=$(date +%s%N)
    MS=$(( (T1 - T0) / 1000000 ))
    echo "status $id con $TOTAL Jobs: $MS ms" | tee -a "$TIMES"
    [ "$MS" -gt "$MAX_MS" ] && MAX_MS=$MS
done
T0=$(date +%s%N); cli list >/dev/null; T1=$(date +%s%N)
echo "list con $TOTAL Jobs: $(( (T1 - T0) / 1000000 )) ms (informativo)" | tee -a "$TIMES"
check "status responde en <= 1000 ms (máximo: $MAX_MS ms, incluye arrancar el cliente)" test "$MAX_MS" -le 1000

stop_tc
finish_tc TC-004 20
