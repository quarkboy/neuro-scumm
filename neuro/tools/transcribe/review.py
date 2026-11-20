#!/usr/bin/env python3
"""review.py EXTRACT_DIR INI_FILE [PORT]: a local web page for checking room descriptions.

Shows each room extracted by extract-rooms.sh (the whole room as one picture, with its
objects and actors outlined) next to what the description file says about it, and lets
a reviewer correct the text and mark the room as checked. Saving writes straight back
into the description file, keeping its comments and the order of everything else.

If EXTRACT_DIR also holds lines.json (made by lines.py from a script dump), the page
gets a Lines view: every line the game's scripts can say, with its speaker and text
colour, where a reviewer names speakers and marks lines as starting or finishing a goal
or as a clue for one.

Two more views cover the rest of the file: goals, clues and nudges; and alerts, watches,
notes on dialogue lines, single-line speakers and the global sections.

Only listens on 127.0.0.1. Open http://127.0.0.1:PORT/ (8070 unless given).
"""
import json
import os
import re
import sys
from threading import Lock
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
EXTRACT_DIR = os.path.abspath(sys.argv[1])
INI_FILE = os.path.abspath(sys.argv[2])
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8070

SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
KEY = re.compile(r"^\s*([^=;#\s][^=]*?)\s*=\s*(.*)$")
# Sections that are one of a kind, and the prefixes of those that come in numbers
SINGLE = ("states", "game", "actors", "synonyms", "inventory")
INI_LOCK = Lock()
LISTS = {"goal": "goals", "clue": "clues", "line": "lines", "nudge": "nudges",
         "alert": "alerts", "watch": "watches", "linenote": "linenotes", "press": "presses"}
SAFE_SECTION = re.compile(r"(room\d+|(%s)\.[\w-]+|%s)" % ("|".join(LISTS), "|".join(SINGLE)))
SAFE_KEY = re.compile(r"[^\s=\[\]#;][^=\[\]#\r\n]*")


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


def write_ini(blocks):
    text = "\n".join(line for _, lines in blocks for line in lines).rstrip("\n") + "\n"
    with open(INI_FILE + ".tmp", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(INI_FILE + ".tmp", INI_FILE)


def is_comment(line):
    return line.lstrip().startswith(";")


def split_tail(block):
    """A block's own lines, and the comment lines ending it, which belong to the next section."""
    name, lines = block
    start = len(lines)
    while start > (0 if name is None else 1) and is_comment(lines[start - 1]):
        start -= 1
    return lines[:start], lines[start:]


def section_keys(lines):
    keys = {}
    for line in lines[1:]:
        m = KEY.match(line)
        if m:
            keys[m.group(1)] = m.group(2)
    return keys


def keys_end(own):
    """Where a section's keys stop; a heading for the sections after it may follow."""
    return max((i + 1 for i, line in enumerate(own) if i and KEY.match(line)), default=1)


def comment_of(blocks, index):
    """The comment written above a section and any among its keys, as plain text."""
    own, _ = split_tail(blocks[index])
    lines = split_tail(blocks[index - 1])[1] + [line for line in own[1:keys_end(own)] if is_comment(line)]
    return "\n".join(re.sub(r"^\s*; ?", "", line) for line in lines).strip()


def kind_of(section):
    return "room" if re.fullmatch(r"room\d+", section) else section.split(".")[0]


def edit_section(blocks, section, changes, delete=False, blank=(), comment=None):
    """Sets keys of one section (None or "" removes one), creating it if needed; or deletes it.

    A key named in blank is written with an empty value instead of being removed.
    A comment, when given, replaces the one above the section and any among its keys.
    """
    if not SAFE_SECTION.fullmatch(section):
        raise ValueError(f"section {section!r} is not one this page edits")
    clean = {}
    for key, value in changes.items():
        if not SAFE_KEY.fullmatch(key) or key != key.strip():
            raise ValueError(f"bad key {key!r}")
        value = None if value is None else " ".join(str(value).split())
        clean[key] = "" if (not value and key in blank) else (value or None)
    line_for = lambda key, value: f"{key} = {value}".rstrip()

    index = next((i for i, (name, _) in enumerate(blocks) if name == section), None)
    if index is None:
        if delete:
            return
        # A new section goes after the last one of its kind, so the file stays grouped
        kind = kind_of(section)
        last = max((i for i, (name, _) in enumerate(blocks) if name and kind_of(name) == kind), default=len(blocks) - 1)
        before, tail = split_tail(blocks[last])
        if before and before[-1].strip():
            before.append("")
        blocks[last] = (blocks[last][0], before)
        index = last + 1
        lines = [f"[{section}]"] + [line_for(k, v) for k, v in clean.items() if v is not None] + [""]
        blocks.insert(index, (section, lines + tail))
    elif delete:
        before, _ = split_tail(blocks[index - 1])
        own, tail = split_tail(blocks[index])
        rest = own[keys_end(own):]
        while rest and before and not rest[0].strip() and not before[-1].strip():
            rest = rest[1:]
        blocks[index - 1] = (blocks[index - 1][0], before + rest + tail)
        del blocks[index]
        return
    else:
        own, tail = split_tail(blocks[index])
        kept = [own[0]]
        for line in own[1:]:
            m = KEY.match(line)
            if m and m.group(1) in clean:
                value = clean.pop(m.group(1))
                if value is not None:
                    kept.append(line_for(m.group(1), value))
                continue
            kept.append(line)
        # New keys go after the last key line, before any trailing blank lines
        insert = len(kept)
        while insert > 1 and not kept[insert - 1].strip():
            insert -= 1
        kept[insert:insert] = [line_for(k, v) for k, v in clean.items() if v is not None]
        blocks[index] = (section, kept + tail)

    if comment is not None:
        before, _ = split_tail(blocks[index - 1])
        wanted = ["; " + line.strip() for line in str(comment).splitlines() if line.strip()]
        if wanted and before and before[-1].strip():
            before.append("")
        blocks[index - 1] = (blocks[index - 1][0], before + wanted)
        own, tail = split_tail(blocks[index])
        end = keys_end(own)
        blocks[index] = (section, [own[0]] + [line for line in own[1:end] if not is_comment(line)] + own[end:] + tail)

def update_section(section, changes, delete=False, blank=(), comment=None, inventory=None):
    with INI_LOCK:
        blocks = read_ini()
        edit_section(blocks, section, changes, delete, blank, comment)
        if inventory:
            edit_section(blocks, "inventory", inventory)
        write_ini(blocks)


def journal():
    """Every section that is not a room, grouped by kind, and each room's keys."""
    out = {name: [] for name in LISTS.values()}
    out.update({name: {"keys": {}, "comment": ""} for name in SINGLE})
    out["rooms"] = {}
    blocks = read_ini()
    for index, (name, lines) in enumerate(blocks):
        if not name:
            continue
        keys = section_keys(split_tail(blocks[index])[0])
        kind, _, ident = name.partition(".")
        if kind in LISTS and ident:
            out[LISTS[kind]].append({**keys, "id": ident, "_comment": comment_of(blocks, index)})
        elif name in SINGLE:
            out[name] = {"keys": keys, "comment": comment_of(blocks, index)}
        elif re.fullmatch(r"room\d+", name):
            out["rooms"][name[4:]] = keys
    return out


def rooms():
    blocks = read_ini()
    found = {name: index for index, (name, _) in enumerate(blocks) if name}
    grouped = {}
    for file in sorted(os.listdir(EXTRACT_DIR)):
        m = re.fullmatch(r"room(\d+)(?:--([\w-]+))?\.json", file)
        if not m:
            continue
        try:
            with open(os.path.join(EXTRACT_DIR, file), encoding="utf-8") as f:
                room = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            if m[2]:  # A live capture may still be arriving.
                continue
            raise
        image = file[:-5] + ".png"
        if m[2] and not os.path.isfile(os.path.join(EXTRACT_DIR, image)):
            continue
        room["image"] = "img/" + image
        room["label"] = m[2].replace("-", " ").replace("_", " ") if m[2] else "Room survey"
        grouped.setdefault(room["room"], []).append(room)

    out = []
    for number, captures in sorted(grouped.items()):
        captures.sort(key=lambda c: c["label"] != "Room survey")
        room = captures[0].copy()
        room["captures"] = captures
        index = found.get(f"room{number}")
        room["ini"] = {
            "keys": section_keys(split_tail(blocks[index])[0]) if index is not None else {},
            "comment": comment_of(blocks, index) if index is not None else "",
        }
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
        elif self.path == "/api/journal":
            self._send(200, json.dumps(journal()).encode(), "application/json")
        elif self.path == "/api/lines":
            path = os.path.join(EXTRACT_DIR, "lines.json")
            if os.path.exists(path):
                with open(path, "rb") as f:
                    self._send(200, f.read(), "application/json")
            else:
                self._send(200, b'{"rooms":{},"lines":[]}', "application/json")
        elif re.fullmatch(r"/img/room\d+(?:--[\w-]+)?\.png", self.path):
            with open(os.path.join(EXTRACT_DIR, os.path.basename(self.path)), "rb") as f:
                self._send(200, f.read(), "image/png")
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        fields = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)))
        try:
            if self.path == "/api/section":
                # {"section": "clue.flower", "set": {"goal": "idol", ...}, "blank": [...], "comment": "..."}
                # or {"section": ..., "delete": true}
                update_section(fields["section"], fields.get("set") or {}, bool(fields.get("delete")),
                               fields.get("blank") or (), fields.get("comment"), fields.get("inventory"))
            else:
                self._send(404, b"not found", "text/plain")
                return
        except (ValueError, KeyError) as e:
            self._send(400, json.dumps({"error": str(e)}).encode(), "application/json")
            return
        self._send(200, b'{"ok":true}', "application/json")

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    print(f"Reviewing {EXTRACT_DIR} against {INI_FILE}: http://127.0.0.1:{PORT}/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
