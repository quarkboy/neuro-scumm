#!/usr/bin/env python3
"""The operator's console for Neuro-sama plays The Secret of Monkey Island.

A web page for whoever runs the stream: choose the settings, start and stop
the game, save and load, send Neuro a message, and watch what she is told and
what she does. The game window it starts is made to be captured: 16:9, with
the game on one side and a column about what she is doing on the other.

    python3 neuro/console/console.py            # this machine only, port 7878
    python3 neuro/console/console.py --bind 0.0.0.0:7878

The console sits between the game and the Neuro API server. The game is
started as a child process and connects to the console; the console connects
on to the server in the settings and passes everything through.

Standard library only. Python 3.9 or newer.
"""

import argparse
import asyncio
import base64
import collections
import gzip
import hashlib
import json
import os
import secrets
import struct
import sys
import time
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
CONFIG = HERE / "console.local.json"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
# How long a started game gets to connect before it is started again, and how often that is tried
START_WAIT = 12
START_ATTEMPTS = 3

GAMES_DIR = REPO / "neuro" / "games"


def load_games():
    """Each folder under neuro/games with a game.json is a game that can be played."""
    games = {}
    for path in sorted(GAMES_DIR.glob("*/game.json")):
        try:
            games[path.parent.name] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            print(f"Skipping {path}: {error}", file=sys.stderr)
    return games


GAMES = load_games()


def steam_dir(game):
    """Where Steam installs the game by default on this machine."""
    folder = "steamapps/common/" + game.get("steam_folder", "")
    if os.name == "nt":
        return str(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam" / folder)
    if "microsoft" in os.uname().release.lower():
        return "/mnt/c/Program Files (x86)/Steam/" + folder
    return str(Path.home() / ".steam/steam" / folder)


DEFAULTS = {
    # Which game (a folder under neuro/games), and where things are
    "game": next(iter(GAMES), ""),
    # Empty: the folder Steam installs that game to
    "game_dir": "",
    "scummvm": "",
    # In a source checkout savegames stay outside the repository; in an unpacked release, inside its folder
    "save_dir": str((REPO.parent if (REPO / ".git").exists() else REPO) / "saves"),
    # Who plays
    "neuro_url": "ws://127.0.0.1:8000",
    "operator_name": "Vedal",
    "start_slot": None,
    # The window (used when the game is next started)
    "stream_size": "1920x1080",
    "fullscreen": False,
    "filtering": True,
    "panel": True,
    "panel_side": "right",
    # Empty: "Neuro plays" and the game's name
    "panel_title": "",
    "panel_chat": 30,
    "panel_hide": [],
    "music_volume": 192,
    "sfx_volume": 192,
    "speech_volume": 192,
    # The Neuro module (changed in the running game at once)
    "dialogue_mode": "speech",
    "idle_force_seconds": 45,
    "reaction_ms": 300,
    "context_tokens": 32000,
    "pointer": True,
    "skip_cutscenes": True,
    "fast_mode": "off",
}

# Settings the running game takes over the link, as "debug_set NAME VALUE"
LIVE = ("idle_force_seconds", "reaction_ms", "context_tokens", "pointer", "skip_cutscenes", "fast_mode",
        "music_volume", "sfx_volume", "speech_volume", "panel_hide", "panel_title", "panel_chat", "panel_side")


# --- WebSocket (RFC 6455, text frames) -------------------------------------------------

class WebSocket:
    def __init__(self, reader, writer, mask):
        self.reader, self.writer, self.mask = reader, writer, mask
        self.open = True

    async def recv(self):
        """The next text message, or None once the other side has gone."""
        parts = []
        try:
            while True:
                head = await self.reader.readexactly(2)
                opcode, length = head[0] & 0x0F, head[1] & 0x7F
                if length == 126:
                    length = struct.unpack(">H", await self.reader.readexactly(2))[0]
                elif length == 127:
                    length = struct.unpack(">Q", await self.reader.readexactly(8))[0]
                key = await self.reader.readexactly(4) if head[1] & 0x80 else None
                data = await self.reader.readexactly(length)
                if key:
                    data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
                if opcode == 0x8:
                    break
                if opcode == 0x9:
                    self._frame(0xA, data)
                    continue
                if opcode == 0xA:
                    continue
                parts.append(data)
                if head[0] & 0x80:
                    return b"".join(parts).decode("utf-8", "replace")
        except (asyncio.IncompleteReadError, ConnectionError, OSError):
            pass
        self.open = False
        return None

    def _frame(self, opcode, data):
        head = bytes([0x80 | opcode])
        n = len(data)
        bit = 0x80 if self.mask else 0
        if n < 126:
            head += bytes([bit | n])
        elif n < 65536:
            head += bytes([bit | 126]) + struct.pack(">H", n)
        else:
            head += bytes([bit | 127]) + struct.pack(">Q", n)
        if self.mask:
            key = os.urandom(4)
            data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
            head += key
        self.writer.write(head + data)

    def send(self, text):
        if not self.open:
            return
        try:
            self._frame(0x1, text.encode("utf-8"))
        except (ConnectionError, OSError):
            self.open = False

    def close(self):
        if self.open:
            try:
                self._frame(0x8, b"")
            except (ConnectionError, OSError):
                pass
        self.open = False
        try:
            self.writer.close()
        except Exception:
            pass


async def ws_connect(url):
    parts = urlsplit(url)
    if parts.scheme != "ws" or not parts.hostname:
        raise ValueError("the server address must start with ws://")
    port = parts.port or 80
    path = (parts.path or "/") + ("?" + parts.query if parts.query else "")
    reader, writer = await asyncio.wait_for(asyncio.open_connection(parts.hostname, port), 5)
    key = base64.b64encode(os.urandom(16)).decode()
    writer.write((f"GET {path} HTTP/1.1\r\nHost: {parts.hostname}:{port}\r\nUpgrade: websocket\r\n"
                  f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 5)
    if b" 101 " not in head.split(b"\r\n", 1)[0]:
        writer.close()
        raise ConnectionError("the server did not accept a WebSocket connection")
    return WebSocket(reader, writer, mask=True)


# --- The console ------------------------------------------------------------------------

def describe_action(name, data, lines):
    """One line for the page and the stream column: what an action of hers does."""
    try:
        args = json.loads(data) if isinstance(data, str) and data else (data or {})
    except ValueError:
        args = {}
    if not isinstance(args, dict):
        args = {}
    if name == "interact":
        def one(verb, target, second):
            if not verb:
                return ""
            if second:
                return f"{verb} {target} {'to' if verb.lower() == 'give' else 'with'} {second}"
            return f"{verb} {target}".strip()
        text = one(args.get("verb", ""), args.get("target", ""), args.get("second_target"))
        then = one(args.get("then_verb", ""), args.get("then_target", ""), None)
        return text + (f", then {then}" if then else "")
    if name == "choose_dialogue":
        if args.get("text"):
            return f"Says: “{args['text']}”"
        option = str(args.get("option", ""))
        if option in lines:
            return f"Says: “{lines[option]}”"
        return f"Picks line {option}"
    if name == "answer":
        return f"Answers {args.get('answer', args.get('reply', '?'))}"
    if name == "read_journal":
        return "Reads her journal"
    if name == "add_note":
        return f"Notes: {args.get('text', '')}"
    if name == "wait":
        return "Waits"
    if name == "skip_cutscene":
        return "Skips the scene"
    return name


class Console:
    def __init__(self, bind, open_browser=False):
        self.host, self.port = bind
        self.open_browser = open_browser
        self.token = ""
        self.settings = dict(DEFAULTS)
        self.load_config()

        self.proc = None
        self.output = collections.deque(maxlen=300)

        self.game = None            # the game's connection to us
        self.upstream = None        # our connection to the Neuro API server
        self.upstream_error = ""
        self.game_name = ""
        self.startup = None         # the game's startup message, replayed on every server connection
        self.actions = {}           # her registered actions, by name
        self.force = None           # the last actions/force not yet answered
        self.backlog = []           # context sent while no server was connected
        self.hold = False

        self.seq = 0
        self.events = collections.deque(maxlen=1500)
        self.status_id = 0
        self.view = self.empty_view()

    # Settings and token

    def load_config(self):
        try:
            saved = json.loads(CONFIG.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            saved = {}
        self.token = saved.get("token") or secrets.token_hex(16)
        for key, value in (saved.get("settings") or {}).items():
            if key in DEFAULTS:
                self.settings[key] = value
        # What was saved as a game's default follows the game from now on
        for game in GAMES.values():
            if self.settings["game_dir"] == steam_dir(game):
                self.settings["game_dir"] = ""
            if self.settings["panel_title"] == "Neuro plays|" + game.get("name", ""):
                self.settings["panel_title"] = ""
        self.save_config()

    def save_config(self):
        CONFIG.write_text(json.dumps({"token": self.token, "settings": self.settings}, indent=2), encoding="utf-8")

    def game(self):
        return GAMES.get(self.settings["game"]) or next(iter(GAMES.values()), {})

    def game_folder(self):
        return GAMES_DIR / (self.settings["game"] if self.settings["game"] in GAMES else next(iter(GAMES), ""))

    def game_dir(self):
        return self.settings["game_dir"] or steam_dir(self.game())

    def panel_title(self):
        return self.settings["panel_title"] or "Neuro plays|" + self.game().get("name", "")

    def find_scummvm(self):
        if self.settings["scummvm"]:
            return Path(self.settings["scummvm"])
        name = "scummvm.exe" if os.name == "nt" else "scummvm"
        for folder in (REPO / "build", REPO.parent / "build-linux", REPO, REPO / "build-scummvm" / "Debugx64",
                       REPO / "build-scummvm" / "Releasex64"):
            if (folder / name).is_file():
                return folder / name
        return None

    # What the page and the stream column show

    def empty_view(self):
        return {"location": "", "inventory": [], "goals": [], "now": "", "state": "idle",
                "speech": None, "question": "", "lines": {}}

    def event(self, way, command, text, raw=None):
        self.seq += 1
        self.events.append({"n": self.seq, "t": time.time(), "way": way, "command": command,
                            "text": text, "raw": raw})

    def saves(self):
        found = []
        folder = Path(self.settings["save_dir"])
        if not folder.is_dir():
            return found
        target = self.game().get("target", "")
        for path in folder.iterdir():
            suffix = path.suffix
            if len(suffix) != 4 or suffix[1] != "s" or not suffix[2:].isdigit():
                continue
            # Savegames are named after the game; one folder can hold several games'
            if target and path.stem != target:
                continue
            name = ""
            try:
                with open(path, "rb") as f:
                    head = f.read(2)
                opener = gzip.open if head == b"\x1f\x8b" else open
                with opener(path, "rb") as f:
                    header = f.read(44)
                if header[:4] == b"SCVM":
                    name = header[12:44].split(b"\0")[0].decode("latin-1")
            except (OSError, EOFError):
                pass
            found.append({"slot": int(suffix[2:]), "name": name, "time": path.stat().st_mtime})
        return sorted(found, key=lambda s: s["slot"])

    def status(self):
        return {
            "running": self.proc is not None and self.proc.returncode is None,
            "game_connected": self.game is not None,
            "neuro_connected": self.upstream is not None,
            "neuro_error": self.upstream_error,
            "hold": self.hold,
            "games": [{"id": key, "name": game.get("name", key)} for key, game in GAMES.items()],
            "game_dir": self.game_dir(),
            "panel_title": self.panel_title(),
        }

    # The game process

    async def start_game(self, attempt=1):
        if self.proc is not None and self.proc.returncode is None:
            return "The game is already running."
        exe = self.find_scummvm()
        if not exe or not exe.is_file():
            return "ScummVM was not found. Build it, or give its path in the settings."
        s = self.settings
        if not GAMES:
            return f"No game is set up: there is no game.json under {GAMES_DIR}"
        game_dir = Path(self.game_dir())
        if not game_dir.is_dir():
            return f"The game folder does not exist: {game_dir}"
        game_file = self.game().get("game_file")
        if game_file and not (game_dir / game_file).exists():
            return f"The game folder has no {game_file}: {game_dir}"
        save_dir = Path(s["save_dir"])
        save_dir.mkdir(parents=True, exist_ok=True)

        # The window: the game is 4:3 and as tall as the window, and whatever
        # is wider than that is the column. The game is first enlarged by a
        # whole number, which keeps its pixels sharp, and then fitted.
        size = str(s["stream_size"]).lower().replace(",", "x").split("x")
        if len(size) != 2 or not all(part.strip().isdigit() for part in size):
            return f"The window size should look like 1920x1080, not {s['stream_size']}."
        width, height = int(size[0]), int(size[1])
        if not s["panel"]:
            width = height * 4 // 3
        args = [str(exe), f"--savepath={save_dir}", "--auto-detect", "-p", str(game_dir),
                "--gfx-mode=surfacesdl", "--scaler=normal", f"--scale-factor={max(1, min(5, height // 240))}",
                "--aspect-ratio", "--fullscreen" if s["fullscreen"] else "--no-fullscreen",
                "--filtering" if s["filtering"] else "--no-filtering"]
        for key in ("music_volume", "sfx_volume", "speech_volume"):
            args.append(f"--{key.replace('_', '-')}={max(0, min(255, int(s[key])))}")
        if s["start_slot"] is not None and str(s["start_slot"]) != "":
            args += ["-x", str(int(s["start_slot"]))]

        env = dict(os.environ)
        # The game only takes this console's own commands (save, load, status, settings) when told to
        env.setdefault("NEURO_DEBUG_ACTIONS", "operator")
        env.update({
            "NEURO_SDK_WS_URL": f"ws://127.0.0.1:{self.port}/game?t={self.token}",
            "NEURO_DESCRIPTIONS": str(self.game_folder() / "descriptions.ini"),
            "NEURO_DIALOGUE_MODE": str(s["dialogue_mode"]),
            "NEURO_IDLE_FORCE_SECONDS": str(int(s["idle_force_seconds"])),
            "NEURO_REACTION_MS": str(int(s["reaction_ms"])),
            "NEURO_CONTEXT_TOKENS": str(int(s["context_tokens"])),
            "NEURO_POINTER": "on" if s["pointer"] else "off",
            "NEURO_SKIP_CUTSCENES": "on" if s["skip_cutscenes"] else "off",
            "NEURO_FAST_MODE": str(s["fast_mode"]),
            "NEURO_STREAM_SIZE": f"{width}x{height}",
            "NEURO_PANEL_SIDE": str(s["panel_side"]),
            "NEURO_PANEL_TITLE": self.panel_title(),
            "NEURO_PANEL_CHAT": str(int(s["panel_chat"])),
            "NEURO_PANEL_HIDE": ",".join(s["panel_hide"]),
        })
        self.output.clear()
        try:
            self.proc = await asyncio.create_subprocess_exec(
                *args, env=env, stdin=asyncio.subprocess.DEVNULL,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        except OSError as error:
            return f"The game could not be started: {error}"
        self.event("console", "start", "Game started" + (f" from slot {s['start_slot']}" if s["start_slot"] not in (None, "") else ""))
        asyncio.ensure_future(self.read_output(self.proc))
        asyncio.ensure_future(self.watch_start(self.proc, attempt))
        return ""

    async def watch_start(self, proc, attempt):
        """Now and then the game hangs while opening its window (seen under WSL).
        A game that has not connected after a while is started again."""
        await asyncio.sleep(START_WAIT)
        if self.proc is not proc or proc.returncode is not None or self.game is not None:
            return
        if attempt >= START_ATTEMPTS:
            self.event("console", "error", "The game is running but has not connected. Stop it and look at the game output.")
            return
        self.event("console", "start", "The game did not come up; starting it again")
        await self.stop_game()
        await self.start_game(attempt + 1)

    async def read_output(self, proc):
        while True:
            line = await proc.stdout.readline()
            if not line:
                break
            text = line.decode("utf-8", "replace").rstrip()
            if text and not text.startswith("ALSA lib"):
                self.output.append(text)
        code = await proc.wait()
        self.event("console", "stop", f"Game ended (exit code {code})")

    async def stop_game(self):
        proc = self.proc
        if proc is None or proc.returncode is not None:
            return
        proc.terminate()
        try:
            await asyncio.wait_for(proc.wait(), 4)
        except asyncio.TimeoutError:
            proc.kill()

    # Messages to the game

    def to_game(self, name, data="", ident="dbg"):
        """An action for the game. With the id "dbg" the game sends no result."""
        if self.game is None:
            return False
        action = {"id": ident, "name": name}
        if data != "":
            action["data"] = data
        self.game.send(json.dumps({"command": "action", "data": action}))
        return True

    def to_upstream(self, message):
        if self.upstream is not None:
            self.upstream.send(json.dumps(message))

    # The link: game -> console -> server

    async def serve_game(self, ws):
        if self.game is not None:
            self.game.close()
        self.game = ws
        self.actions, self.force, self.backlog, self.startup = {}, None, [], None
        self.view = self.empty_view()
        self.event("console", "link", "The game connected")
        poll = asyncio.ensure_future(self.poll_status(ws))
        while True:
            text = await ws.recv()
            if text is None:
                break
            try:
                self.from_game(text)
            except Exception as error:  # one bad message must not drop the link
                self.event("console", "error", f"Could not handle a message from the game: {error}")
        poll.cancel()
        if self.game is ws:
            self.game = None
            self.view = self.empty_view()
            self.event("console", "link", "The game disconnected")
            if self.upstream is not None:
                self.upstream.close()

    async def poll_status(self, ws):
        while ws.open:
            self.status_id += 1
            self.to_game("debug_status", ident=f"console-status-{self.status_id}")
            await asyncio.sleep(3)

    def from_game(self, text):
        message = json.loads(text)
        command = message.get("command", "")
        data = message.get("data") or {}
        view = self.view

        if command == "action/result" and str(data.get("id", "")).startswith("console-"):
            try:
                status = json.loads(data.get("message", "{}"))
            except ValueError:
                return
            view["location"] = status.get("location", "")
            view["inventory"] = status.get("inventory", [])
            # Her own notes follow the goals under their own heading
            goals = status.get("journal", "").split("\nYour notes", 1)[0]
            view["goals"] = [line[2:] for line in goals.split("\n") if line.startswith("- ")]
            return

        if command == "startup":
            self.game_name = message.get("game", "")
            self.startup = message
            self.actions, self.force = {}, None
        elif command == "actions/register":
            for action in data.get("actions", []):
                self.actions[action.get("name", "")] = action
        elif command == "actions/unregister":
            for name in data.get("action_names", []):
                self.actions.pop(name, None)
        elif command == "actions/force":
            self.force = message
            view["state"] = "thinking"
            view["question"] = data.get("query", "")
            view["lines"] = {}
            for line in str(data.get("state", "")).split("\n"):
                number, dot, rest = line.partition(". ")
                if dot and number.isdigit():
                    view["lines"][number] = rest
            self.event("to_neuro", command, data.get("query", ""), message)
        elif command == "context":
            self.event("to_neuro", command, data.get("message", "").strip(), message)
        elif command == "speech/started":
            view["speech"] = {"speaker": data.get("speaker", ""), "text": data.get("text", "")}
            self.event("to_neuro", "speech", f"{data.get('speaker', '')}: {data.get('text', '')}", message)
        elif command == "speech/ended":
            view["speech"] = None
        elif command == "action/result":
            ok = bool(data.get("success"))
            if ok:
                self.force = None
                if view["state"] == "thinking":
                    view["state"] = "acting"
                    view["question"], view["lines"] = "", {}
            self.event("to_neuro", "result", ("OK" if ok else "Refused") +
                       (": " + data["message"] if data.get("message") else ""), message)

        if self.upstream is None:
            # Nothing is listening yet. What she must not miss is kept for when someone is.
            if command == "context" and len(self.backlog) < 200:
                self.backlog.append(message)
            return
        if self.hold and command in ("actions/register", "actions/force"):
            return
        self.upstream.send(text)

    async def run_upstream(self):
        """Keeps a connection to the Neuro API server for as long as a game is connected."""
        while True:
            await asyncio.sleep(0.5)
            if self.game is None or self.startup is None:
                continue
            url = self.settings["neuro_url"]
            try:
                ws = await ws_connect(url)
            except (OSError, ValueError, asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError) as error:
                self.upstream_error = f"No connection to {url}: {error or type(error).__name__}"
                await asyncio.sleep(2)
                continue
            self.upstream, self.upstream_error = ws, ""
            self.event("console", "link", f"Connected to the Neuro server at {url}")
            # Tell the server everything the game has told us that still stands
            ws.send(json.dumps(self.startup))
            for message in self.backlog:
                ws.send(json.dumps(message))
            self.backlog = []
            if not self.hold:
                self.replay_actions()
            while True:
                text = await ws.recv()
                if text is None:
                    break
                try:
                    self.from_upstream(text)
                except Exception as error:
                    self.event("console", "error", f"Could not handle a message from the server: {error}")
            if self.upstream is ws:
                self.upstream = None
                self.event("console", "link", "The Neuro server connection closed")
            ws.close()

    def replay_actions(self):
        if self.actions:
            self.to_upstream({"command": "actions/register", "game": self.game_name,
                              "data": {"actions": list(self.actions.values())}})
        if self.force is not None:
            self.to_upstream(self.force)

    def from_upstream(self, text):
        message = json.loads(text)
        command = message.get("command", "")
        data = message.get("data") or {}
        if command == "actions/reregister_all":
            if not self.hold:
                self.replay_actions()
            return
        if command == "action":
            name = data.get("name", "")
            if name.strip().lower().replace("-", "_").replace(" ", "_").startswith("debug_"):
                # Those are this console's and the developer's; the server has no business sending one
                self.to_upstream({"command": "action/result", "game": self.game_name,
                                  "data": {"id": data.get("id", ""), "success": False, "message": "Unknown action."}})
                return
            said = describe_action(name, data.get("data"), self.view["lines"])
            self.event("from_neuro", "action", said, message)
            if self.hold:
                self.to_upstream({"command": "action/result", "game": self.game_name,
                                  "data": {"id": data.get("id", ""), "success": False,
                                           "message": "The game is paused for a moment."}})
                return
            self.view["now"] = said
            self.view["state"] = "acting"
        if self.game is not None:
            self.game.send(text)

    # What the operator does

    def set_hold(self, on):
        if on == self.hold:
            return
        self.hold = on
        self.to_game("debug_set", f"hold {'on' if on else 'off'}")
        who = self.settings["operator_name"]
        if on:
            if self.actions:
                self.to_upstream({"command": "actions/unregister", "game": self.game_name,
                                  "data": {"action_names": list(self.actions)}})
            self.event("console", "hold", f"{who} took her controls away")
        else:
            self.replay_actions()
            self.event("console", "hold", f"{who} gave her controls back")

    def say(self, text, silent):
        who = self.settings["operator_name"]
        message = f"{who} (who is running the stream) says: {text}"
        if self.upstream is None:
            return "There is no connection to the Neuro server."
        self.to_upstream({"command": "context", "game": self.game_name,
                          "data": {"message": message, "silent": bool(silent)}})
        self.event("console", "say", message)
        return ""

    def change_settings(self, new):
        old = dict(self.settings)
        for key, value in new.items():
            if key not in DEFAULTS:
                continue
            kind = type(DEFAULTS[key])
            try:
                if key == "start_slot":
                    value = None if value in (None, "") else int(value)
                elif kind is bool:
                    value = bool(value)
                elif kind is int:
                    value = int(value)
                elif kind is list:
                    value = [str(item) for item in value if str(item).isalpha()]
                else:
                    value = str(value)
            except (TypeError, ValueError):
                continue
            self.settings[key] = value
        self.save_config()
        for key in LIVE:
            if self.settings[key] != old[key]:
                value = self.settings[key]
                if isinstance(value, bool):
                    value = "on" if value else "off"
                elif isinstance(value, list):
                    value = ",".join(value) or "none"
                elif key == "panel_title":
                    value = self.panel_title()
                self.to_game("debug_set", f"{key} {value}")
        if self.settings["neuro_url"] != old["neuro_url"] and self.upstream is not None:
            self.upstream.close()

    # HTTP

    async def handle(self, reader, writer):
        try:
            head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
            lines = head.decode("latin-1").split("\r\n")
            method, target, _ = lines[0].split(" ", 2)
            headers = {}
            for line in lines[1:]:
                name, colon, value = line.partition(":")
                if colon:
                    headers[name.strip().lower()] = value.strip()
            url = urlsplit(target)
            query = parse_qs(url.query)
            token = (query.get("t") or [headers.get("x-token", "")])[0]
            if not secrets.compare_digest(token.encode(), self.token.encode()):
                self.respond(writer, 403, "text/plain", b"This page needs the address the console printed, with its token.")
                return

            if url.path == "/game" and headers.get("upgrade", "").lower() == "websocket":
                accept = base64.b64encode(hashlib.sha1((headers.get("sec-websocket-key", "") + WS_GUID).encode()).digest()).decode()
                writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                              f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
                await self.serve_game(WebSocket(reader, writer, mask=False))
                return

            body = b""
            if method == "POST":
                body = await reader.readexactly(min(int(headers.get("content-length", "0")), 1 << 20))
            await self.route(writer, method, url.path, query, body)
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError, ValueError, asyncio.LimitOverrunError):
            pass
        finally:
            try:
                writer.close()
            except Exception:
                pass

    def respond(self, writer, code, kind, body):
        reason = {200: "OK", 403: "Forbidden", 404: "Not Found", 400: "Bad Request"}.get(code, "OK")
        writer.write((f"HTTP/1.1 {code} {reason}\r\nContent-Type: {kind}\r\nContent-Length: {len(body)}\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n").encode() + body)

    def json(self, writer, value, code=200):
        self.respond(writer, code, "application/json", json.dumps(value).encode())

    async def route(self, writer, method, path, query, body):
        if method == "GET" and path == "/":
            self.respond(writer, 200, "text/html; charset=utf-8", (HERE / "page.html").read_bytes())
            return
        if method == "GET" and path == "/api/state":
            since = int((query.get("since") or ["0"])[0])
            self.json(writer, {"status": self.status(), "settings": self.settings, "saves": self.saves(),
                               "view": self.view, "seq": self.seq,
                               "events": [e for e in self.events if e["n"] > since][-300:],
                               "output": list(self.output)[-40:]})
            return
        if method != "POST":
            self.respond(writer, 404, "text/plain", b"Not found")
            return

        try:
            args = json.loads(body or b"{}")
        except ValueError:
            self.json(writer, {"error": "That was not JSON."}, 400)
            return
        error = ""
        if path == "/api/settings":
            self.change_settings(args)
        elif path == "/api/start":
            if "settings" in args:
                self.change_settings(args["settings"])
            error = await self.start_game()
        elif path == "/api/stop":
            await self.stop_game()
        elif path == "/api/hold":
            self.set_hold(bool(args.get("on")))
        elif path == "/api/say":
            text = str(args.get("text", "")).strip()
            error = self.say(text, args.get("silent")) if text else "There is nothing to say."
        elif path == "/api/save":
            slot = int(args.get("slot", 0))
            name = str(args.get("name", "")).strip() or "console"
            if not self.to_game("debug_save", f"{slot} {name[:60]}"):
                error = "The game is not connected."
            else:
                self.event("console", "save", f"Saved to slot {slot} ({name})")
        elif path == "/api/load":
            slot = int(args.get("slot", 0))
            if not self.to_game("debug_load", str(slot)):
                error = "The game is not connected."
            else:
                self.event("console", "load", f"Loaded slot {slot}")
        else:
            self.respond(writer, 404, "text/plain", b"Not found")
            return
        self.json(writer, {"error": error} if error else {"ok": True})

    async def run(self):
        server = await asyncio.start_server(self.handle, self.host, self.port, limit=1 << 20)
        shown = "127.0.0.1" if self.host in ("0.0.0.0", "") else self.host
        print(f"Console: http://{shown}:{self.port}/?t={self.token}")
        print("Ctrl+C stops the console and the game.")
        sys.stdout.flush()
        if self.open_browser:
            import webbrowser
            webbrowser.open(f"http://{shown}:{self.port}/?t={self.token}")
        asyncio.ensure_future(self.run_upstream())
        try:
            async with server:
                await server.serve_forever()
        finally:
            await self.stop_game()


def main():
    parser = argparse.ArgumentParser(description="The operator's console for the Monkey Island integration.")
    parser.add_argument("--bind", default="127.0.0.1:7878", help="address and port to serve on (default 127.0.0.1:7878)")
    parser.add_argument("--new-token", action="store_true", help="make a new token; the old address stops working")
    parser.add_argument("--open", action="store_true", help="open the page in the default browser")
    args = parser.parse_args()
    host, _, port = args.bind.rpartition(":")
    if args.new_token and CONFIG.exists():
        saved = json.loads(CONFIG.read_text(encoding="utf-8"))
        saved.pop("token", None)
        CONFIG.write_text(json.dumps(saved, indent=2), encoding="utf-8")
    console = Console((host or "127.0.0.1", int(port)), args.open)
    try:
        asyncio.run(console.run())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
