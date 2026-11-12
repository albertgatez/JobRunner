#!/usr/bin/env bash
# TC-006 — Captura de stdout/stderr y código de salida (RF-11, RF-07)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-006.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-006; SOCK="/tmp/jobrunner-tc006.sock"
OUT="$RESULTS_DIR/tc-006-output.txt"
exec > >(tee "$OUT") 2>&1
trap cleanup_tc EXIT
STATUS_LOG="$RESULTS_DIR/tc-006-status.txt"; : > "$STATUS_LOG"

# Guarda el status completo como evidencia, recortando salidas largas.
save_status() {
    echo "--- $1 (job $2)" >> "$STATUS_LOG"
    cli status "$2" | python3 -c 'import sys,json
d=json.load(sys.stdin); j=d.get("job",{})
for k in ("stdout","stderr"):
    if len(j.get(k,""))>200: j[k]=j[k][:40]+"... (%d bytes)" % len(j[k])
print(json.dumps(d, indent=2, ensure_ascii=False))' >> "$STATUS_LOG"
}

echo "== TC-006 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
echo "== Paso 1: pruebas unitarias [TC-006]"
"$BUILD_DIR/jobrunner_tests" "[TC-006]" | tee "$RESULTS_DIR/ctest-tc006.txt" | tail -4
check "pruebas unitarias [TC-006] en verde" test "${PIPESTATUS[0]}" -eq 0

start_tc paso2 3 || exit 2

echo "== Paso 2: un Job que escribe en ambos flujos y sale con 3"
M=$(submit_id sh -c 'echo salida-1; echo error-1 >&2; echo salida-2; exit 3'); wait_terminal "$M" 5
save_status "ambos flujos" "$M"
check "stdout contiene solo lo escrito en stdout" jcheck "$M" "j['stdout']=='salida-1\\nsalida-2\\n'"
check "stderr contiene solo lo escrito en stderr" jcheck "$M" "j['stderr']=='error-1\\n'"
check "FAILED con exit_code 3" jcheck "$M" "j['state']=='FAILED' and j.get('exit_code')==3"

echo "== Paso 3: Jobs que escriben en un solo flujo"
O=$(submit_id sh -c 'printf solo-out'); E=$(submit_id sh -c 'echo solo-err >&2')
wait_terminal "$O" 5; wait_terminal "$E" 5
save_status "solo stdout" "$O"; save_status "solo stderr" "$E"
check "solo stdout: stdout capturado y stderr vacío" jcheck "$O" "j['stdout']=='solo-out' and j['stderr']==''"
check "solo stderr: stderr capturado y stdout vacío" jcheck "$E" "j['stdout']=='' and j['stderr']=='solo-err\\n'"
check "ambos SUCCEEDED con exit_code 0" bash -c "[ '$(field "$O" state)/$(field "$O" exit_code) $(field "$E" state)/$(field "$E" exit_code)' = 'SUCCEEDED/0 SUCCEEDED/0' ]"

echo "== Paso 4: escrituras alternadas en stdout y stderr"
I=$(submit_id sh -c 'for i in 1 2 3 4 5; do echo out$i; echo err$i >&2; done'); wait_terminal "$I" 5
save_status "alternado" "$I"
check "stdout conserva el orden y no trae líneas de stderr" jcheck "$I" "j['stdout']==''.join('out%d\\n' % i for i in range(1, 6))"
check "stderr conserva el orden y no trae líneas de stdout" jcheck "$I" "j['stderr']==''.join('err%d\\n' % i for i in range(1, 6))"

echo "== Paso 5: salida de varios bloques (200 000 B en stdout, 100 000 B en stderr)"
L=$(submit_id python3 -c 'import sys; sys.stdout.write("x" * 200000); sys.stderr.write("y" * 100000)')
wait_terminal "$L" 10
save_status "salida grande" "$L"
check "stdout completo (200 000 bytes)" jcheck "$L" "j['stdout']=='x' * 200000"
check "stderr completo (100 000 bytes)" jcheck "$L" "j['stderr']=='y' * 100000"

echo "== Paso 6: códigos de salida (RF-07)"
Z=$(submit_id sh -c 'exit 0'); X=$(submit_id sh -c 'exit 42'); K=$(submit_id sh -c 'kill -KILL $$')
N=$(submit_id comando-que-no-existe-tc006)
for id in $Z $X $K $N; do wait_terminal "$id" 5; done
save_status "exit 0" "$Z"; save_status "exit 42" "$X"; save_status "SIGKILL" "$K"; save_status "comando inexistente" "$N"
check "exit 0 -> SUCCEEDED con exit_code 0" jcheck "$Z" "j['state']=='SUCCEEDED' and j.get('exit_code')==0"
check "exit 42 -> FAILED con exit_code 42" jcheck "$X" "j['state']=='FAILED' and j.get('exit_code')==42"
check "terminado por SIGKILL -> FAILED con exit_signal 9" jcheck "$K" "j['state']=='FAILED' and j.get('exit_signal')==9"
echo "exit_code registrado para el Job terminado por señal: '$(field "$K" exit_code)' (el manual indica null)"
check "comando inexistente -> FAILED con exit_code 127" jcheck "$N" "j['state']=='FAILED' and j.get('exit_code')==127"

echo "== Paso 7: tiempos de un Job terminado (RF-07)"
check "received_at <= started_at <= finished_at" jcheck "$M" "j['received_at'] <= j['started_at'] <= j['finished_at']"
check "un Job terminado por señal también registra finished_at" jcheck "$K" "'finished_at' in j"

stop_tc
finish_tc TC-006 17
