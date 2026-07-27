#!/usr/bin/env python3
"""Agent-friendly remote control for Braillatron via displayd WebSocket.

Bypasses the browser focus issues: pair over HTTP, inject Linux keycodes over
/ws/frame (same path as the remote-display viewer).

Examples:
  # Most reliable for agents: static pairing code + inject (on Pi or LAN)
  braillatron-remote-keys --host 192.168.0.239 --code 123456 down enter

  # Or run entirely on-device over SSH (uses localhost + code)
  braillatron-remote-keys --host 192.168.0.239 --ssh dietpi --via-ssh --code 123456 down enter

  # Dynamic pairing via SSH when no static hash is configured
  braillatron-remote-keys --host 192.168.0.239 --ssh dietpi --via-ssh --pair --clear-lockout down

Environment (optional):
  BRAILLATRON_HOST, BRAILLATRON_SSH_USER, BRAILLATRON_SSH_PASS,
  BRAILLATRON_PAIRING_CODE, BRAILLATRON_REMOTE_DISPLAY_URL
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import select
import socket
import ssl
import struct
import subprocess
import sys
import time
import urllib.parse
from typing import Iterable, Optional, Sequence, Tuple

# Linux input keycodes — must match deploy/static/remote-display/viewer.js KEY_MAP
KEYCODES = {
    "dot1": 33,  # KEY_F
    "dot2": 32,  # KEY_D
    "dot3": 31,  # KEY_S
    "dot4": 36,  # KEY_J
    "dot5": 37,  # KEY_K
    "dot6": 38,  # KEY_L
    "f": 33,
    "d": 32,
    "s": 31,
    "j": 36,
    "k": 37,
    "l": 38,
    "up": 103,
    "down": 108,
    "backspace": 14,
    "bs": 14,
    "enter": 28,
    "menu": 41,  # KEY_GRAVE / backtick
    "grave": 41,
    "`": 41,
    "tab": 15,  # Shift/TTS
    "tts": 15,
    "shift": 15,
    "speech": 126,  # KEY_RIGHTMETA
    "space": 57,  # KEY_SPACE — registered on virtual keyboard
}

DOT_BITS = {
    "1": "dot1",
    "2": "dot2",
    "3": "dot3",
    "4": "dot4",
    "5": "dot5",
    "6": "dot6",
}


class WebSocketClient:
    """Minimal RFC6455 client (text frames only) using the stdlib."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self._buf = bytearray()

    @classmethod
    def connect(cls, url: str, cookie_header: str, timeout: float = 10.0) -> "WebSocketClient":
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme not in ("ws", "wss"):
            raise ValueError(f"unsupported websocket URL: {url}")
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        path = parsed.path or "/"
        if parsed.query:
            path += "?" + parsed.query

        raw = socket.create_connection((host, port), timeout=timeout)
        if parsed.scheme == "wss":
            ctx = ssl.create_default_context()
            raw = ctx.wrap_socket(raw, server_hostname=host)

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        headers = [
            f"GET {path} HTTP/1.1",
            f"Host: {host}:{port}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key}",
            "Sec-WebSocket-Version: 13",
            "Origin: http://localhost",
        ]
        if cookie_header:
            headers.append(f"Cookie: {cookie_header}")
        headers.append("")
        headers.append("")
        raw.sendall("\r\n".join(headers).encode("ascii"))

        response = b""
        while b"\r\n\r\n" not in response:
            chunk = raw.recv(4096)
            if not chunk:
                raise ConnectionError("websocket handshake closed early")
            response += chunk
        status_line = response.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
        if " 101 " not in status_line:
            raise ConnectionError(f"websocket upgrade failed: {status_line}")
        return cls(raw)

    def send_text(self, text: str) -> None:
        payload = text.encode("utf-8")
        header = bytearray([0x81])  # FIN + text
        mask_bit = 0x80
        length = len(payload)
        if length < 126:
            header.append(mask_bit | length)
        elif length < (1 << 16):
            header.append(mask_bit | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(mask_bit | 127)
            header.extend(struct.pack("!Q", length))
        mask = os.urandom(4)
        header.extend(mask)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + masked)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


def load_defaults_from_env_file() -> None:
    candidates = [
        os.environ.get("BRAILLATRON_PI_ENV", ""),
        os.path.join(os.path.dirname(__file__), "..", "..", ".cursor", "pi.local.env"),
        os.path.expanduser("~/.config/braillatron/pi.local.env"),
    ]
    for path in candidates:
        if not path or not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))
        break


def host_from_args(args: argparse.Namespace) -> Tuple[str, int]:
    if args.url:
        parsed = urllib.parse.urlparse(args.url)
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or 8080
        return host, port
    host = args.host or os.environ.get("BRAILLATRON_HOST") or "127.0.0.1"
    if host.startswith("http://") or host.startswith("https://"):
        parsed = urllib.parse.urlparse(host)
        return parsed.hostname or "127.0.0.1", parsed.port or 8080
    return host, args.port


def pair(host: str, port: int, code: str, timeout: float = 10.0) -> str:
    """Pair and return Cookie header using a raw HTTP/1.1 POST (most reliable)."""
    body = json.dumps({"code": code}).encode("ascii")
    request = (
        b"POST /api/pair HTTP/1.1\r\n"
        + f"Host: {host}:{port}\r\n".encode("ascii")
        + b"Content-Type: application/json\r\n"
        + f"Content-Length: {len(body)}\r\n".encode("ascii")
        + b"Connection: close\r\n"
        + b"\r\n"
        + body
    )
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(request)
        chunks = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    raw = b"".join(chunks).decode("utf-8", errors="replace")
    if "\r\n\r\n" not in raw:
        raise RuntimeError(f"pairing incomplete response: {raw!r}")
    header, payload = raw.split("\r\n\r\n", 1)
    status_line = header.split("\r\n", 1)[0]
    if " 200 " not in status_line:
        raise RuntimeError(f"pairing {status_line}: {payload.strip()}")
    try:
        data = json.loads(payload) if payload.strip() else {}
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"pairing bad JSON: {payload!r}") from exc
    if not data.get("ok", False):
        raise RuntimeError(f"pairing failed: {data}")
    cookie_header = ""
    for line in header.split("\r\n"):
        if line.lower().startswith("set-cookie:") and "braillatron_session=" in line:
            cookie_header = line.split(":", 1)[1].strip().split(";", 1)[0]
            break
    if not cookie_header:
        raise RuntimeError("pairing succeeded but no braillatron_session cookie returned")
    return cookie_header


def ssh_run(ssh_user: str, host: str, password: Optional[str], remote_cmd: str) -> str:
    remote = f"{ssh_user}@{host}"
    cmd = [
        "ssh",
        "-T",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-o",
        "PreferredAuthentications=password" if password else "BatchMode=yes",
        "-o",
        "PubkeyAuthentication=no" if password else "PubkeyAuthentication=yes",
        remote,
        remote_cmd,
    ]
    if password:
        cmd = ["sshpass", "-p", password] + cmd
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"ssh failed ({result.returncode}): {result.stderr or result.stdout}"
        )
    return result.stdout


def fetch_pairing_code_via_ssh(ssh_user: str, host: str, password: Optional[str],
                               clear_lockout: bool = False) -> str:
    if clear_lockout:
        print("clearing displayd pairing lockout …", flush=True)
        ssh_run(ssh_user, host, password, "sudo systemctl restart braillatron-displayd")
        time.sleep(1.2)
    # One SSH round-trip: wait for socket, start pairing, print code only
    remote = (
        "for i in 1 2 3 4 5 6 7 8 9 10; do "
        "[[ -S /run/braillatron/display-cmd.sock ]] && break; sleep 0.3; done; "
        "sudo braillatron-show-pairing-code 2>/dev/null | head -1"
    )
    output = ssh_run(ssh_user, host, password, remote)
    code = output.strip().splitlines()[0].strip() if output.strip() else ""
    if not code or not code.isdigit():
        raise RuntimeError(f"could not fetch pairing code via SSH: {output!r}")
    return code


def tap(ws: WebSocketClient, keycode: int, hold_ms: float = 40.0) -> None:
    ws.send_text(json.dumps({"type": "keydown", "key": keycode}))
    time.sleep(max(hold_ms, 1.0) / 1000.0)
    ws.send_text(json.dumps({"type": "keyup", "key": keycode}))


def chord(ws: WebSocketClient, dots: Sequence[str], hold_ms: float = 250.0) -> None:
    codes = [KEYCODES[DOT_BITS[d]] for d in dots]
    for code in codes:
        ws.send_text(json.dumps({"type": "keydown", "key": code}))
        time.sleep(0.025)
    time.sleep(max(hold_ms, 1.0) / 1000.0)
    for code in reversed(codes):
        ws.send_text(json.dumps({"type": "keyup", "key": code}))
        time.sleep(0.015)


def parse_action(token: str) -> Tuple[str, object]:
    lower = token.lower()
    if lower.startswith("chord:"):
        dots = [c for c in lower.split(":", 1)[1] if c in DOT_BITS]
        if not dots:
            raise ValueError(f"empty chord: {token}")
        return "chord", dots
    if lower.startswith("sleep:") or lower.startswith("wait:"):
        ms = float(lower.split(":", 1)[1])
        return "sleep", ms
    if lower.startswith("hold:"):
        # hold:enter:200
        parts = lower.split(":")
        if len(parts) != 3 or parts[1] not in KEYCODES:
            raise ValueError(f"hold syntax is hold:<key>:<ms>, got {token}")
        return "hold", (parts[1], float(parts[2]))
    if lower in KEYCODES:
        return "tap", lower
    if lower.isdigit():
        return "raw", int(lower)
    raise ValueError(
        f"unknown key {token!r}; try: {', '.join(sorted(k for k in KEYCODES if k.isalpha() or k in ('`',)))}"
    )


def run_actions(ws: WebSocketClient, actions: Iterable[str], gap_ms: float) -> None:
    for token in actions:
        kind, value = parse_action(token)
        if kind == "tap":
            tap(ws, KEYCODES[str(value)])
            print(f"tap {value} ({KEYCODES[str(value)]})", flush=True)
        elif kind == "raw":
            tap(ws, int(value))
            print(f"tap raw {value}", flush=True)
        elif kind == "chord":
            chord(ws, list(value))
            print(f"chord {''.join(value)}", flush=True)
        elif kind == "hold":
            name, ms = value  # type: ignore[misc]
            tap(ws, KEYCODES[str(name)], hold_ms=float(ms))
            print(f"hold {name} {ms}ms", flush=True)
        elif kind == "sleep":
            time.sleep(float(value) / 1000.0)
            print(f"sleep {value}ms", flush=True)
        if gap_ms > 0 and kind != "sleep":
            time.sleep(gap_ms / 1000.0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("actions", nargs="*", help="Keys/actions: down up enter menu tab chord:12 sleep:200 …")
    parser.add_argument("--host", default=None, help="Pi hostname/IP (default BRAILLATRON_HOST or 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Remote display HTTP port (default 8080)")
    parser.add_argument("--url", default=os.environ.get("BRAILLATRON_REMOTE_DISPLAY_URL"), help="Full http://host:port/")
    parser.add_argument("--code", default=os.environ.get("BRAILLATRON_PAIRING_CODE"), help="Pairing code")
    parser.add_argument("--pair", action="store_true",
                        help="Fetch a fresh pairing code via SSH (prefer --via-ssh for reliability)")
    parser.add_argument("--via-ssh", action="store_true",
                        help="Run pairing+keys on the Pi over SSH (localhost). Most reliable for agents.")
    parser.add_argument("--clear-lockout", action="store_true",
                        help="Restart braillatron-displayd first to clear rate-limit lockout")
    parser.add_argument("--ssh", default=None,
                        help="SSH user for --pair/--via-ssh (default: dietpi, else PI_SSH_USER)")
    parser.add_argument("--ssh-pass", default=os.environ.get("BRAILLATRON_SSH_PASS") or os.environ.get("PI_SSH_PASS"),
                        help="SSH password for sshpass (optional if key auth works)")
    parser.add_argument("--gap-ms", type=float, default=120.0, help="Delay between actions (default 120)")
    parser.add_argument("--list-keys", action="store_true", help="Print known key names and exit")
    return parser


def run_via_ssh(args: argparse.Namespace, host: str) -> int:
    import shlex

    ssh_user = args.ssh or os.environ.get("BRAILLATRON_SSH_USER") or "dietpi"
    # Prefer dietpi over a missing local username mirrored from pi.local.env
    if ssh_user == "grahamthetvi":
        ssh_user = "dietpi"
    actions = " ".join(shlex.quote(a) for a in args.actions)
    clear_block = ""
    if args.clear_lockout:
        clear_block = (
            "sudo systemctl restart braillatron-displayd; "
            "for i in $(seq 1 25); do "
            "[[ -S /run/braillatron/display-cmd.sock ]] && curl -sS -m1 http://127.0.0.1:8080/ >/dev/null 2>&1 && break; "
            "sleep 0.2; done; "
        )
    if args.code:
        code_block = f"CODE={shlex.quote(args.code)}; "
    else:
        code_block = "CODE=$(sudo braillatron-show-pairing-code 2>/dev/null | head -1); "
    remote = (
        "set -e; "
        + clear_block
        + code_block
        + "echo \"on-device pairing code $CODE\"; "
        + f"braillatron-remote-keys --host 127.0.0.1 --port {args.port} "
        + f"--code \"$CODE\" --gap-ms {args.gap_ms} {actions}"
    )
    print(f"running key injection on {ssh_user}@{host} via SSH …", flush=True)
    output = ssh_run(ssh_user, host, args.ssh_pass, remote)
    sys.stdout.write(output)
    if not output.endswith("\n"):
        sys.stdout.write("\n")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    load_defaults_from_env_file()
    if "PI_HOST" in os.environ and "BRAILLATRON_HOST" not in os.environ:
        os.environ["BRAILLATRON_HOST"] = os.environ["PI_HOST"]
    if "REMOTE_DISPLAY_PAIRING" in os.environ and "BRAILLATRON_PAIRING_CODE" not in os.environ:
        os.environ["BRAILLATRON_PAIRING_CODE"] = os.environ["REMOTE_DISPLAY_PAIRING"]

    args = build_parser().parse_args(argv)
    if args.list_keys:
        for name, code in sorted(KEYCODES.items(), key=lambda kv: (kv[1], kv[0])):
            print(f"{name:12} {code}")
        return 0
    if not args.actions:
        build_parser().print_help()
        return 2

    host, port = host_from_args(args)

    if args.via_ssh:
        return run_via_ssh(args, host)

    code = args.code
    if args.pair and not code:
        ssh_user = args.ssh or "dietpi"
        print(f"fetching pairing code via ssh {ssh_user}@{host} …", flush=True)
        code = fetch_pairing_code_via_ssh(ssh_user, host, args.ssh_pass, clear_lockout=args.clear_lockout)
        print(f"pairing code {code}", flush=True)
    if not code:
        print("error: need --code, --pair, or --via-ssh", file=sys.stderr)
        return 2

    print(f"pairing with http://{host}:{port}/ …", flush=True)
    try:
        cookie = pair(host, port, code)
    except RuntimeError as exc:
        msg = str(exc)
        # Only fall back to SSH when talking to a remote host (avoid recursion on-device).
        if host not in ("127.0.0.1", "localhost") and (
            "403" in msg or "rate_limited" in msg or "invalid" in msg
        ):
            print(f"pairing failed ({exc}); falling back to --via-ssh …", flush=True)
            args.via_ssh = True
            args.code = code
            args.ssh = args.ssh or "dietpi"
            return run_via_ssh(args, host)
        raise
    ws_url = f"ws://{host}:{port}/ws/frame"
    print(f"connecting {ws_url}", flush=True)
    ws = WebSocketClient.connect(ws_url, cookie)
    try:
        ws.sock.setblocking(False)
        deadline = time.time() + 0.3
        while time.time() < deadline:
            ready, _, _ = select.select([ws.sock], [], [], 0.05)
            if not ready:
                continue
            try:
                ws.sock.recv(65536)
            except BlockingIOError:
                break
        ws.sock.setblocking(True)
        run_actions(ws, args.actions, args.gap_ms)
    finally:
        ws.close()
    print("done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
