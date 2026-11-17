#!/usr/bin/env python3
"""mock-server.py PORT SCRIPT: a minimal stand-in Neuro API server for isolated tests.

Accepts one game connection, answers nothing on its own, prints every message the game
sends (one JSON per line, prefixed with seconds since start), and plays SCRIPT: one line
per step, either
    wait SECONDS
    until TEXT            (wait until a message from the game contains TEXT, up to 60 s)
    act NAME [JSON]       (send an action; JSON is its data)
Lines starting with # are ignored. Exits when the script ends.
Uses only the standard library, so it runs anywhere Python does.
"""
import base64
import hashlib
import json
import socket
import struct
import sys
import threading
import time
import uuid

PORT = int(sys.argv[1])
STEPS = [l.strip() for l in open(sys.argv[2], encoding="utf-8") if l.strip() and not l.startswith("#")]
START = time.time()
received = []
lock = threading.Condition()


def log(direction, text):
    print(f"{time.time() - START:7.1f} {direction} {text}", flush=True)


def handshake(conn):
    request = b""
    while b"\r\n\r\n" not in request:
        request += conn.recv(4096)
    key = [l.split(b":", 1)[1].strip() for l in request.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")][0]
    accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")


def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise ConnectionError
        data += chunk
    return data


def reader(conn):
    try:
        while True:
            head = recv_exact(conn, 2)
            length = head[1] & 0x7F
            if length == 126:
                length = struct.unpack(">H", recv_exact(conn, 2))[0]
            elif length == 127:
                length = struct.unpack(">Q", recv_exact(conn, 8))[0]
            mask = recv_exact(conn, 4) if head[1] & 0x80 else b"\0\0\0\0"
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(recv_exact(conn, length)))
            if head[0] & 0x0F == 0x8:
                return
            if head[0] & 0x0F == 0x1:
                # The game escapes slashes ("actions\\/force"); undo that so steps can name commands plainly
                text = payload.decode("utf-8", "replace").replace("\\/", "/")
                log("<", text)
                with lock:
                    received.append(text)
                    lock.notify_all()
    except (ConnectionError, OSError):
        return


def send(conn, obj):
    data = json.dumps(obj).encode()
    header = bytes([0x81]) + (bytes([len(data)]) if len(data) < 126 else bytes([126]) + struct.pack(">H", len(data)))
    conn.sendall(header + data)
    log(">", json.dumps(obj))


server = socket.create_server(("127.0.0.1", PORT))
conn, _ = server.accept()
handshake(conn)
threading.Thread(target=reader, args=(conn,), daemon=True).start()

seen = 0
for step in STEPS:
    kind, _, rest = step.partition(" ")
    if kind == "wait":
        time.sleep(float(rest))
    elif kind == "until":
        deadline = time.time() + 60
        with lock:
            while not any(rest in m for m in received[seen:]) and time.time() < deadline:
                lock.wait(deadline - time.time())
            hits = [i for i, m in enumerate(received[seen:], seen) if rest in m]
            if hits:
                seen = hits[0] + 1
            else:
                log("!", f"timed out waiting for {rest!r}")
                for message in received[-5:]:
                    log("!", message)
                sys.exit(1)
    elif kind == "act":
        name, _, data = rest.partition(" ")
        message = {"command": "action", "data": {"id": str(uuid.uuid4()), "name": name}}
        if data:
            message["data"]["data"] = data
        send(conn, message)
time.sleep(1)
