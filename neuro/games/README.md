# Games

One folder per game. The module in `engines/scumm/neuro/` knows nothing about any
particular game; what it needs to be told is here.

A game's folder holds:

- `descriptions.ini`: the description file. Its `[game]` section names the game and says
  what the module cannot work out about its screen (the verb slot of the sentence line,
  the inventory's buttons and variables, the verbs for walking, giving and opening), and
  `[synonyms]` lists other words for its verbs. The rest is the rooms, goals, clues and
  alerts. The format is described in `engines/scumm/neuro/data.h` and `journal.h`.
- `game.json`: for the launchers and tools, one `"key": "value"` to a line:
  `name`, `steam_folder` (under `steamapps/common`), `game_file` (a file that must be in
  the game's folder), `target` (the name ScummVM gives the game's savegames), and
  `verbs` (names for the verb numbers in its scripts, for the script tools).
- Notes and status, as the game needs.

The console offers every folder here that has a `game.json`. `neuro/run.sh` and the
extraction scripts take the folder's name in `NEURO_GAME` (default `monkey1`).

To start a new game: make the folder with a `game.json`, run the
[transcription tools](../tools/transcribe/README.md) to dump its rooms and lines, and
write `descriptions.ini` with the review page. The rule for all hand-written text: say
what is on screen and what characters said, never how to solve anything.
