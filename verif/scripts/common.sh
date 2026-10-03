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

cli()  { "$CLI_BIN" "$SOCK" "$@"; }
json() { python3 -c 'import sys,json; d=json.load(sys.stdin); print(eval(sys.argv[1]))' "$1"; }

start_server() {
    rm -f "$SOCK"
    "$SERVER_BIN" "$SOCK" >"$RESULTS_DIR/${TC_NAME:-tc}-server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "ERROR: el servidor no abrió el socket" >&2; return 1
}

stop_server() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    rm -f "$SOCK"
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
