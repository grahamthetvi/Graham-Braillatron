#!/usr/bin/env bash
# Generate a fresh remote-display pairing code (valid 5 minutes).
set -euo pipefail

SOCKET="${BRAILLATRON_DISPLAY_CMD_SOCKET:-/run/braillatron/display-cmd.sock}"

if [[ ! -S "${SOCKET}" ]]; then
  echo "displayd command socket not found: ${SOCKET}" >&2
  echo "Is braillatron-displayd running? Try: sudo systemctl start braillatron-displayd" >&2
  exit 1
fi

response="$(python3 - <<PY
import json, socket, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("${SOCKET}")
s.sendall(b'{"cmd":"pairing.start"}\n')
time.sleep(0.2)
print(s.recv(4096).decode(), end="")
PY
)"

code="$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('code',''))" "${response}")"
if [[ -z "${code}" ]]; then
  echo "Failed to start pairing: ${response}" >&2
  exit 1
fi

echo "${code}"
echo "Valid for 5 minutes. Open http://<pi-ip>:8080 and enter this code." >&2
