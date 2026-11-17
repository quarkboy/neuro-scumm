# Roadmap

Where the project stands and what is left. For how it is built, see [PLAN.md](PLAN.md).

## Where it stands

The module plays one game, *The Secret of Monkey Island*; how far that has got is in
[games/monkey1/STATUS.md](games/monkey1/STATUS.md). Everything about that game is in its
folder under [`games/`](games/README.md), and the module itself is meant to work for other
SCUMM games given a folder of their own.

Built so far:
- The actions, the room and dialogue reporting, the journal with goals and clues, and timed questions.
- Developer tools: a dump of every room and every line of dialogue from a game's scripts, a review page for the description file, and scripts that drive the game headless.
- An operator console in [`console/`](console/README.md): settings, starting and stopping the game, saving and loading, messages to her, taking her controls away, a live view, and a 16:9 game window with a column beside the game.
- Packages for Windows and Linux, built and published for each release.

## Next

1. **Other SCUMM games.** Checks for the versions of the engine that differ (Day of the Tentacle, Indiana Jones and the Last Crusade, Maniac Mansion, Zak McKracken), then a second game, Monkey Island 2, as the proof.
2. **The console.** Still to come: pausing the game itself, acting on her behalf (answering a question, picking a line), and restarting the game if it crashes.
3. **Review of Monkey Island's Parts Two, Three and Four**, and an unaided full run.

## Known gaps

- She sometimes picks a line to say before the game has asked for one, in the silent gaps between spoken lines.
- The games without a verb bar (Full Throttle, The Curse of Monkey Island, Sam & Max, The Dig, Loom) need another way to act and are not planned yet.

## Ideas not started

- **Graduated hints.** A `hint` action in the style of the old hint books: each puzzle is a question with answers that go from a gentle nudge to the full solution, one level per request, with the operator deciding whether hints are on and how far they may go.
- **Going somewhere she has been.** A `go_to` action that walks to any room already visited, using exits learned along the way. It would shorten long errands such as the grog run.
