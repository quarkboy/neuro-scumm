# Transcription tools

For writing and checking the description file (`neuro/games/NAME/descriptions.ini`) of a
SCUMM game. The scripts take the game's folder name in `NEURO_GAME` (default `monkey1`)
and find the installed game from its `game.json`; see [`neuro/games/`](../../games/README.md).

## 1. Extract the rooms

```sh
neuro/tools/transcribe/extract-rooms.sh ../extract/monkey1 1
```

Starts the game headless from savegame slot 1, visits every room, and writes for
each one `roomNNN.png` (the whole room, scrolled and stitched into one picture) and
`roomNNN.json` (its objects with ids, names, boxes and states, and the actors present).
It uses the extraction mode built into the Neuro module (`engines/scumm/neuro/extract.cpp`),
so it works for any game the SCUMM engine runs.

The script drives the game with four environment variables, described at the top of
`engines/scumm/neuro/extract.cpp`: `NEURO_EXTRACT_DIR` (where to write; this turns the
mode on), `NEURO_EXTRACT_FROM` (the first room), `NEURO_EXTRACT_SKIP` (rooms to leave
out) and, for step 2, `NEURO_DUMP_SCRIPTS` (write the raw rooms and scripts instead).

Some rooms crash or hang when entered out of order, because their scripts expect
something the story sets up first. Those are skipped and listed in `skipped`; capture
them by playing to them.

What the pictures show is the room as its scripts draw it when entered cold: people
and things that only appear later in the story may be missing, and the camera may
never have been meant to show every part.

## 2. Extract the lines

```sh
neuro/tools/transcribe/extract-lines.sh ../extract/monkey1 1
```

Writes `lines.json`: every line of text the game's scripts contain, about 2,900 spoken
lines and 600 dialogue choices for Monkey Island 1. For each line it records the room,
the script it is in (for an object's script, which verb it answers: "poster / Look at"),
the actor it is printed for, and the text colour where the script sets one.

It reads the scripts directly (nothing is played), so it covers every room and every
branch, including lines no walkthrough reaches. It understands the scripts of SCUMM
version 5 games only (Monkey Island 1 on CD, Monkey Island 2, Fate of Atlantis); the room
extraction above has no such limit. It is not a decompiler: it finds the
instructions that carry text and knows nothing about when they run.

## 3. Draft and review

```sh
python3 neuro/tools/transcribe/review.py ../extract/monkey1 neuro/games/monkey1/descriptions.ini
```

Run the command from the repository root, then open http://127.0.0.1:8070/ in a browser. An optional third argument changes the port. The page shows the capture beside its editable description. Hover over an object row to highlight its outline in the picture. Edit
the room name, description, object notes and actor names, set the review status
(draft, needs work, checked), and save: the change goes straight into the description
file, keeping its comments and everything the page does not edit.

`status` and `review_note` are for reviewers. The bridge reads the description file when ScummVM starts, so restart the game to play with saved changes.

### Review a room in more than one state

The **Room** tab keeps one permanent description for the room. Under **Scenery
that changes**, choose an object (unnamed background objects are included),
select **Use captured state**, and enter the sentence for that state. Switch
captures to write the other sentences. The preview follows unsaved edits and
flags objects or states the selected capture cannot match. **Save** writes
`scenery.OBJECT.STATE = sentence` in the room section. Object notes still belong
on the individual targets. A scenery sentence also reaches the player on a
revisit, after a load, or when the state changes while the player is there.

Choose **Thing note override** on an object-state row to put its text beside
the target in **Things**. For example, give a chest the usual note `closed`
and set its state 1 override to `open`. This writes `object.ID.1 = open`.
Other states use the usual note. A hidden or unavailable target stays omitted.

For scenery driven by a global game variable, choose **+ variable value**,
enter its number, the matching value and the sentence. Save the rule before
making a new capture so the engine records the variable for preview and
**Use captured value**. Older captures can lack that reading. The room key is
`scenery.var.NUMBER.VALUE`. Room 16 uses variable 242 for the seesaw's angle.

Choose **+ running script** to show a sentence while a game script is active.
On the Mêlée Island map, `scenery.script.202` describes the roaming pirates.
Captures made after saving the rule record whether the script was running.

For a carried item, an inventory label applies across rooms and matches both the object ID and the current game name. For example, `[inventory]` with `name.164.cereal = box of cereal` labels the cereal while it has that name. Edit inventory labels in **Alerts and rules** or on a room row that offers the inventory label field.

In a scrolling room, the capture selector shows which part of the room is in view.
In room 39, compare **Crossing 1**, **Crossing 2**, and **Crossing 3** to follow
Guybrush across the cavern and back. Under **How the room behaves**, entering
`cave` in the walkable ground field offers a place between adjacent caves when
the game finds reachable floor there. New live captures record those places for the preview.

To collect a named capture during play, start the developer driver with
`NEURO_CAPTURE_DIR` set to the absolute path of the existing extraction folder.
Once the game shows the desired scene, run:

```sh
neuro/tools/dev/t.sh dbg debug_capture Before-the-dam
```

The command needs developer actions enabled and a PNG-capable build. For the
classic paletted game it writes the current viewport and object states together
as `room040--Before-the-dam.png` and `.json`. Use a distinct label (letters,
digits, hyphens or underscores) for each state; reusing it replaces the pair.
Reload the editor to see the new capture. Existing numbered room surveys still
work. Older surveys lack the player's position, so use a live capture to check
left/right directions. In a scrolling room the picture shows the captured
viewport; targets can extend into the rest of the room.

The page has four tabs:

- **Room:** the description, object notes and actor names, as above, and everything
  else a room's section can hold: other names for things, things and actors left out of
  what she is told, voices by text colour, places to walk to (pick the point in the
  picture), where Guybrush stands, passages, and the room's switches.
- **Lines:** every line found in the room's scripts, with a swatch of its text colour and
  the speaker the game will report. Lines whose speaker is a guess (text printed with no
  actor, which the game calls "Narrator" unless told otherwise) can be filtered for. Click
  a line to name who says it, for that line alone or for every line in that colour, and
  to mark it as giving a new goal, finishing a goal, or being a clue for a goal. Replies to
  commands such as "Look at poster" are lines like any other.
- **Goals and clues:** everything her journal can show: goals with their clues folded
  under them, and nudges. The search box narrows the list to goals and clues that
  mention a word. In a clue, **Hide once** retires it when any listed event happens.
  For example, hide a shovel price when she has the item named exactly `shovel`.
  You can also retire a clue when a game bit is set or a named replacement clue
  is found. Retirement survives saving and loading, even if it preceded discovery.
- **Alerts and rules:** alerts, watched variables, notes on dialogue choices, speakers
  set for single lines, state labels, fallback actor names and the game's switches, plus
  a list of the rooms that have settings of their own.

Every section has a box for its comment in the file. A comment saved from the page is
written above the section.

For the full INI format, see [data.h](../../../engines/scumm/neuro/data.h) and [journal.h](../../../engines/scumm/neuro/journal.h). Some engine settings, including `[game] pickup_verb`, are edited directly in the INI. Saving reviewer edits preserves those keys.

## Reading the game's logic: `disasm.py`

`disasm.py SCRIPT_DUMP_DIR room N` (or `script N`, or `--out DIR` for everything) prints the
scripts from the same dump as readable instructions, one per line with its offset. Use it to
find out how a puzzle works: which variable gates an exit, where a character walks, what an
object does for each verb. Add `--decimal` to compare offsets with `lines.json`.
Like `lines.py` it reads SCUMM version 5 scripts only. `--game neuro/games/NAME/game.json`
names the verb numbers.
