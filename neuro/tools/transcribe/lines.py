#!/usr/bin/env python3
"""lines.py SCRIPT_DUMP_DIR OUTPUT.json [GAME.json]: every line of text in a SCUMM v5 game's scripts.

Reads the raw rooms and global scripts written by the Neuro module's NEURO_DUMP_SCRIPTS
mode and finds every instruction that carries text: lines printed for an actor, lines
the player's character says, dialogue choices (verb names), actor names, object names and loaded
strings. For each it records where it is (room, script, and for an object's script
which verb it answers), who says it as far as the script shows, and the text colour.

This is not a full decompiler. It looks for the instructions that carry text and
parses those exactly; a byte sequence only counts if the whole instruction parses and
the text looks like text. That finds what the game can say; it does not know when.

GAME.json (neuro/games/NAME/game.json) names the game's verb numbers; without it they
are given as numbers.
"""
import json
import os
import re
import struct
import sys

# Names for the game's verb numbers, from the "verbs" of a game.json; 255 is the same in every game
VERB_NAMES = {255: "(any other verb)"}


def load_verb_names(game_json):
    VERB_NAMES.update({int(number): name for number, name in json.load(open(game_json, encoding="utf-8")).get("verbs", {}).items()})


class Bad(Exception):
    pass


def blocks(data, start, end):
    """The (tag, offset, size) of each block between two offsets of a big-header resource."""
    pos = start
    while pos + 8 <= end:
        tag = data[pos:pos + 4]
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if size < 8 or pos + size > end or not re.fullmatch(rb"[A-Za-z0-9 ]{4}", tag):
            return
        yield tag.decode(), pos, size
        pos += size


class Reader:
    def __init__(self, data, pos):
        self.data, self.pos = data, pos

    def byte(self):
        if self.pos >= len(self.data):
            raise Bad
        self.pos += 1
        return self.data[self.pos - 1]

    def word(self):
        if self.pos + 2 > len(self.data):
            raise Bad
        self.pos += 2
        return struct.unpack("<H", self.data[self.pos - 2:self.pos])[0]

    def byte_param(self, opcode, mask):
        """A byte, or a variable reference if the opcode's flag for this parameter is set."""
        return ("var", self.word()) if opcode & mask else self.byte()

    def word_param(self, opcode, mask):
        value = self.word()
        return ("var", value) if opcode & mask else value

    def string(self):
        """A text string up to its 0 byte, as a list of plain text and escape codes."""
        parts, text = [], bytearray()
        while True:
            c = self.byte()
            if c == 0:
                break
            if c in (0xFF, 0xFE):
                code = self.byte()
                if text:
                    parts.append(bytes(text))
                    text = bytearray()
                if code in (1, 2, 3, 8):
                    parts.append(("code", code))
                elif code in (4, 5, 6, 7, 9, 10, 12, 13, 14):
                    parts.append(("code", code, self.word()))
                else:
                    raise Bad
            else:
                text.append(c)
        if text:
            parts.append(bytes(text))
        return parts


def plain(parts):
    """What the bridge's cleanText would send for this string; substitutions become {...}."""
    out = ""
    for part in parts:
        if isinstance(part, bytes):
            for c in part:
                if c == ord("@") or c < 0x20:
                    continue
                out += "..." if c == ord("^") else " " if c == 0xFA else bytes([c]).decode("cp437")
        elif part[1] in (1, 3):
            out += " "
        elif part[1] in (4, 5, 6, 7):
            out += "{" + {4: "number", 5: "verb", 6: "name", 7: "string"}[part[1]] + "}"
    return re.sub(r" +", " ", out).strip()


def looks_like_text(parts, min_letters=2):
    raw = b"".join(p for p in parts if isinstance(p, bytes))
    if not raw and not any(not isinstance(p, bytes) and p[1] in (4, 5, 6, 7) for p in parts):
        return False
    if any(c < 0x20 and c not in (0x0F, 0x10, 0x11, 0x1C) for c in raw):
        return False
    letters = sum(1 for c in raw if chr(c).isalpha())
    return letters >= min_letters or (not raw)


def parse_print(r, opcode, with_actor):
    actor = r.byte_param(opcode, 0x80) if with_actor else "ego"
    color = None
    while True:
        sub = r.byte()
        if sub == 0xFF:
            # No text: this only sets the style that later prints of this kind start from.
            # Worth keeping when it sets a colour (and long enough not to be a chance match).
            if not isinstance(color, int):
                raise Bad
            return [{"op": "style", "actor": actor, "color": color}]
        kind = sub & 0x0F
        if kind == 0:
            r.word(), r.word()
        elif kind == 1:
            color = r.byte_param(sub, 0x80)
        elif kind == 2:
            r.word()
        elif kind in (3, 8):
            r.word(), r.word()
        elif kind in (4, 6, 7):
            pass
        elif kind == 15:
            parts = r.string()
            if not looks_like_text(parts):
                raise Bad
            return [{"op": "say", "actor": actor, "color": color, "parts": parts}]
        else:
            raise Bad


def parse_verb_ops(r, opcode):
    verb = r.byte_param(opcode, 0x80)
    found = []
    while True:
        sub = r.byte()
        if sub == 0xFF:
            break
        kind = sub & 0x1F
        if kind == 2:
            parts = r.string()
            if not looks_like_text(parts, 1):
                raise Bad
            found.append({"op": "choice", "verb": verb, "parts": parts})
        elif kind in (1, 20):
            r.word()
        elif kind in (3, 4, 16, 18, 23):
            r.byte_param(sub, 0x80)
        elif kind == 5:
            r.word(), r.word()
        elif kind in (6, 7, 8, 9, 17, 19):
            pass
        elif kind == 22:
            r.word(), r.byte_param(sub, 0x40)
        else:
            raise Bad
    if not found:
        raise Bad
    return found


ACTOR_SUBOP_BYTES = {0: 1, 1: 1, 2: 2, 3: 1, 4: 1, 5: 2, 6: 1, 7: 3, 8: 0, 10: 0, 11: 2, 14: 1,
                     16: 1, 17: 2, 18: 0, 19: 1, 20: 0, 21: 0, 22: 1, 23: 1}


def parse_actor_ops(r, opcode):
    actor = r.byte_param(opcode, 0x80)
    info = {"op": "actor", "actor": actor}
    while True:
        sub = r.byte()
        if sub == 0xFF:
            break
        kind = sub & 0x1F
        if kind == 13:
            parts = r.string()
            if not looks_like_text(parts, 1):
                raise Bad
            info["parts"] = parts
        elif kind == 12:
            info["talk_color"] = r.byte_param(sub, 0x80)
        elif kind == 9:
            r.word()
        elif kind in ACTOR_SUBOP_BYTES:
            for n in range(ACTOR_SUBOP_BYTES[kind]):
                r.byte_param(sub, 0x80 >> n)
        else:
            raise Bad
    if "parts" not in info and "talk_color" not in info:
        raise Bad
    return [info]


def parse_object_name(r, opcode):
    obj = r.word_param(opcode, 0x80)
    parts = r.string()
    if not looks_like_text(parts, 1):
        raise Bad
    return [{"op": "object_name", "object": obj, "parts": parts}]


def parse_load_string(r, opcode):
    sub = r.byte()
    if sub & 0x1F != 1:
        raise Bad
    r.byte_param(sub, 0x80)
    parts = r.string()
    if not looks_like_text(parts):
        raise Bad
    return [{"op": "string", "parts": parts}]


PARSERS = {
    0x14: lambda r, op: parse_print(r, op, True), 0x94: lambda r, op: parse_print(r, op, True),
    0xD8: lambda r, op: parse_print(r, op, False),
    0x7A: parse_verb_ops, 0xFA: parse_verb_ops,
    0x13: parse_actor_ops, 0x53: parse_actor_ops, 0x93: parse_actor_ops, 0xD3: parse_actor_ops,
    0x54: parse_object_name, 0xD4: parse_object_name,
    0x27: parse_load_string,
}


def scan(code, base=0):
    """Every text-carrying instruction in a stretch of script code, with its offset."""
    found, pos = [], 0
    while pos < len(code):
        parser = PARSERS.get(code[pos])
        if parser:
            reader = Reader(code, pos + 1)
            try:
                for item in parser(reader, code[pos]):
                    item["offset"] = base + pos
                    found.append(item)
                pos = reader.pos
                continue
            except Bad:
                pass
        pos += 1
    return found


def verb_entries(verb_block):
    """An object's VERB block: [(verb id, offset of its code from the block start)], and where code starts."""
    entries, pos = [], 8
    while pos < len(verb_block) and verb_block[pos] != 0:
        entries.append((verb_block[pos], struct.unpack("<H", verb_block[pos + 1:pos + 3])[0]))
        pos += 3
    return entries, pos + 1


def read_room(data, room):
    scripts, objects, palette = [], {}, None
    for tag, pos, size in blocks(data, 8, len(data)):
        body = data[pos + 8:pos + size]
        if tag == "CLUT":
            palette = ["#%02x%02x%02x" % tuple(body[i:i + 3]) for i in range(0, min(len(body), 768), 3)]
        elif tag in ("ENCD", "EXCD"):
            scripts.append(({"kind": "enter" if tag == "ENCD" else "exit"}, body, 8))
        elif tag == "LSCR":
            scripts.append(({"kind": "local", "script": body[0]}, body[1:], 9))
        elif tag == "OBCD":
            obj_id, name, verbs = None, "", None
            for sub, spos, ssize in blocks(data, pos + 8, pos + size):
                sbody = data[spos + 8:spos + ssize]
                if sub == "CDHD":
                    obj_id = struct.unpack("<H", sbody[:2])[0]
                elif sub == "OBNA":
                    name = plain(Reader(sbody, 0).string()) if sbody and sbody[0] else ""
                elif sub == "VERB":
                    verbs = data[spos:spos + ssize]
            if obj_id is None:
                continue
            objects[obj_id] = name
            if verbs:
                entries, code_start = verb_entries(verbs)
                scripts.append(({"kind": "object", "object": obj_id, "object_name": name, "entries": entries},
                                verbs[code_start:], code_start))
    return scripts, objects, palette


def main():
    dump, out = sys.argv[1], sys.argv[2]
    if len(sys.argv) > 3:
        load_verb_names(sys.argv[3])
    rooms, lines, actor_info = {}, [], {}
    global_rooms = {}
    for entry in open(os.path.join(dump, "index.txt")):
        m = re.match(r"script (\d+) room (\d+)", entry)
        if m:
            global_rooms[int(m.group(1))] = int(m.group(2))

    sources = []
    for name in sorted(os.listdir(dump)):
        m = re.fullmatch(r"room(\d+)\.room", name)
        if m:
            room = int(m.group(1))
            data = open(os.path.join(dump, name), "rb").read()
            scripts, objects, palette = read_room(data, room)
            rooms[room] = {"objects": objects, "palette": palette}
            sources += [(room, where, code, base) for where, code, base in scripts]
        m = re.fullmatch(r"script(\d+)\.scrp", name)
        if m:
            data = open(os.path.join(dump, name), "rb").read()
            sources.append((0, {"kind": "global", "script": int(m.group(1))}, data[8:], 8))

    for room, where, code, base in sources:
        entries = sorted(where.pop("entries", []), key=lambda e: e[1])
        for item in scan(code, base):
            item["room"] = room
            item["where"] = dict(where)
            if entries:
                verb = [v for v, offset in entries if offset <= item["offset"]]
                if verb:
                    item["where"]["verb"] = verb[-1]
                    item["where"]["verb_name"] = VERB_NAMES.get(verb[-1], f"verb {verb[-1]}")
            parts = item.pop("parts", None)
            if parts is not None:
                item["text"] = plain(parts)
                item["has_substitution"] = "{" in item["text"]
            if item["op"] == "actor":
                key = (room, json.dumps(item["actor"]))
                info = actor_info.setdefault(key, {})
                if "text" in item:
                    info["name"] = item["text"]
                if isinstance(item.get("talk_color"), int):
                    info["talk_color"] = item["talk_color"]
            lines.append(item)

    # Talk colours by actor over the whole game, for actors whose colour is set
    # in another room or in a script that runs everywhere.
    everywhere, anywhere = {}, {}
    for item in lines:
        if item["op"] == "actor" and isinstance(item.get("talk_color"), int):
            key = json.dumps(item["actor"])
            anywhere.setdefault(key, []).append(item["talk_color"])
            if item["room"] == 0:
                everywhere.setdefault(key, []).append(item["talk_color"])

    def most_common(values):
        return max(set(values), key=values.count)

    # Guybrush is actor 1, usually reached through the "ego" variable (variable 1)
    ego_colors = [c for key in ("1", json.dumps(["var", 1])) for c in anywhere.get(key, [])]
    ego_color = most_common(ego_colors) if ego_colors else None

    def text_slot(actor):
        """Prints share remembered styles by slot: three special ones and one for everything else."""
        return {252: 3, 253: 2, 254: 1}.get(actor, 0) if isinstance(actor, int) else 0

    def script_key(item):
        w = item["where"]
        return (item["room"], w["kind"], w.get("script"), w.get("object"))

    styles = {}        # script -> [(offset, slot, colour)]
    for item in lines:
        if item["op"] == "style":
            styles.setdefault(script_key(item), []).append((item["offset"], text_slot(item["actor"]), item["color"]))
    entry_styles = {}  # room -> last colour its entry script sets, per slot
    for (room, kind, _, _), found in styles.items():
        if kind == "enter":
            for _, slot, color in sorted(found):
                entry_styles[(room, slot)] = color

    # Who says each line and in what colour, as far as the scripts show.
    # color_sure is false where the script only makes a colour likely.
    for item in lines:
        if item["op"] != "say":
            continue
        actor = item["actor"]
        if item["color"] is not None and not isinstance(item["color"], int):
            item["color"] = None                       # a colour taken from a variable
        if isinstance(item["color"], int):
            item["color_from"], item["color_sure"] = "print", True

        if actor == "ego":
            item["speaker"] = "Guybrush"
            if item["color"] is None and ego_color is not None:
                item["color"], item["color_from"] = ego_color, "ego"
                item["color_sure"] = len(set(ego_colors)) == 1
        elif isinstance(actor, int) and actor < 252:
            key = json.dumps(actor)
            info = actor_info.get((item["room"], key)) or {}
            item["speaker"] = info.get("name", "")
            if item["color"] is None and "talk_color" in info:
                item["color"], item["color_from"] = info["talk_color"], "actor"
                # one actor can stand in for several people, each with a colour
                item["color_sure"] = len({l["talk_color"] for l in lines if l["op"] == "actor" and l["room"] == item["room"]
                                          and json.dumps(l["actor"]) == key and isinstance(l.get("talk_color"), int)}) == 1
            elif item["color"] is None:
                found = everywhere.get(key) or anywhere.get(key)
                if found:
                    item["color"], item["color_from"] = most_common(found), "actor-elsewhere"
                    item["color_sure"] = False
        elif isinstance(actor, int):
            item["speaker"] = ""                       # printed on screen, not by a character
            if item["color"] is None:
                slot = text_slot(actor)
                earlier = [c for offset, s, c in sorted(styles.get(script_key(item), [])) if s == slot and offset < item["offset"]]
                if earlier:
                    item["color"], item["color_from"] = earlier[-1], "style"
                elif (item["room"], slot) in entry_styles:
                    item["color"], item["color_from"] = entry_styles[(item["room"], slot)], "room-style"
                if item["color"] is not None:
                    item["color_sure"] = False
        else:
            item["speaker"] = ""

    lines = [l for l in lines if l["op"] != "style"]
    said = [l for l in lines if l["op"] == "say"]
    sure = sum(1 for l in said if l.get("color_sure"))
    likely = sum(1 for l in said if isinstance(l.get("color"), int) and not l.get("color_sure"))
    print(f"text colour: {sure} certain, {likely} likely, {len(said) - sure - likely} unknown")
    print(f"{len(rooms)} rooms, {len(sources)} scripts, {len(lines)} text items: {len(said)} lines said, "
          f"{sum(1 for l in lines if l['op'] == 'choice')} dialogue choices, "
          f"{sum(1 for l in said if isinstance(l['actor'], int) and l['actor'] >= 252)} printed with no actor")
    json.dump({"rooms": {str(k): v for k, v in sorted(rooms.items())}, "lines": lines}, open(out, "w"), ensure_ascii=False)


if __name__ == "__main__":
    main()
