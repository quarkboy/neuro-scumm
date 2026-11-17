# The Secret of Monkey Island: notes read from the game's scripts

Kept because they took work to find and the descriptions depend on them. The numbers are
those of the Special Edition's classic game (SCUMM version 5, English).

### The screen
- The sentence line above the verbs is verb slot 100.
- The inventory is eight picture buttons, verb slots 200 to 207. Variables 133 to 140 hold
  the object each one shows, and slots 208 and 209 scroll a row up and down.
- Verb numbers: 2 Open, 3 Close, 4 Give, 5 Push, 6 Pull, 7 Use, 8 Look at, 9 Pick up,
  10 Talk to, 11 Walk to; 1 is what the inventory uses.

These are in `descriptions.ini` (`[game]`) and `game.json` (`verbs`), where the module and
the tools read them.


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
