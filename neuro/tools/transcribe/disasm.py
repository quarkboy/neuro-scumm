#!/usr/bin/env python3
"""disasm.py: a SCUMM v5 script disassembler (big-header resources, e.g. Monkey Island 1 CD).

Usage:
    disasm.py SCRIPT_DUMP_DIR room N          every script of room N
    disasm.py SCRIPT_DUMP_DIR script N        global script N
    disasm.py SCRIPT_DUMP_DIR --out DIR       one text file per room and per global script
    options: --decimal   print offsets in decimal instead of hex

SCRIPT_DUMP_DIR is the directory written by the Neuro module's NEURO_DUMP_SCRIPTS mode
(roomNNN.room = whole ROOM block, scriptNNN.scrp = whole SCRP block).

Every opcode is decoded following ScummVM's engines/scumm/script_v5.cpp (version 5,
non-FM-Towns, non-small-header branches), so the decoder never has to resynchronise.

Reading the output
  [0123]  offset of the instruction, in hex unless --decimal. Offsets are the ones ScummVM
          itself uses (_scriptPointer - _scriptOrgPointer) and the ones lines.py records:
          from the start of the block including its 8-byte header (ENCD/EXCD/SCRP code
          starts at 8, LSCR code at 9 after the script-number byte, object code is
          addressed from the start of the VERB block like the verb table's offsets).
  VAR[n name]   global variable (name = ScummVM's VAR_ name for v5, if it has one)
  Local[n]      script-local variable        Bit[n]   bit variable
  VAR[100 + Local[2]]   array-indexed form (base + index)
  unless (cond) goto X  jump to X when cond is false (that is how every SCUMM
                        conditional works); `goto X` is unconditional
  classes       in setClass / ifClassOfIs a constant class is shown as +n (set / has
                class n) or -n (clear / has not), `0` in setClass clears all classes
  obj numbers   a constant object number is followed by its name when a room defines one
  strings       0xFF escape codes are shown as {newline} {keep} {wait} {int v} {verb v}
                {name v} {string v} {anim n} {sound n} {color n} {font n}
"""
import os
import re
import struct
import sys

VERB_NAMES = {1: "(inventory)", 2: "Open", 3: "Close", 4: "Give", 5: "Push", 6: "Pull", 7: "Use",
              8: "Look at", 9: "Pick up", 10: "Talk to", 11: "Walk to", 255: "(any other verb)"}

VAR_NAMES = {
    0: "keypress", 1: "ego", 2: "camera_pos_x", 3: "have_msg", 4: "room", 5: "override",
    6: "machine_speed", 7: "me", 8: "num_actor", 9: "current_lights", 10: "currentdrive",
    11: "tmr_1", 12: "tmr_2", 13: "tmr_3", 14: "music_timer", 15: "actor_range_min",
    16: "actor_range_max", 17: "camera_min_x", 18: "camera_max_x", 19: "timer_next",
    20: "virt_mouse_x", 21: "virt_mouse_y", 22: "room_resource", 23: "last_sound",
    24: "cutsceneexit_key", 25: "talk_actor", 26: "camera_fast_x", 27: "scroll_script",
    28: "entry_script", 29: "entry_script2", 30: "exit_script", 31: "exit_script2",
    32: "verb_script", 33: "sentence_script", 34: "inventory_script",
    35: "cutscene_start_script", 36: "cutscene_end_script", 37: "charinc", 38: "walkto_obj",
    39: "debugmode", 40: "heapspace", 42: "restart_key", 43: "pause_key", 44: "mouse_x",
    45: "mouse_y", 46: "timer", 47: "timer_total", 48: "soundcard", 49: "videomode",
    50: "mainmenu_key", 51: "fixeddisk", 52: "cursorstate", 53: "userput",
    54: "talk_string_y", 56: "soundresult", 57: "talkstop_key", 59: "fade_delay",
    60: "nosubtitles", 64: "soundparam", 65: "soundparam2", 66: "soundparam3",
    67: "inputmode", 68: "memory_performance", 69: "video_performance", 70: "room_flag",
    71: "game_loaded", 72: "new_room",
}

# From ScummEngine_v5::setupOpcodes (commented-out entries there are unknown opcodes here).
OPCODE_TABLE = {
    "stopObjectCode": "00 a0", "putActor": "01 21 41 61 81 a1 c1 e1", "startMusic": "02 82",
    "getActorRoom": "03 83", "isGreaterEqual": "04 84", "drawObject": "05 85",
    "getActorElevation": "06 86", "setState": "07 47 87 c7", "isNotEqual": "08 88",
    "faceActor": "09 49 89 c9", "startScript": "0a 2a 4a 6a 8a aa ca ea",
    "getVerbEntrypoint": "0b 4b 8b cb", "resourceRoutines": "0c 8c",
    "walkActorToActor": "0d 4d 8d cd", "putActorAtObject": "0e 4e 8e ce",
    "getObjectState": "0f 8f", "getObjectOwner": "10 90", "animateActor": "11 51 91 d1",
    "panCameraTo": "12 92", "actorOps": "13 53 93 d3", "print": "14 94",
    "actorFromPos": "15 55 95 d5", "getRandomNr": "16 96", "and": "17 97",
    "jumpRelative": "18", "doSentence": "19 39 59 79 99 b9 d9 f9", "move": "1a 9a",
    "multiply": "1b 9b", "startSound": "1c 9c", "ifClassOfIs": "1d 9d",
    "walkActorTo": "1e 3e 5e 7e 9e be de fe", "isActorInBox": "1f 5f 9f df",
    "stopMusic": "20", "getAnimCounter": "22 a2", "getActorY": "23 a3",
    "loadRoomWithEgo": "24 64 a4 e4", "pickupObject": "25 65 a5 e5", "setVarRange": "26 a6",
    "stringOps": "27", "equalZero": "28", "setOwnerOf": "29 69 a9 e9", "delayVariable": "2b",
    "cursorCommand": "2c", "putActorInRoom": "2d 6d ad ed", "delay": "2e",
    "matrixOps": "30 b0", "getInventoryCount": "31 b1", "setCameraAt": "32 b2",
    "roomOps": "33 73 b3 f3", "getDist": "34 74 b4 f4", "findObject": "35 75 b5 f5",
    "walkActorToObject": "36 76 b6 f6", "startObject": "37 77 b7 f7", "isLessEqual": "38 b8",
    "subtract": "3a ba", "getActorScale": "3b bb", "stopSound": "3c bc",
    "findInventory": "3d 7d bd fd", "drawBox": "3f 7f bf ff", "cutscene": "40",
    "chainScript": "42 c2", "getActorX": "43 c3", "isLess": "44 c4", "increment": "46",
    "isEqual": "48 c8", "soundKludge": "4c", "actorFollowCamera": "52 d2",
    "setObjectName": "54 d4", "getActorMoving": "56 d6", "or": "57 d7", "beginOverride": "58",
    "add": "5a da", "divide": "5b db", "setClass": "5d dd", "freezeScripts": "60 e0",
    "stopScript": "62 e2", "getActorFacing": "63 e3", "getClosestObjActor": "66 e6",
    "getStringWidth": "67 e7", "isScriptRunning": "68 e8", "debug": "6b eb",
    "getActorWidth": "6c ec", "stopObjectScript": "6e ee", "lights": "70 f0",
    "getActorCostume": "71 f1", "loadRoom": "72 f2", "isGreater": "78 f8", "verbOps": "7a fa",
    "getActorWalkBox": "7b fb", "isSoundRunning": "7c fc", "breakHere": "80",
    "systemOps": "98", "dummy": "a7", "notEqualZero": "a8", "saveRestoreVerbs": "ab",
    "expression": "ac", "wait": "ae", "endCutscene": "c0", "decrement": "c6",
    "pseudoRoom": "cc", "printEgo": "d8",
}
OPCODES = {int(code, 16): name for name, codes in OPCODE_TABLE.items() for code in codes.split()}

P1, P2, P3 = 0x80, 0x40, 0x20


class Unknown(Exception):
    """An opcode or sub-opcode the v5 interpreter would reject."""


class RanOff(Exception):
    """The instruction needs bytes beyond the end of the script."""


def quote(raw):
    out = ""
    for c in raw:
        if c == 0x22 or c == 0x5C:
            out += "\\" + chr(c)
        elif c < 0x20 or c == 0x7F:
            out += "\\x%02x" % c
        else:
            out += bytes([c]).decode("cp437")
    return out


class Ins:
    __slots__ = ("offset", "text", "target", "name", "strings", "end")

    def __init__(self, offset, text, target, name, strings, end):
        self.offset, self.text, self.target, self.name, self.strings, self.end = offset, text, target, name, strings, end


class Decoder:
    def __init__(self, code, base=0, obj_names=None, fmt_off=None):
        self.data, self.base, self.pos = code, base, 0
        self.obj_names = obj_names or {}
        self.fmt_off = fmt_off or (lambda n: "%04X" % n)
        self.op = 0
        self.target = None
        self.strings = []

    # ---- raw fetches -------------------------------------------------------------
    def u8(self):
        if self.pos >= len(self.data):
            raise RanOff
        self.pos += 1
        return self.data[self.pos - 1]

    def u16(self):
        if self.pos + 2 > len(self.data):
            raise RanOff
        self.pos += 2
        return self.data[self.pos - 2] | self.data[self.pos - 1] << 8

    def s16(self):
        v = self.u16()
        return v - 0x10000 if v & 0x8000 else v

    # ---- variables ---------------------------------------------------------------
    @staticmethod
    def var_name(v, index=None):
        """v has no 0x2000 bit. Same precedence as ScummEngine::readVar."""
        plus = "" if index is None else " + " + index
        if not v & 0xF000:
            name = VAR_NAMES.get(v)
            return "VAR[%d%s%s]" % (v, " " + name if name else "", plus)
        if v & 0x8000:
            return "Bit[%d%s]" % (v & 0x7FFF, plus)
        if v & 0x4000:
            return "Local[%d%s]" % (v & 0xFFF, plus)
        return "BadVar[0x%04x%s]" % (v, plus)

    def var(self):
        """A variable reference: getVar / getResultPos."""
        v = self.u16()
        if v & 0x2000:
            a = self.u16()
            index = self.var_name(a & ~0x2000) if a & 0x2000 else str(a & 0xFFF)
            return self.var_name(v & ~0x2000, index)
        return self.var_name(v)

    def pb(self, mask):
        return self.var() if self.op & mask else str(self.u8())

    def pw(self, mask):
        return self.var() if self.op & mask else str(self.s16())

    def obj(self, mask):
        """A word parameter that is an object number: add the object's name if known."""
        if self.op & mask:
            return self.var()
        n = self.s16()
        name = self.obj_names.get(n)
        return '%d "%s"' % (n, name) if name else str(n)

    def cls(self, mask, setting):
        if self.op & mask:
            return self.var()
        n = self.s16()
        if n == 0 and setting:
            return "0"
        return ("+" if n & 0x80 else "-") + str(n & 0x7F)

    def vararg(self):
        items = []
        while True:
            self.op = self.u8()
            if self.op == 0xFF:
                return "[" + ", ".join(items) + "]"
            items.append(self.pw(P1))

    def string(self):
        """A 0-terminated message (resStrLen): 0xFF escapes take 0 or 2 more bytes."""
        start = self.pos
        out, text = "", bytearray()
        while True:
            c = self.u8()
            if c == 0:
                break
            if c != 0xFF:
                text.append(c)
                continue
            out += quote(text)
            text = bytearray()
            code = self.u8()
            if code in (1, 2, 3, 8):
                out += {1: "{newline}", 2: "{keep}", 3: "{wait}", 8: "{code8}"}[code]
            else:
                v = self.u16()
                if code in (4, 5, 6, 7):
                    ref = self.var_name(v) if not v & 0x2000 else "0x%04x" % v
                    out += "{%s %s}" % ({4: "int", 5: "verb", 6: "name", 7: "string"}[code], ref)
                else:
                    kind = {9: "anim", 10: "sound", 12: "color", 13: "code13", 14: "font"}.get(code, "code%d" % code)
                    out += "{%s %d}" % (kind, v)
        out += quote(text)
        self.strings.append(bytes(self.data[start:self.pos - 1]))
        return '"' + out + '"'

    def jump(self):
        offset = self.s16()
        self.target = self.pos + offset
        return "goto " + self.fmt_off(self.base + self.target)

    # ---- driver ------------------------------------------------------------------
    def one(self):
        """Decode one instruction at self.pos; returns its text."""
        self.op = opcode = self.u8()
        name = OPCODES.get(opcode)
        if name is None:
            raise Unknown("0x%02X" % opcode)
        self.name = name
        return getattr(self, "o_" + name)()

    def run(self):
        """[Ins...], status ('ok' | 'unknown' | 'ranoff'), detail."""
        out = []
        while self.pos < len(self.data):
            start = self.pos
            self.target, self.strings = None, []
            try:
                text = self.one()
            except Unknown as e:
                out.append(Ins(self.base + start, "?? " + str(e), None, "??", [], self.base + self.pos))
                return out, "unknown", str(e)
            except RanOff:
                out.append(Ins(self.base + start, "?? 0x%02X (runs past the end of the script)" % self.data[start],
                               None, "??", [], self.base + len(self.data)))
                return out, "ranoff", "0x%02X" % self.data[start]
            target = None if self.target is None else self.base + self.target
            out.append(Ins(self.base + start, text, target, self.name, self.strings, self.base + self.pos))
        return out, "ok", ""

    # ---- helpers for families ----------------------------------------------------
    def getter(self, fn, *kinds):
        res = self.var()
        args = []
        for kind, mask in zip(kinds, (P1, P2, P3)):
            args.append({"b": self.pb, "w": self.pw, "o": self.obj}[kind](mask))
        return "%s = %s(%s)" % (res, fn, ", ".join(args))

    def call(self, fn, *kinds):
        args = []
        for kind, mask in zip(kinds, (P1, P2, P3)):
            args.append({"b": self.pb, "w": self.pw, "o": self.obj}[kind](mask))
        return "%s(%s)" % (fn, ", ".join(args))

    def arith(self, sym):
        res = self.var()
        return "%s %s %s" % (res, sym, self.pw(P1))

    def compare(self, sym):
        # jumpRelative(b <op> a) with a = the variable, b = the operand; shown as "var sym operand"
        a = self.var()
        b = self.pw(P1)
        return "unless (%s %s %s) %s" % (a, sym, b, self.jump())

    # ---- opcodes -----------------------------------------------------------------
    def o_stopObjectCode(self):
        return "stopObjectCode()"

    def o_putActor(self):
        return self.call("putActor", "b", "w", "w")

    def o_startMusic(self):
        return self.call("startMusic", "b")

    def o_getActorRoom(self):
        return self.getter("getActorRoom", "b")

    def o_isGreaterEqual(self):
        return self.compare("<=")       # b >= a

    def o_isGreater(self):
        return self.compare("<")        # b > a

    def o_isLess(self):
        return self.compare(">")        # b < a

    def o_isLessEqual(self):
        return self.compare(">=")       # b <= a

    def o_isEqual(self):
        return self.compare("==")

    def o_isNotEqual(self):
        return self.compare("!=")

    def o_equalZero(self):
        return "unless (%s == 0) %s" % (self.var(), self.jump())

    def o_notEqualZero(self):
        return "unless (%s != 0) %s" % (self.var(), self.jump())

    def o_drawObject(self):
        obj = self.obj(P1)
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub == 1:
            x = self.pw(P1)
            return "drawObject(obj %s, at %s, %s)" % (obj, x, self.pw(P2))
        if sub == 2:
            return "drawObject(obj %s, state %s)" % (obj, self.pw(P1))
        if sub == 0x1F:
            return "drawObject(obj %s)" % obj
        raise Unknown("0x%02X (drawObject sub-op 0x%02X)" % (self.data[self.pos - 1], self.op))

    def o_getActorElevation(self):
        return self.getter("getActorElevation", "b")

    def o_setState(self):
        obj = self.obj(P1)
        return "setState(obj %s, %s)" % (obj, self.pb(P2))

    def o_faceActor(self):
        return self.call("faceActor", "b", "o")

    def o_startScript(self):
        op = self.op
        script = self.pb(P1)
        args = self.vararg()
        flags = (", freezeResistant" if op & 0x20 else "") + (", recursive" if op & 0x40 else "")
        return "startScript(%s, %s%s)" % (script, args, flags)

    def o_getVerbEntrypoint(self):
        return self.getter("getVerbEntrypoint", "o", "w")

    RES_OPS = {1: "loadScript", 2: "loadSound", 3: "loadCostume", 4: "loadRoom", 5: "nukeScript",
               6: "nukeSound", 7: "nukeCostume", 8: "nukeRoom", 9: "lockScript", 10: "lockSound",
               11: "lockCostume", 12: "lockRoom", 13: "unlockScript", 14: "unlockSound",
               15: "unlockCostume", 16: "unlockRoom", 17: "clearHeap", 18: "loadCharset",
               19: "nukeCharset", 20: "loadFlObject"}

    def o_resourceRoutines(self):
        self.op = sub = self.u8()
        res = None
        if sub != 17:
            res = self.pb(P1)
        op = sub & 0x3F
        if op not in self.RES_OPS:      # 32..37 exist only on FM-Towns
            raise Unknown("0x%02X (resourceRoutines sub-op 0x%02X)" % (self.data[self.pos - 1], sub))
        if op == 17:
            return "resourceRoutines.clearHeap()"
        if op == 20:
            return "resourceRoutines.loadFlObject(obj %s, room %s)" % (self.obj(P2), res)
        return "resourceRoutines.%s(%s)" % (self.RES_OPS[op], res)

    def o_walkActorToActor(self):
        a = self.pb(P1)
        b = self.pb(P2)
        return "walkActorToActor(%s, %s, dist %d)" % (a, b, self.u8())

    def o_putActorAtObject(self):
        a = self.pb(P1)
        return "putActorAtObject(%s, obj %s)" % (a, self.obj(P2))

    def o_getObjectState(self):
        return self.getter("getObjectState", "o")

    def o_getObjectOwner(self):
        return self.getter("getObjectOwner", "o")

    def o_animateActor(self):
        return self.call("animateActor", "b", "b")

    def o_panCameraTo(self):
        return self.call("panCameraTo", "w")

    ACTOR_OPS = {0: ("Dummy", "b"), 1: ("Costume", "b"), 2: ("StepDist", "bb"), 3: ("Sound", "b"),
                 4: ("WalkAnim", "b"), 5: ("TalkAnim", "bb"), 6: ("StandAnim", "b"),
                 7: ("Anim", "bbb"), 8: ("Default", ""), 9: ("Elevation", "w"),
                 10: ("AnimDefault", ""), 11: ("Palette", "bb"), 12: ("TalkColor", "b"),
                 13: ("Name", "s"), 14: ("InitAnim", "b"), 16: ("Width", "b"), 17: ("Scale", "bb"),
                 18: ("NeverZClip", ""), 19: ("AlwaysZClip", "b"), 20: ("IgnoreBoxes", ""),
                 21: ("FollowBoxes", ""), 22: ("AnimSpeed", "b"), 23: ("ShadowMode", "b")}

    VERB_OPS = {1: ("Image", "w"), 2: ("Name", "s"), 3: ("Color", "b"), 4: ("HiColor", "b"),
                5: ("At", "ww"), 6: ("On", ""), 7: ("Off", ""), 8: ("Delete", ""), 9: ("New", ""),
                16: ("DimColor", "b"), 17: ("Dim", ""), 18: ("Key", "b"), 19: ("Center", ""),
                20: ("NameString", "w"), 22: ("AssignObject", "ob"), 23: ("BackColor", "b")}

    def subop_list(self, table, mask, what):
        """Sub-ops up to 0xFF; each sub-op byte carries the PARAM flags for its own operands."""
        items = []
        while True:
            self.op = self.u8()
            if self.op == 0xFF:
                return "[" + ", ".join(items) + "]"
            entry = table.get(self.op & mask)
            if entry is None:
                raise Unknown("0x%02X (%s sub-op 0x%02X)" % (self.data[self.pos - 1], what, self.op))
            items.append(self.subop(entry))

    def subop(self, entry):
        name, kinds = entry
        args = []
        for kind, pmask in zip(kinds, (P1, P2, P3)):
            if kind == "s":
                args.append(self.string())
            else:
                args.append({"b": self.pb, "w": self.pw, "o": self.obj}[kind](pmask))
        return "%s(%s)" % (name, ", ".join(args)) if kinds else name

    def o_actorOps(self):
        actor = self.pb(P1)
        return "actorOps(%s, %s)" % (actor, self.subop_list(self.ACTOR_OPS, 0x1F, "actorOps"))

    def o_verbOps(self):
        verb = self.pb(P1)
        return "verbOps(%s, %s)" % (verb, self.subop_list(self.VERB_OPS, 0x1F, "verbOps"))

    PRINT_OPS = {0: ("At", "ww"), 1: ("Color", "b"), 2: ("Clipped", "w"), 3: ("Erase", "ww"),
                 4: ("Center", ""), 6: ("Left", ""), 7: ("Overhead", ""), 8: ("SayVoice", "ww"),
                 15: ("Text", "s")}

    def parse_string(self):
        """decodeParseString: sub-ops up to 0xFF, but the text sub-op ends the list itself."""
        items = []
        while True:
            self.op = self.u8()
            if self.op == 0xFF:
                break
            entry = self.PRINT_OPS.get(self.op & 0x0F)
            if entry is None:
                raise Unknown("0x%02X (print sub-op 0x%02X)" % (self.data[self.pos - 1], self.op))
            items.append(self.subop(entry))
            if self.op & 0x0F == 15:
                break
        return "[" + ", ".join(items) + "]"

    def o_print(self):
        actor = self.pb(P1)
        return "print(%s, %s)" % (actor, self.parse_string())

    def o_printEgo(self):
        return "printEgo(%s)" % self.parse_string()

    def o_actorFromPos(self):
        return self.getter("actorFromPos", "w", "w")

    def o_getRandomNr(self):
        return self.getter("getRandomNr", "b")

    def o_and(self):
        return self.arith("&=")

    def o_or(self):
        return self.arith("|=")

    def o_add(self):
        return self.arith("+=")

    def o_subtract(self):
        return self.arith("-=")

    def o_multiply(self):
        return self.arith("*=")

    def o_divide(self):
        return self.arith("/=")

    def o_move(self):
        return self.arith("=")

    def o_increment(self):
        return self.var() + "++"

    def o_decrement(self):
        return self.var() + "--"

    def o_jumpRelative(self):
        return self.jump()

    def o_doSentence(self):
        op = self.op
        verb = self.pb(P1)
        if not op & P1 and verb == "254":
            return "doSentence(STOP)"
        name = VERB_NAMES.get(int(verb)) if not op & P1 else None
        if name:
            verb += ' "%s"' % name
        a = self.obj(P2)
        return "doSentence(verb %s, obj %s, obj %s)" % (verb, a, self.obj(P3))

    def o_startSound(self):
        return self.call("startSound", "b")

    def class_list(self, setting):
        items = []
        while True:
            self.op = self.u8()
            if self.op == 0xFF:
                return "[" + ", ".join(items) + "]"
            items.append(self.cls(P1, setting))

    def o_ifClassOfIs(self):
        obj = self.obj(P1)
        classes = self.class_list(False)
        return "unless (classOf(obj %s) is %s) %s" % (obj, classes, self.jump())

    def o_setClass(self):
        obj = self.obj(P1)
        return "setClass(obj %s, %s)" % (obj, self.class_list(True))

    def o_walkActorTo(self):
        return self.call("walkActorTo", "b", "w", "w")

    def o_isActorInBox(self):
        a = self.pb(P1)
        b = self.pb(P2)
        return "unless (isActorInBox(%s, box %s)) %s" % (a, b, self.jump())

    def o_stopMusic(self):
        return "stopMusic()"

    def o_getAnimCounter(self):
        return self.getter("getAnimCounter", "b")

    def o_getActorY(self):
        return self.getter("getActorY", "o")

    def o_getActorX(self):
        return self.getter("getActorX", "o")

    def o_loadRoomWithEgo(self):
        obj = self.obj(P1)
        room = self.pb(P2)
        x = self.s16()
        return "loadRoomWithEgo(obj %s, room %s, %d, %d)" % (obj, room, x, self.s16())

    def o_pickupObject(self):
        obj = self.obj(P1)
        return "pickupObject(obj %s, room %s)" % (obj, self.pb(P2))

    def o_setVarRange(self):
        op = self.op
        res = self.var()
        count = self.u8()
        values = []
        while True:                     # a do/while in the interpreter: at least one value
            values.append(str(self.s16() if op & 0x80 else self.u8()))
            count = (count - 1) & 0xFF
            if count == 0:
                break
        return "setVarRange(%s, %d, [%s])" % (res, len(values), ", ".join(values))

    def o_stringOps(self):
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub == 1:
            a = self.pb(P1)
            return "stringOps.load(string %s, %s)" % (a, self.string())
        if sub == 2:
            a = self.pb(P1)
            return "stringOps.copy(string %s, from string %s)" % (a, self.pb(P2))
        if sub == 3:
            a, b = self.pb(P1), self.pb(P2)
            return "stringOps.setChar(string %s, index %s, %s)" % (a, b, self.pb(P3))
        if sub == 4:
            res = self.var()
            a = self.pb(P1)
            return "%s = stringOps.getChar(string %s, index %s)" % (res, a, self.pb(P2))
        if sub == 5:
            a = self.pb(P1)
            return "stringOps.create(string %s, size %s)" % (a, self.pb(P2))
        raise Unknown("0x%02X (stringOps sub-op 0x%02X)" % (self.data[self.pos - 1], self.op))

    def o_setOwnerOf(self):
        obj = self.obj(P1)
        return "setOwnerOf(obj %s, %s)" % (obj, self.pb(P2))

    def o_delayVariable(self):
        return "delayVariable(%s)" % self.var()

    CURSOR_OPS = {1: ("CursorOn", ""), 2: ("CursorOff", ""), 3: ("UserputOn", ""),
                  4: ("UserputOff", ""), 5: ("CursorSoftOn", ""), 6: ("CursorSoftOff", ""),
                  7: ("UserputSoftOn", ""), 8: ("UserputSoftOff", ""), 10: ("CursorImage", "bb"),
                  11: ("CursorHotspot", "bbb"), 12: ("CursorSet", "b"), 13: ("CharsetSet", "b")}

    def o_cursorCommand(self):
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub == 14:
            return "cursorCommand.CharsetColors(%s)" % self.vararg()
        if sub in self.CURSOR_OPS:
            return "cursorCommand." + self.subop(self.CURSOR_OPS[sub]) + ("" if self.CURSOR_OPS[sub][1] else "()")
        return "cursorCommand.nop_0x%02X()" % self.op       # the interpreter's default case reads nothing

    def o_putActorInRoom(self):
        a = self.pb(P1)
        return "putActorInRoom(%s, room %s)" % (a, self.pb(P2))

    def o_delay(self):
        n = self.u8()
        n |= self.u8() << 8
        n |= self.u8() << 16
        return "delay(%d)" % n

    def o_matrixOps(self):
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub in (1, 2, 3):
            a = self.pb(P1)
            b = self.pb(P2)
            return "matrixOps.%s(box %s, %s)" % ({1: "setBoxFlags", 2: "setBoxScale", 3: "setBoxScaleSlot"}[sub], a, b)
        if sub == 4:
            return "matrixOps.createBoxMatrix()"
        return "matrixOps.nop_0x%02X()" % self.op            # default case reads nothing

    def o_getInventoryCount(self):
        return self.getter("getInventoryCount", "b")

    def o_setCameraAt(self):
        return self.call("setCameraAt", "w")

    def o_roomOps(self):
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub == 1:
            a = self.pw(P1)
            return "roomOps.scroll(%s, %s)" % (a, self.pw(P2))
        if sub == 3:
            a = self.pw(P1)
            return "roomOps.screen(%s, %s)" % (a, self.pw(P2))
        if sub == 4:
            a, b, c = self.pw(P1), self.pw(P2), self.pw(P3)
            self.op = self.u8()
            return "roomOps.palette(r %s, g %s, b %s, color %s)" % (a, b, c, self.pb(P1))
        if sub == 5:
            return "roomOps.shakeOn()"
        if sub == 6:
            return "roomOps.shakeOff()"
        if sub == 7:
            a, b = self.pb(P1), self.pb(P2)
            self.op = self.u8()
            c, d = self.pb(P1), self.pb(P2)
            self.op = self.u8()
            return "roomOps.scale(scale1 %s, y1 %s, scale2 %s, y2 %s, slot %s)" % (a, b, c, d, self.pb(P2))
        if sub == 8:
            a, b = self.pb(P1), self.pb(P2)
            return "roomOps.intensity(%s, from %s, to %s)" % (a, b, self.pb(P3))
        if sub == 9:
            a = self.pb(P1)
            return "roomOps.saveGame(flag %s, slot %s)" % (a, self.pb(P2))
        if sub == 10:
            return "roomOps.fade(%s)" % self.pw(P1)
        if sub in (11, 12):
            a, b, c = self.pw(P1), self.pw(P2), self.pw(P3)
            self.op = self.u8()
            d = self.pb(P1)
            return "roomOps.%s(%s, %s, %s, from %s, to %s)" % (
                "rgbIntensity" if sub == 11 else "shadow", a, b, c, d, self.pb(P2))
        if sub in (13, 14):
            a = self.pb(P1)
            start = self.pos
            while self.u8():
                pass
            name = quote(self.data[start:self.pos - 1])
            return 'roomOps.%s(string %s, file "%s")' % ("saveString" if sub == 13 else "loadString", a, name)
        if sub == 15:
            a = self.pb(P1)
            self.op = self.u8()
            b, c = self.pb(P1), self.pb(P2)
            self.op = self.u8()
            return "roomOps.transform(%s, from %s, to %s, time %s)" % (a, b, c, self.pb(P1))
        if sub == 16:
            a = self.pb(P1)
            return "roomOps.cycleSpeed(%s, %s)" % (a, self.pb(P2))
        raise Unknown("0x%02X (roomOps sub-op 0x%02X)" % (self.data[self.pos - 1], self.op))

    def o_getDist(self):
        return self.getter("getDist", "o", "o")

    def o_findObject(self):
        return self.getter("findObject", "b", "b")

    def o_walkActorToObject(self):
        a = self.pb(P1)
        return "walkActorToObject(%s, obj %s)" % (a, self.obj(P2))

    def o_startObject(self):
        obj = self.obj(P1)
        script = self.pb(P2)
        return "startObject(obj %s, verb %s, %s)" % (obj, script, self.vararg())

    def o_getActorScale(self):
        return self.getter("getActorScale", "b")

    def o_stopSound(self):
        return self.call("stopSound", "b")

    def o_findInventory(self):
        return self.getter("findInventory", "b", "b")

    def o_drawBox(self):
        x, y = self.pw(P1), self.pw(P2)
        self.op = self.u8()
        x2, y2 = self.pw(P1), self.pw(P2)
        return "drawBox(%s, %s, %s, %s, color %s)" % (x, y, x2, y2, self.pb(P3))

    def o_cutscene(self):
        return "cutscene(%s)" % self.vararg()

    def o_endCutscene(self):
        return "endCutscene()"

    def o_chainScript(self):
        script = self.pb(P1)
        return "chainScript(%s, %s)" % (script, self.vararg())

    def o_soundKludge(self):
        return "soundKludge(%s)" % self.vararg()

    def o_actorFollowCamera(self):
        return self.call("actorFollowCamera", "b")

    def o_setObjectName(self):
        obj = self.obj(P1)
        return "setObjectName(obj %s, %s)" % (obj, self.string())

    def o_getActorMoving(self):
        return self.getter("getActorMoving", "b")

    def o_beginOverride(self):
        return "beginOverride()" if self.u8() else "endOverride()"

    def o_freezeScripts(self):
        return self.call("freezeScripts", "b")

    def o_stopScript(self):
        return self.call("stopScript", "b")

    def o_getActorFacing(self):
        return self.getter("getActorFacing", "b")

    def o_getClosestObjActor(self):
        return self.getter("getClosestObjActor", "w")

    def o_getStringWidth(self):
        return self.getter("getStringWidth", "b")

    def o_isScriptRunning(self):
        return self.getter("isScriptRunning", "b")

    def o_debug(self):
        return self.call("debug", "w")

    def o_getActorWidth(self):
        return self.getter("getActorWidth", "b")

    def o_stopObjectScript(self):
        return "stopObjectScript(obj %s)" % self.obj(P1)

    def o_lights(self):
        a = self.pb(P1)
        b = self.u8()
        return "lights(%s, %d, %d)" % (a, b, self.u8())

    def o_getActorCostume(self):
        return self.getter("getActorCostume", "b")

    def o_loadRoom(self):
        return self.call("loadRoom", "b")

    def o_getActorWalkBox(self):
        return self.getter("getActorWalkBox", "b")

    def o_isSoundRunning(self):
        return self.getter("isSoundRunning", "b")

    def o_breakHere(self):
        return "breakHere()"

    def o_systemOps(self):
        sub = self.u8()
        return "systemOps.%s()" % {1: "restart", 2: "pause", 3: "quit"}.get(sub, "unknown_%d" % sub)

    def o_dummy(self):
        return "dummy_0xA7()"

    def o_saveRestoreVerbs(self):
        self.op = self.u8()
        sub = self.op
        a, b, c = self.pb(P1), self.pb(P2), self.pb(P3)
        name = {1: "saveVerbs", 2: "restoreVerbs", 3: "deleteVerbs"}.get(sub & 0x1F, "sub_0x%02X" % sub)
        return "saveRestoreVerbs.%s(from %s, to %s, set %s)" % (name, a, b, c)

    def o_expression(self):
        res = self.var()
        stack, rpn = [], []
        infix = True
        while True:
            self.op = self.u8()
            if self.op == 0xFF:
                break
            sub = self.op & 0x1F
            if sub == 1:
                v = self.pw(P1)
                stack.append((v, True))
                rpn.append(v)
            elif sub in (2, 3, 4, 5):
                sym = {2: "+", 3: "-", 4: "*", 5: "/"}[sub]
                rpn.append(sym)
                if len(stack) < 2:
                    infix = False
                    continue
                (b, b_atom), (a, a_atom) = stack.pop(), stack.pop()
                stack.append(("%s %s %s" % (a if a_atom else "(" + a + ")", sym, b if b_atom else "(" + b + ")"), False))
            elif sub == 6:
                # a whole normal instruction; its result (written to VAR[0]) is pushed
                target, name = self.target, getattr(self, "name", None)
                text = self.one()
                self.target, self.name = target, "expression"
                text = re.sub(r"^VAR\[0 keypress\] = ", "", text)
                text = "<" + text + ">"
                stack.append((text, True))
                rpn.append(text)
            # any other sub-op is ignored by the interpreter and reads nothing
        self.name = "expression"
        if infix and len(stack) == 1:
            return "%s = %s" % (res, stack[0][0])
        return "%s = expression RPN(%s)" % (res, " ".join(rpn))

    def o_wait(self):
        self.op = self.u8()
        sub = self.op & 0x1F
        if sub == 1:
            return "waitForActor(%s)" % self.pb(P1)
        if sub in (2, 3, 4):
            return {2: "waitForMessage()", 3: "waitForCamera()", 4: "waitForSentence()"}[sub]
        raise Unknown("0x%02X (wait sub-op 0x%02X)" % (self.data[self.pos - 1], self.op))

    def o_pseudoRoom(self):
        room = self.u8()
        items = []
        while True:
            j = self.u8()
            if j == 0:
                break
            items.append(str(j & 0x7F) if j >= 0x80 else "(%d ignored)" % j)
        return "pseudoRoom(%d, [%s])" % (room, ", ".join(items))


# ---- resource files --------------------------------------------------------------

def blocks(data, start, end):
    """(tag, offset, size) of each block between two offsets of a big-header resource."""
    pos = start
    while pos + 8 <= end:
        tag = data[pos:pos + 4]
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if size < 8 or pos + size > end or not re.fullmatch(rb"[A-Za-z0-9 ]{4}", tag):
            return
        yield tag.decode(), pos, size
        pos += size


def object_name(body):
    """An OBNA string as plain text."""
    out, i = "", 0
    while i < len(body) and body[i]:
        c = body[i]
        if c == 0xFF and i + 1 < len(body):
            i += 2 if body[i + 1] in (1, 2, 3, 8) else 4
            continue
        if c != 0x40 and c >= 0x20:
            out += bytes([c]).decode("cp437")
        i += 1
    return out.strip()


def read_room(data):
    """The scripts of a ROOM block: [(info dict, code bytes, base offset)], in file order."""
    scripts = []
    for tag, pos, size in blocks(data, 8, len(data)):
        body = data[pos + 8:pos + size]
        if tag in ("ENCD", "EXCD"):
            scripts.append(({"kind": "entry" if tag == "ENCD" else "exit"}, body, 8))
        elif tag == "LSCR":
            scripts.append(({"kind": "local", "script": body[0]}, body[1:], 9))
        elif tag == "OBCD":
            obj_id, name, verbs = None, "", None
            for sub, spos, ssize in blocks(data, pos + 8, pos + size):
                sbody = data[spos + 8:spos + ssize]
                if sub == "CDHD":
                    obj_id = struct.unpack("<H", sbody[:2])[0]
                elif sub == "OBNA":
                    name = object_name(sbody)
                elif sub == "VERB":
                    verbs = data[spos:spos + ssize]
            if obj_id is None:
                continue
            entries, code_start = [], 8
            if verbs:
                p = 8
                while p + 2 < len(verbs) and verbs[p] != 0:
                    entries.append((verbs[p], struct.unpack("<H", verbs[p + 1:p + 3])[0]))
                    p += 3
                code_start = p + 1
            scripts.append(({"kind": "object", "object": obj_id, "object_name": name, "entries": entries},
                            verbs[code_start:] if verbs else b"", code_start))
    return scripts


class Dump:
    def __init__(self, path, decimal=False):
        self.path = path
        self.rooms, self.globals = {}, {}
        for name in sorted(os.listdir(path)):
            m = re.fullmatch(r"room(\d+)\.room", name)
            if m:
                self.rooms[int(m.group(1))] = name
            m = re.fullmatch(r"script(\d+)\.scrp", name)
            if m:
                self.globals[int(m.group(1))] = name
        self.fmt_off = (lambda n: "%04d" % n) if decimal else (lambda n: "%04X" % n)
        self._room_cache = {}
        self.obj_names = {}
        for room in self.rooms:
            for info, _, _ in self.room_scripts(room):
                if info["kind"] == "object" and info["object_name"]:
                    self.obj_names.setdefault(info["object"], info["object_name"])
        self.stats = {"scripts": 0, "ok": 0, "unknown": 0, "ranoff": 0, "empty": 0,
                      "jumps": 0, "bad_jumps": 0, "bad_entries": 0}
        self.failures = []

    def room_scripts(self, room):
        if room not in self._room_cache:
            data = open(os.path.join(self.path, self.rooms[room]), "rb").read()
            self._room_cache[room] = read_room(data)
        return self._room_cache[room]

    def decode(self, code, base):
        return Decoder(code, base, self.obj_names, self.fmt_off).run()

    def account(self, label, code, base, ins, status, detail, entries=()):
        """Update the run statistics for one script; returns warning lines for the listing."""
        warnings = []
        s = self.stats
        s["scripts"] += 1
        if not code:
            s["empty"] += 1
        s[status] += 1
        if status != "ok":
            self.failures.append("%s: %s %s at %s" % (label, "unknown opcode" if status == "unknown" else "ran off the end in",
                                                     detail, self.fmt_off(ins[-1].offset)))
        starts = {i.offset for i in ins}
        end = base + len(code)
        for i in ins:
            if i.target is not None:
                s["jumps"] += 1
                if i.target not in starts and not (status == "ok" and i.target == end):
                    s["bad_jumps"] += 1
                    warnings.append("; WARNING: jump at %s targets %s, which is not an instruction boundary"
                                    % (self.fmt_off(i.offset), self.fmt_off(i.target)))
        for verb, offset in entries:
            if offset not in starts and status == "ok":
                s["bad_entries"] += 1
                warnings.append("; WARNING: verb %d entry point %s is not an instruction boundary"
                                % (verb, self.fmt_off(offset)))
        return warnings

    def listing(self, label, code, base, entries=()):
        ins, status, detail = self.decode(code, base)
        warnings = self.account(label, code, base, ins, status, detail, entries)
        at = {}
        for verb, offset in entries:
            at.setdefault(offset, []).append(verb)
        lines = []
        for i in ins:
            for verb in at.get(i.offset, ()):
                lines.append(("  ; ---- verb %d %s" % (verb, VERB_NAMES.get(verb, ""))).rstrip())
            lines.append("[%s] %s" % (self.fmt_off(i.offset), i.text))
        if status == "ok":
            lines.append("; end %s" % self.fmt_off(base + len(code)))
        else:
            lines.append("; STOPPED: %s (%d of %d bytes decoded)" % (
                "unknown opcode" if status == "unknown" else "ran past the end", ins[-1].offset - base, len(code)))
        return lines + warnings

    def room_text(self, room):
        out = []
        for info, code, base in self.room_scripts(room):
            kind = info["kind"]
            if kind in ("entry", "exit"):
                title = "room %d %s script (%s)" % (room, kind, "ENCD" if kind == "entry" else "EXCD")
                entries = ()
            elif kind == "local":
                title = "room %d local script %d (LSCR)" % (room, info["script"])
                entries = ()
            else:
                entries = info["entries"]
                verbs = ", ".join("%d %s @%s" % (v, VERB_NAMES.get(v, ""), self.fmt_off(o)) for v, o in entries)
                title = 'room %d object %d "%s" verbs: %s' % (room, info["object"], info["object_name"], verbs or "(none)")
            out.append("==== " + title + " ====")
            if kind == "object" and not entries and not code:
                out.append("; no verb code")
                out.append("")
                continue
            out += self.listing(title, code, base, entries)
            out.append("")
        return "\n".join(out)

    def script_text(self, number):
        data = open(os.path.join(self.path, self.globals[number]), "rb").read()
        title = "global script %d (SCRP)" % number
        return "\n".join(["==== " + title + " ===="] + self.listing(title, data[8:], 8) + [""])

    def report(self, stream=sys.stderr):
        s = self.stats
        print("%d scripts: %d decoded cleanly to their end, %d hit an unknown opcode, %d ran off the end; "
              "%d jumps, %d not on an instruction boundary; %d verb entry points off a boundary"
              % (s["scripts"], s["ok"], s["unknown"], s["ranoff"], s["jumps"], s["bad_jumps"], s["bad_entries"]),
              file=stream)
        for failure in self.failures:
            print("  " + failure, file=stream)


def main(argv):
    args = [a for a in argv[1:] if a != "--decimal"]
    decimal = "--decimal" in argv
    out_dir = None
    if "--out" in args:
        i = args.index("--out")
        if i + 1 >= len(args):
            sys.exit(__doc__)
        out_dir = args[i + 1]
        del args[i:i + 2]
    if not args or (out_dir is None and len(args) != 3):
        sys.exit(__doc__)
    dump = Dump(args[0], decimal)
    if out_dir is not None:
        os.makedirs(out_dir, exist_ok=True)
        for room in sorted(dump.rooms):
            with open(os.path.join(out_dir, "room%03d.txt" % room), "w", encoding="utf-8") as f:
                f.write(dump.room_text(room))
        for number in sorted(dump.globals):
            with open(os.path.join(out_dir, "script%03d.txt" % number), "w", encoding="utf-8") as f:
                f.write(dump.script_text(number))
        print("wrote %d room files and %d global script files to %s" % (len(dump.rooms), len(dump.globals), out_dir),
              file=sys.stderr)
    else:
        what, number = args[1], int(args[2])
        if what == "room" and number in dump.rooms:
            sys.stdout.write(dump.room_text(number))
        elif what == "script" and number in dump.globals:
            sys.stdout.write(dump.script_text(number))
        else:
            sys.exit("no %s %d in %s" % (what, number, args[0]))
    dump.report()
    return 1 if dump.stats["unknown"] or dump.stats["ranoff"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except BrokenPipeError:
        pass
