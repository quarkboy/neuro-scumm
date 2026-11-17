# The Secret of Monkey Island

Everything the integration needs to know about this one game.

| File | What it is |
|---|---|
| `descriptions.ini` | The description file: rooms, names, goals, clues, alerts, and the `[game]` section with what the module cannot work out about the game's screen. Format: `engines/scumm/neuro/data.h` and `journal.h`. Edit it with the [review page](../../tools/transcribe/README.md). |
| `game.json` | What the launchers and tools need: the game's name, the folder Steam installs it to, a file that must be in that folder, the name ScummVM gives its savegames, and the names of its verb numbers. |
| [`NOTES.md`](NOTES.md) | What was read from the game's scripts. |
| [`STATUS.md`](STATUS.md) | How far it has been played and reviewed. |

## The game

You need *The Secret of Monkey Island: Special Edition* from Steam. ScummVM plays the
classic game straight out of its `Monkey1.pak`; nothing is copied out of that folder or
written to it. The description file fits this release in English: it is keyed by the
game's room, object, actor and variable numbers and by exact lines of dialogue.

The console finds the game where Steam installs it, or takes the folder as a setting.
`neuro/run.sh` takes it in `GAME_DIR`:

| Where the game is | `GAME_DIR` |
|---|---|
| Steam on Windows, used from WSL | `/mnt/c/Program Files (x86)/Steam/steamapps/common/The Secret of Monkey Island Special Edition` (found by itself) |
| Steam on Linux (Proton) | `~/.steam/steam/steamapps/common/The Secret of Monkey Island Special Edition` (found by itself) |
| Anywhere else | the folder you copied or installed it to |
