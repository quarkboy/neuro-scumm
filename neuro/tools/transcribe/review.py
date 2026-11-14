#!/usr/bin/env python3
"""review.py EXTRACT_DIR INI_FILE [PORT]: a local web page for checking room descriptions.

Shows each room extracted by extract-rooms.sh (the whole room as one picture, with its
objects and actors outlined) next to what the description file says about it, and lets
a reviewer correct the text and mark the room as checked. Saving writes straight back
into the description file, keeping its comments and the order of everything else.

Only listens on 127.0.0.1. Open http://127.0.0.1:PORT/ (8070 unless given).
"""
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
EXTRACT_DIR = os.path.abspath(sys.argv[1])
INI_FILE = os.path.abspath(sys.argv[2])
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8070

# Keys the page edits; anything else in a room's section (spots, voices) is kept as it is
ROOM_KEYS = ("name", "description", "status", "review_note")
SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
KEY = re.compile(r"^\s*([^=;#\s][^=]*?)\s*=\s*(.*)$")


def read_ini():
    """The description file as (section name or None, list of lines) blocks, in order."""
    blocks, name, lines = [], None, []
    with open(INI_FILE, encoding="utf-8") as f:
        for line in f.read().splitlines():
            m = SECTION.match(line)
            if m:
                blocks.append((name, lines))
                name, lines = m.group(1), [line]
            else:
                lines.append(line)
    blocks.append((name, lines))
    return blocks


def section_keys(lines):
    keys = {}
    for line in lines[1:]:
        m = KEY.match(line)
        if m:
            keys[m.group(1)] = m.group(2)
    return keys


def room_info(blocks, room):
    for name, lines in blocks:
        if name == f"room{room}":
            return section_keys(lines)
    return {}


def save_room(room, fields):
    """Rewrites one [roomN] section with the page's fields, keeping everything else."""
    blocks = read_ini()
    wanted = {}
    for key in ROOM_KEYS:
        value = " ".join(str(fields.get(key, "")).split())
        if value:
            wanted[key] = value
    for kind in ("object", "actor"):
        for thing_id, value in (fields.get(kind + "s") or {}).items():
            value = " ".join(str(value).split())
            if value:
                wanted[f"{kind}.{int(thing_id)}"] = value
    edited = set(ROOM_KEYS)
    edited.update(f"object.{int(i)}" for i in (fields.get("objects") or {}))
    edited.update(f"actor.{int(i)}" for i in (fields.get("actors") or {}))

    section = f"room{room}"
    for index, (name, lines) in enumerate(blocks):
        if name != section:
            continue
        kept = [lines[0]]
        for line in lines[1:]:
            m = KEY.match(line)
            if m and m.group(1) in edited:
                if m.group(1) in wanted:
                    kept.append(f"{m.group(1)} = {wanted.pop(m.group(1))}")
                continue
            kept.append(line)
        # New keys go after the last key line, before any trailing blank lines
        insert = len(kept)
        while insert > 1 and not kept[insert - 1].strip():
            insert -= 1
        kept[insert:insert] = [f"{k} = {v}" for k, v in wanted.items()]
        blocks[index] = (name, kept)
        break
    else:
        lines = [f"[{section}]"] + [f"{k} = {v}" for k, v in wanted.items()] + [""]
        if blocks[-1][1] and blocks[-1][1][-1].strip():
            blocks[-1][1].append("")
        blocks.append((section, lines))

    text = "\n".join(line for _, lines in blocks for line in lines).rstrip("\n") + "\n"
    with open(INI_FILE + ".tmp", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(INI_FILE + ".tmp", INI_FILE)


def rooms():
    blocks = read_ini()
    out = []
    for file in sorted(os.listdir(EXTRACT_DIR)):
        m = re.fullmatch(r"room(\d+)\.json", file)
        if not m:
            continue
        with open(os.path.join(EXTRACT_DIR, file), encoding="utf-8") as f:
            room = json.load(f)
        info = room_info(blocks, room["room"])
        room["ini"] = {
            "name": info.get("name", ""),
            "description": info.get("description", ""),
            "status": info.get("status", ""),
            "review_note": info.get("review_note", ""),
            "objects": {k.split(".", 1)[1]: v for k, v in info.items() if k.startswith("object.")},
            "actors": {k.split(".", 1)[1]: v for k, v in info.items() if k.startswith("actor.")},
            "other": {k: v for k, v in info.items()
                      if k not in ROOM_KEYS and not k.startswith(("object.", "actor."))},
        }
        room["image"] = f"img/{file[:-5]}.png"
        out.append(room)
    return out


class Handler(BaseHTTPRequestHandler):
    def _send(self, status, body, ctype):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "review.html"), "rb") as f:
                self._send(200, f.read(), "text/html; charset=utf-8")
        elif self.path == "/api/rooms":
            self._send(200, json.dumps(rooms()).encode(), "application/json")
        elif re.fullmatch(r"/img/room\d+\.png", self.path):
            with open(os.path.join(EXTRACT_DIR, os.path.basename(self.path)), "rb") as f:
                self._send(200, f.read(), "image/png")
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        m = re.fullmatch(r"/api/room/(\d+)", self.path)
        if not m:
            self._send(404, b"not found", "text/plain")
            return
        fields = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)))
        save_room(int(m.group(1)), fields)
        self._send(200, b'{"ok":true}', "application/json")

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    print(f"Reviewing {EXTRACT_DIR} against {INI_FILE}: http://127.0.0.1:{PORT}/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
