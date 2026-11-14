# Development test driver

Rough scripts used to drive the game headless through Gary's MCP endpoint
(`http://127.0.0.1:8000/mcp`). They were written in a scratch directory and
keep their output next to themselves, so copy them somewhere writable first.

- `t.sh boot` starts the game, skips the credits and waits for the dock.
- `t.sh load N` starts from savegame slot N.
- `t.sh act NAME 'JSON'` sends an action as the user; `t.sh dbg NAME [DATA]`
  sends an unregistered `debug_*` action (screenshot, dump, save, load, room,
  click, speed).
- `t.sh shot [NAME]` saves a screenshot as PNG; `t.sh log` prints new bridge
  traffic; `tr.sh` prints a condensed transcript of the whole run.
- `goto.sh` has shell functions for walking somewhere and choosing dialogue.

Savegame slots made so far (in `../saves`, outside the repository):
1 dock at the start, 2 SCUMM Bar after meeting the pirate leaders,
3 SCUMM Bar kitchen.
