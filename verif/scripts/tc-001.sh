#!/usr/bin/env bash
# TC-001 — Envío de Jobs y manejo de solicitudes inválidas (RF-01, RF-02)
# Uso (desde la raíz): BUILD_DIR=build verif/scripts/tc-001.sh
source "$(dirname "$0")/common.sh"
TC_NAME=tc-001; SOCK="/tmp/jobrunner-tc001.sock"
OUT="$RESULTS_DIR/tc-001-output.txt"
exec > >(tee "$OUT") 2>&1

echo "== TC-001 run-id=$RUN_ID commit=$(git rev-parse --short HEAD 2>/dev/null)"
echo "== Paso 1: iniciar servidor"
start_server || exit 2
check "el socket existe" test -S "$SOCK"

echo "== Paso 2: submit echo hola"
R2=$(cli submit echo hola); RC2=$?; echo "$R2"; echo "rc=$RC2"
ID2=$(echo "$R2" | json 'd["job_id"]')
check "ok=true"                    test "$(echo "$R2" | json 'd["ok"]')" = "True"
check "job_id numérico"            test -n "$ID2"
check "rc del cliente = 0"         test "$RC2" -eq 0

echo "== Paso 3: submit echo adios (distinto, evita ventana de duplicados)"
R3=$(cli submit echo adios); echo "$R3"
ID3=$(echo "$R3" | json 'd["job_id"]')
check "ok=true"                    test "$(echo "$R3" | json 'd["ok"]')" = "True"
check "job_id distinto al del paso 2" test "$ID2" != "$ID3"

echo "== Paso 4: submit \"\" (comando vacío)"
JOBS_BEFORE=$(cli list | json 'len(d["jobs"])')
R4=$(cli submit ""); RC4=$?; echo "$R4"; echo "rc=$RC4"
JOBS_AFTER=$(cli list | json 'len(d["jobs"])')
check "ok=false con mensaje"       test "$(echo "$R4" | json 'd["ok"]')" = "False" 
check "mensaje de error presente"  test -n "$(echo "$R4" | json 'd.get("error","")')"
check "rc del cliente != 0"        test "$RC4" -ne 0
check "no se creó un Job nuevo ($JOBS_BEFORE -> $JOBS_AFTER)" test "$JOBS_BEFORE" = "$JOBS_AFTER"

echo "== Paso 5: JSON mal formado y JSON sin 'op'"
R5A=$(raw_frame '{esto no es json'); echo "malformado: $R5A"
R5B=$(raw_frame '{"x":1}');          echo "sin op:     $R5B"
check "JSON mal formado -> ok=false con error" test "$(echo "$R5A" | json 'd["ok"]')" = "False"
check "sin op -> ok=false con error"           test "$(echo "$R5B" | json 'd["ok"]')" = "False"

echo "== Paso 6: el servidor sigue vivo y responde"
R6=$(cli submit echo final); echo "$R6"
check "servidor vivo (kill -0)"    kill -0 "$SERVER_PID"
check "submit posterior ok=true"   test "$(echo "$R6" | json 'd["ok"]')" = "True"

stop_server
echo "== Fallos: $FAILS"
[ "$FAILS" -eq 0 ] && echo "RESULTADO TC-001: PASS" || echo "RESULTADO TC-001: FAIL"
exit "$FAILS"
