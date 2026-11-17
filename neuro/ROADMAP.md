# Roadmap

Where the project stands and what is left. For how it is built, see [PLAN.md](PLAN.md).

## Where it stands

A stand-in language model has played *The Secret of Monkey Island* from a new game to the end credits through the integration. That run needed stops for fixes along the way; an unaided run from start to finish has not been done yet.

Built so far:
- The actions, the room and dialogue reporting, the journal with goals and clues, and timed questions.
- Descriptions for all 81 rooms. Part One has been reviewed by a person; the rest is a first pass.
- Text that is only drawn on screen (the treasure map, the recipe, notes and signs) reaches her as text.
- Things whose look carries information are narrated as they change (the safe handle, the log on the summit of Monkey Island, who is winning a sword fight).
- The overhead maps, the forest, following the storekeeper, the catacombs and the ghost ship are playable from what she is told.
- Insult sword fighting: the lines to choose from come with a note of which insults she has not yet heard the comeback to, and which ones only the Sword Master uses.
- Developer tools: a dump of every room and every line of dialogue from the game's scripts, a review page for the description file, and scripts that drive the game headless.
- Packages for Windows and Linux, built and published for each release.

## Next

1. **Operator web console.** A first version exists in [`console/`](console/README.md): settings, starting and stopping the game, saving and loading, messages to her, taking her controls away, a live view, and a 16:9 game window with a column beside the game. Still to come: pausing the game itself, acting on her behalf (answering a question, picking a line), and restarting the game if it crashes.
2. **Review Parts Two, Three and Four.** Their descriptions and clues have not been checked against the game by a person.
3. **An unaided full run.**

## Known gaps

- Some wording was written from the scripts and not checked against the picture: where the stone flung from the summit lands, the totem gate, the left and right notes in the catacombs.
- Giving an item to someone who has no reaction scripted still reports "Done.".
- The question about slipping past the cook goes by timing, not by where he is.
- A few speakers in Parts Three and Four still come through as "Narrator" or "Stranger".
- The end credits are sent as text on the screen, line by line.
- She sometimes picks a line to say before the game has asked for one, in the silent gaps between spoken lines.

## Ideas not started

- **Graduated hints.** A `hint` action in the style of the old hint books: each puzzle is a question with answers that go from a gentle nudge to the full solution, one level per request, with the operator deciding whether hints are on and how far they may go.
- **Going somewhere she has been.** A `go_to` action that walks to any room already visited, using exits learned along the way. It would shorten long errands such as the grog run.
- **Monkey Island 2.**

## Notes read from the game's scripts

Kept because they took work to find and the descriptions depend on them.

### The forest
- It is 20 screens (pseudo-rooms 201 to 220 of room 58) joined by a fixed table; nothing is random. Each screen has up to three exits: the left edge, the right edge, and a gap in the trees at the back.
- Going past the first screen needs the map in the inventory, the storekeeper walking ahead, or having been deeper once before.
- One screen (207) only offers its right path when entered from screen 212.

### Following the storekeeper
- He only moves on while she is on the same screen, and waits up to 30 seconds per screen (60 on the dock and the map) for her to catch up; after that he is gone.
- At the end of his route he pushes a sign, which makes a bridge appear. The path beyond cannot be used until the sign has been pushed, by him or by her.
- Entering the Sword Master's clearing once puts it on the island map for direct travel.

### Rooms with their own mouse handler
Some rooms replace the game's mouse handler with a script of their own, and things only that script knows about never reach the normal sentence path. There the command is carried out as real clicks. Rooms that install a handler on entry: 2 (Monkey Island map), 18 (the crack), 21 (jungle with the monkey), 28 (SCUMM Bar), 42 (underwater), 51 (circus tent), 57 (troll bridge), 85 (Mêlée Island map).

### Sword fighting
- Pirates on the road use insults 1 to 16; the Sword Master uses 17 to 33, each answered by one of the same 16 comebacks.
- Guybrush learns an insult by hearing it, and a comeback by using an insult on a pirate who knows the answer. Losing exchanges is how the list grows.
