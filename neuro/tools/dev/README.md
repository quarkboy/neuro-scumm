# Development test drivers

Two ways to drive the game headless.

## Without a server: `mock-server.py`

`mock-server.py PORT STEPS` stands in for the Neuro API server. It prints every message
the game sends and plays a script of steps (`wait`, `until TEXT`, `act NAME [JSON]`);
the top of the file describes them. Point the game at it with
`NEURO_SDK_WS_URL=ws://127.0.0.1:PORT`. It needs nothing but Python, and several can run
side by side on different ports, each with its own `NEURO_SAVE_DIR`.

## Through Gary: `t.sh`

`t.sh` drives the game through Gary's MCP endpoint (`http://127.0.0.1:8000/mcp`). It
keeps its output (`game.out`, `gid`, `pos`, `pid`, `shots/`) beside itself; those names
are ignored by git.

- `t.sh boot` starts the game, skips the credits and waits for the first room.
- `t.sh load N` starts from savegame slot N.
- `t.sh act NAME 'JSON'` sends an action as the user; `t.sh raw NAME 'TEXT'` sends one
  whose data is passed on exactly as given.
- `t.sh dbg NAME [DATA]` sends an unregistered `debug_*` action (screenshot, dump, save,
  load, room, click, speed, var). The game only carries those out when started with
  `NEURO_DEBUG_ACTIONS=all`, which `t.sh start` sets.
- `t.sh shot [NAME]` saves a screenshot as PNG; `t.sh log` prints new bridge traffic;
  `tr.sh` prints a condensed transcript of the whole run.
- `t.sh stop` stops the game that `t.sh` started, and no other.
- Source `goto.sh` to use `goto NAME` and `choose N`. `choose` reads the latest numbered menu from the driver log. When sending `choose_dialogue` directly, supply both fields, for example `{"menu":47,"option":2}` for option 2 on menu 47.
- `mcp.sh TOOL 'JSON'` calls one of Gary's MCP tools.
