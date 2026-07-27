#!/usr/bin/env python3
"""Braillatron LocalSend receive-only sidecar (HTTP, auto-accept).

Implements LocalSend protocol v2 upload API enough for phone/desktop LocalSend
clients to discover this device and push files. Encryption/HTTPS is off — set
the sender to HTTP / encryption off, or add this device by IP:port.

Files land under import dirs by type:
  credentials/incoming  — cookies.txt, client secrets, misc credentials
  library/import        — epub, txt, brf, zip, m4b, mp3, …
  podcasts/import       — opml, xml
  music                 — audio for the Music app
  spelling/custom       — csv/json word lists
"""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import socket
import struct
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

ALIAS = os.environ.get("BRAILLATRON_LOCALSEND_ALIAS", "Braillatron")
PORT = int(os.environ.get("BRAILLATRON_LOCALSEND_PORT", "53317"))
FINGERPRINT = os.environ.get("BRAILLATRON_LOCALSEND_FINGERPRINT") or secrets.token_hex(16)
DATA_ROOT = Path(os.environ.get("BRAILLATRON_DATA", "/data/braillatron"))
INCOMING_LOG = DATA_ROOT / "localsend" / "received.jsonl"
MULTICAST_ADDR = "224.0.0.167"
MULTICAST_PORT = 53317

SAFE_NAME = re.compile(r"[^A-Za-z0-9._ -]+")

_session_lock = threading.Lock()
_sessions: dict[str, dict[str, Any]] = {}


def device_info() -> dict[str, Any]:
    return {
        "alias": ALIAS,
        "version": "2.0",
        "deviceModel": "Braillatron",
        "deviceType": "headless",
        "fingerprint": FINGERPRINT,
        "port": PORT,
        "protocol": "http",
        "download": False,
    }


def safe_filename(name: str) -> str:
    base = Path(name).name.strip() or "upload.bin"
    cleaned = SAFE_NAME.sub("_", base).strip(" ._")
    return cleaned or "upload.bin"


def route_dest(file_name: str) -> Path:
    lower = file_name.lower()
    if lower in {"cookies.txt", "youtube-cookies.txt"} or lower.endswith(".cookies"):
        return DATA_ROOT / "credentials" / "incoming" / safe_filename(file_name)
    if lower in {"imap.ini", "gmail-imap.ini", "school-email.ini"}:
        return DATA_ROOT / "credentials" / "incoming" / safe_filename(file_name)
    if lower.endswith((".opml", ".xml")) and "cookie" not in lower:
        return DATA_ROOT / "podcasts" / "import" / safe_filename(file_name)
    if lower.endswith((".csv", ".json")) and "cookie" not in lower:
        return DATA_ROOT / "spelling" / "custom" / safe_filename(file_name)
    if lower.endswith((".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".aac", ".wav", ".wma")):
        # Prefer music library for loose audio; m4b/audiobook-ish → library import
        if lower.endswith(".m4b"):
            return DATA_ROOT / "library" / "import" / safe_filename(file_name)
        return DATA_ROOT / "music" / safe_filename(file_name)
    if lower.endswith((".epub", ".txt", ".brf", ".zip", ".m4b", ".daisy")):
        return DATA_ROOT / "library" / "import" / safe_filename(file_name)
    return DATA_ROOT / "credentials" / "incoming" / safe_filename(file_name)


def log_received(path: Path, size: int, source: str) -> None:
    INCOMING_LOG.parent.mkdir(parents=True, exist_ok=True)
    record = {
        "ts": int(time.time()),
        "path": str(path),
        "name": path.name,
        "size": size,
        "source": source,
    }
    with INCOMING_LOG.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(record) + "\n")
    print(f"received {path} ({size} bytes) from {source}", flush=True)


def read_json(handler: BaseHTTPRequestHandler) -> Any:
    length = int(handler.headers.get("Content-Length", "0") or "0")
    raw = handler.rfile.read(length) if length > 0 else b"{}"
    if not raw:
        return {}
    return json.loads(raw.decode("utf-8"))


def write_json(handler: BaseHTTPRequestHandler, code: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def write_empty(handler: BaseHTTPRequestHandler, code: int = 200) -> None:
    handler.send_response(code)
    handler.send_header("Content-Length", "0")
    handler.end_headers()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path in ("/api/localsend/v2/info", "/api/localsend/v1/info"):
            write_json(self, 200, device_info())
            return
        write_empty(self, 404)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        path = parsed.path
        qs = parse_qs(parsed.query)

        if path in ("/api/localsend/v2/register", "/api/localsend/v1/register"):
            _ = read_json(self)
            write_json(self, 200, device_info())
            return

        if path in ("/api/localsend/v2/prepare-upload", "/api/localsend/v1/prepare-upload"):
            body = read_json(self)
            files = body.get("files") or {}
            if not isinstance(files, dict) or not files:
                write_empty(self, 400)
                return
            session_id = str(uuid.uuid4())
            tokens: dict[str, str] = {}
            meta: dict[str, Any] = {}
            for file_id, info in files.items():
                if not isinstance(info, dict):
                    continue
                token = secrets.token_urlsafe(16)
                tokens[str(file_id)] = token
                meta[str(file_id)] = {
                    "token": token,
                    "fileName": str(info.get("fileName") or f"{file_id}.bin"),
                    "size": int(info.get("size") or 0),
                }
            with _session_lock:
                _sessions[session_id] = {
                    "ip": self.client_address[0],
                    "files": meta,
                    "created": time.time(),
                }
            # v1 returns only the token map; v2 wraps sessionId + files
            if path.endswith("/v1/prepare-upload"):
                write_json(self, 200, tokens)
            else:
                write_json(self, 200, {"sessionId": session_id, "files": tokens})
            return

        if path in ("/api/localsend/v2/upload", "/api/localsend/v1/upload"):
            session_id = (qs.get("sessionId") or [""])[0]
            file_id = (qs.get("fileId") or [""])[0]
            token = (qs.get("token") or [""])[0]
            if not file_id or not token:
                write_empty(self, 400)
                return
            with _session_lock:
                session = _sessions.get(session_id) if session_id else None
                # v1 may omit sessionId; accept matching token in any open session
                if session is None:
                    for sid, cand in _sessions.items():
                        fmeta = cand.get("files", {}).get(file_id)
                        if fmeta and fmeta.get("token") == token:
                            session_id = sid
                            session = cand
                            break
                if session is None:
                    write_empty(self, 403)
                    return
                fmeta = session.get("files", {}).get(file_id)
                if not fmeta or fmeta.get("token") != token:
                    write_empty(self, 403)
                    return
                if session.get("ip") and session["ip"] != self.client_address[0]:
                    write_empty(self, 403)
                    return
                file_name = fmeta["fileName"]
                expected = int(fmeta.get("size") or 0)

            dest = route_dest(file_name)
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = dest.with_suffix(dest.suffix + ".partial")
            length = int(self.headers.get("Content-Length", "0") or "0")
            remaining = length if length > 0 else expected
            written = 0
            try:
                with tmp.open("wb") as out:
                    while True:
                        chunk_size = 64 * 1024
                        if remaining > 0:
                            chunk_size = min(chunk_size, remaining)
                        chunk = self.rfile.read(chunk_size)
                        if not chunk:
                            break
                        out.write(chunk)
                        written += len(chunk)
                        if remaining > 0:
                            remaining -= len(chunk)
                            if remaining <= 0:
                                break
                tmp.replace(dest)
                log_received(dest, written, self.client_address[0])
                write_empty(self, 200)
            except OSError as exc:
                try:
                    tmp.unlink(missing_ok=True)
                except OSError:
                    pass
                sys.stderr.write(f"upload failed: {exc}\n")
                write_empty(self, 500)
            return

        if path in ("/api/localsend/v2/cancel", "/api/localsend/v1/cancel"):
            session_id = (qs.get("sessionId") or [""])[0]
            with _session_lock:
                _sessions.pop(session_id, None)
            write_empty(self, 200)
            return

        write_empty(self, 404)


def multicast_responder(stop: threading.Event) -> None:
    """Reply to LocalSend UDP announcements so we appear in nearby device lists."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", MULTICAST_PORT))
    except OSError as exc:
        sys.stderr.write(f"multicast bind failed: {exc}\n")
        return
    mreq = struct.pack("=4s4s", socket.inet_aton(MULTICAST_ADDR), socket.inet_aton("0.0.0.0"))
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError as exc:
        sys.stderr.write(f"multicast join failed: {exc}\n")
        sock.close()
        return
    sock.settimeout(1.0)
    payload = json.dumps({**device_info(), "announce": False}).encode("utf-8")
    while not stop.is_set():
        try:
            data, addr = sock.recvfrom(65535)
        except socket.timeout:
            continue
        except OSError:
            break
        try:
            msg = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        if not isinstance(msg, dict):
            continue
        if msg.get("fingerprint") == FINGERPRINT:
            continue
        if msg.get("announce") is False:
            continue
        # Prefer HTTP register to announcer; fall back to multicast reply
        try:
            sock.sendto(payload, (MULTICAST_ADDR, MULTICAST_PORT))
        except OSError:
            pass
        try:
            sock.sendto(payload, addr)
        except OSError:
            pass
    sock.close()


def announce_once() -> None:
    payload = json.dumps({**device_info(), "announce": True}).encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    try:
        sock.sendto(payload, (MULTICAST_ADDR, MULTICAST_PORT))
    except OSError as exc:
        sys.stderr.write(f"announce failed: {exc}\n")
    finally:
        sock.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Braillatron LocalSend receive sidecar")
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--alias", default=None)
    args = parser.parse_args()
    global PORT, ALIAS
    if args.port is not None:
        PORT = args.port
    if args.alias is not None:
        ALIAS = args.alias

    for path in (
        DATA_ROOT / "credentials" / "incoming",
        DATA_ROOT / "library" / "import",
        DATA_ROOT / "podcasts" / "import",
        DATA_ROOT / "music",
        DATA_ROOT / "spelling" / "custom",
        DATA_ROOT / "localsend",
    ):
        path.mkdir(parents=True, exist_ok=True)

    stop = threading.Event()
    threading.Thread(target=multicast_responder, args=(stop,), daemon=True).start()
    announce_once()

    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"LocalSend receive listening http://0.0.0.0:{PORT} alias={ALIAS}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
