#!/usr/bin/env bash
# Funciones comunes de los scripts de verificación (TC-00X).
# Uso: source verif/scripts/common.sh   (desde la raíz del repositorio)

BUILD_DIR="${BUILD_DIR:-build}"
SERVER_BIN="$BUILD_DIR/jobrunner-server"
CLI_BIN="$BUILD_DIR/jobrunner-cli"
RUN_ID="${RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)}"
RESULTS_DIR="verif/results/$RUN_ID"
FAILS=0
CHECKS=0

mkdir -p "$RESULTS_DIR"

# El timeout evita que un servidor colgado detenga el script completo.
cli()  { timeout 10 "$CLI_BIN" "$SOCK" "$@"; }
json() { python3 -c 'import sys,json; d=json.load(sys.stdin); print(eval(sys.argv[1]))' "$1"; }

# Base temporal propia: sin ella, ./data conserva los Jobs de corridas
# anteriores y los ids ya no empiezan en 1 (TC-002 fallaba al correr tras TC-001).
start_server() {
    rm -f "$SOCK"
    if [ -z "${DATA_DIR:-}" ]; then DATA_DIR=$(mktemp -d); OWN_DATA_DIR=1; fi
    "$SERVER_BIN" "$SOCK" --data-dir "$DATA_DIR" >"$RESULTS_DIR/${TC_NAME:-tc}-server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "ERROR: el servidor no abrió el socket" >&2; return 1
}

stop_server() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    rm -f "$SOCK"
    if [ -n "${OWN_DATA_DIR:-}" ]; then rm -rf "$DATA_DIR"; fi
}

# --- Funciones de los casos TC-004 a TC-008 -----------------------------------

# start_tc <etiqueta> [max_concurrentes]: arranca el servidor sobre DATA_DIR
# (temporal si no se definió) y deja su bitácora en tc-XXX-<etiqueta>-server.log.
# Reiniciar con la misma DATA_DIR conserva los Jobs (TC-007).
start_tc() {
    if [ -z "${DATA_DIR:-}" ]; then DATA_DIR=$(mktemp -d); OWN_DATA_DIR=1; fi
    LOG="$RESULTS_DIR/$TC_NAME-$1-server.log"
    rm -f "$SOCK"
    "$SERVER_BIN" "$SOCK" "${2:-3}" --data-dir "$DATA_DIR" >"$LOG" 2>&1 </dev/null &
    SERVER_PID=$!
    for _ in $(seq 1 50); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "ERROR: el servidor no abrió el socket" >&2; return 1
}

# stop_tc [señal]: detiene el servidor (SIGTERM por defecto) y deja su código en SERVER_RC.
stop_tc() {
    kill -"${1:-TERM}" "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null; SERVER_RC=$?
    rm -f "$SOCK"
}

# Para `trap cleanup_tc EXIT`: no deja servidor, socket ni base temporal.
cleanup_tc() {
    kill -9 "$SERVER_PID" 2>/dev/null
    rm -f "$SOCK"
    if [ -n "${OWN_DATA_DIR:-}" ]; then rm -rf "$DATA_DIR"; fi
}

server_alive() { kill -0 "$SERVER_PID" 2>/dev/null; }

# submit_id <comando...>: envía un Job e imprime su id (vacío si fue rechazado).
submit_id() {
    cli submit "$@" 2>/dev/null | python3 -c 'import sys,json
try: print(json.load(sys.stdin).get("job_id", ""))
except Exception: pass'
}

# field <id> <campo>: imprime un campo del Job (vacío si no existe).
field() {
    cli status "$1" 2>/dev/null | python3 -c 'import sys,json
try: v=json.load(sys.stdin)["job"].get(sys.argv[1])
except Exception: v=None
print("" if v is None else v)' "$2"
}

# jcheck <id> <expresión>: evalúa una expresión de Python sobre el Job `j`.
jcheck() {
    cli status "$1" 2>/dev/null | python3 -c 'import sys,json
try: j=json.load(sys.stdin)["job"]
except Exception: sys.exit(1)
sys.exit(0 if eval(sys.argv[1]) else 1)' "$2"
}

# lcheck <expresión> [estado]: evalúa una expresión sobre la lista `jobs`.
lcheck() {
    local expr="$1"; shift
    cli list "$@" 2>/dev/null | python3 -c 'import sys,json
try: jobs=json.load(sys.stdin)["jobs"]
except Exception: sys.exit(1)
sys.exit(0 if eval(sys.argv[1]) else 1)' "$expr"
}

# rcheck <respuesta-json> <expresión>: evalúa una expresión sobre la respuesta `d`.
rcheck() {
    printf '%s' "$1" | python3 -c 'import sys,json
try: d=json.load(sys.stdin)
except Exception: sys.exit(1)
sys.exit(0 if eval(sys.argv[1]) else 1)' "$2"
}

# wait_state <id> <estado> [segundos]: espera hasta que el Job llegue al estado.
wait_state() {
    local i
    for i in $(seq 1 $(( ${3:-5} * 10 ))); do
        [ "$(field "$1" state)" = "$2" ] && return 0
        sleep 0.1
    done
    return 1
}

# wait_terminal <id> [segundos]: espera un estado terminal.
wait_terminal() {
    local i s
    for i in $(seq 1 $(( ${2:-5} * 10 ))); do
        s=$(field "$1" state)
        case "$s" in SUCCEEDED|FAILED|CANCELED) return 0;; esac
        sleep 0.1
    done
    return 1
}

# job_pid <texto>: pid del hijo directo del servidor cuyo comando contiene el texto.
job_pid() { ps --ppid "$SERVER_PID" -o pid=,cmd= | awk -v c="$1" 'index($0,c){print $1; exit}'; }

# pid_gone <pid> [segundos]: espera a que el proceso desaparezca.
pid_gone() {
    local i
    [ -n "$1" ] || return 1
    for i in $(seq 1 $(( ${2:-3} * 10 ))); do
        kill -0 "$1" 2>/dev/null || return 0
        sleep 0.1
    done
    return 1
}

between() { python3 -c "import sys; sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)" "$@"; }

# finish_tc <TC-XXX> <comprobaciones esperadas>: imprime el resumen y sale con 0 solo si pasa.
finish_tc() {
    echo "== Comprobaciones ejecutadas: $CHECKS (esperadas: $2) | fallos: $FAILS"
    if [ "$CHECKS" -ne "$2" ]; then
        echo "[FAIL] el número de comprobaciones ejecutadas no coincide con el esperado"; FAILS=$((FAILS+1))
    fi
    if [ "$FAILS" -eq 0 ]; then echo "RESULTADO $1: PASS"; exit 0; fi
    echo "RESULTADO $1: FAIL"; exit 1
}

# check <descripción> <comando...>: registra PASS/FAIL del criterio
check() {
    local desc="$1"; shift
    CHECKS=$((CHECKS+1))
    if "$@"; then echo "[OK]   $desc"; else echo "[FAIL] $desc"; FAILS=$((FAILS+1)); fi
}

# Envía una trama cruda (prefijo de 4 bytes big-endian + contenido) y muestra la respuesta
raw_frame() {
    python3 - "$SOCK" "$1" <<'PY'
import socket, struct, sys
sock_path, payload = sys.argv[1], sys.argv[2].encode()
s = socket.socket(socket.AF_UNIX); s.settimeout(5); s.connect(sock_path)
s.sendall(struct.pack(">I", len(payload)) + payload)
hdr = s.recv(4); n = struct.unpack(">I", hdr)[0]
data = b""
while len(data) < n: data += s.recv(n - len(data))
print(data.decode())
PY
}
