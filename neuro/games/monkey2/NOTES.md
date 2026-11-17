# Monkey Island 2: notes read from the game's scripts

The numbers are those of the Special Edition's classic game (SCUMM version 5, English;
its boot script calls itself "Monkey 2 (v1.0 11/21/91)").

### The screen
- The same layout as the first game: the sentence line is verb slot 100, the inventory
  is eight picture buttons (verb slots 200 to 207), and slots 208 and 209 scroll it.
- Variables 134 to 141 hold the object each inventory button shows (global script 9).
  The first game uses 133 to 140.
- Verb numbers are the first game's: 2 Open, 3 Close, 4 Give, 5 Push, 6 Pull, 7 Use,
  8 Look at, 9 Pick up, 10 Talk to, 11 Walk to.

### Starting
- The boot script (global script 1) runs the copy protection (script 130) when the boot
  parameter is 0, and that is the only parameter that then starts the game proper
  (script 92). Every other value is a developer's jump into the middle of the game.
- Script 128 waits for Enter ("PRESS ENTER TO BEGIN LAME-O COPY PROTECTION"), with
  script 142 running while it waits. Script 130 then takes four digits, with script 136
  blinking the cursor while it waits for one. ScummVM makes any answer pass, so the
  description file presses Enter and then types a digit for as long as one is asked for.
- After that, script 130 shows the choice between the full game and "Monkey 2 Lite" in
  room 108. Clicking object 1156 or 1158 picks the full game, 1157 or 1159 the short one.
- The opening (Guybrush on the rope, then the campfire) is not a cutscene in the game's
  own sense; it only says where to resume when Escape is pressed. `skip_cutscene` skips
  it as Escape does.
