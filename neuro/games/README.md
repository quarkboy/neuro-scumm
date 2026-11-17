# Games

One folder per game. The module in `engines/scumm/neuro/` knows nothing about any
particular game; what it needs to be told is here.

A game's folder holds:

- `descriptions.ini`: the description file. Its `[game]` section names the game and says
  what the module cannot work out about its screen (the verb slot of the sentence line,
  the inventory's buttons and variables, the verbs for walking, giving and opening), and
  `[synonyms]` lists other words for its verbs, and `[verbs]` names verbs the game
  draws as pictures. The rest is the rooms, goals, clues and
  alerts. The format is described in `engines/scumm/neuro/data.h` and `journal.h`.
- `game.json`: for the launchers and tools, one `"key": "value"` to a line:
  `name`, `steam_folder` (under `steamapps/common`), `game_file` (a file that must be in
  the game's folder), `target` (the name ScummVM gives the game's savegames), and
  `verbs` (names for the verb numbers in its scripts, for the script tools).
- Notes and status, as the game needs.

The console offers every folder here that has a `game.json`. `neuro/run.sh` and the
extraction scripts take the folder's name in `NEURO_GAME` (default `monkey1`).

## Which games

The module starts for SCUMM versions 1 to 6 with a verb bar, and stays off (with a
warning in the log) for anything else.

| Games | State |
|---|---|
| The Secret of Monkey Island (Special Edition, classic mode) | Played from start to finish |
| Other version 4 and 5 games: Monkey Island 1 on floppy or CD, Monkey Island 2, Fate of Atlantis | Same engine code as above; each needs its folder here. Not yet tried |
| Day of the Tentacle (version 6); Indiana Jones and the Last Crusade (version 3); Maniac Mansion and Zak McKracken (versions 1 to 3) | The differences known from reading the engine are handled: several player characters, verbs drawn as pictures (name them in `[verbs]`), bit variables, cutscenes and unusable things in the old versions. None of it has been run with those games |
| Sam & Max, Loom, Full Throttle, The Dig, The Curse of Monkey Island | Not supported: no verb bar, so they need another way to act |
| Humongous Entertainment games, the Commodore 64 and NES versions of Maniac Mansion | Not supported |

To start a new game: make the folder with a `game.json`, run the
[transcription tools](../tools/transcribe/README.md) to dump its rooms and lines, and
write `descriptions.ini` with the review page. The rule for all hand-written text: say
what is on screen and what characters said, never how to solve anything.
