# Monkey Island 2: LeChuck's Revenge

A second game, set up to show that the module needs nothing but a folder here. It starts
and can be played, but nearly all of its hand-written text is still to do: see
[`STATUS.md`](STATUS.md).

| File | What it is |
|---|---|
| `descriptions.ini` | The description file. So far: the `[game]` section, the key presses that get past the copy protection screen, and the screen that asks which game to play (the full one or "Monkey 2 Lite"). |
| `game.json` | The game's name, the folder Steam installs it to, a file that must be in that folder, the name ScummVM gives its savegames, and the names of its verb numbers. |
| [`NOTES.md`](NOTES.md) | What was read from the game's scripts. |

## The game

You need *Monkey Island 2 Special Edition: LeChuck's Revenge* from Steam. ScummVM plays the
classic game straight out of its `monkey2.pak`. Start it with the console's "Game" setting,
or with `NEURO_GAME=monkey2 neuro/run.sh`.
