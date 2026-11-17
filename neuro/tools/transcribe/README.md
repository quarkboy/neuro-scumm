# Transcription tools

For writing and checking the description file (`neuro/data/*.ini`) of a SCUMM game.

## 1. Extract the rooms

```sh
neuro/tools/transcribe/extract-rooms.sh ../extract/monkey1 1
```

Starts the game headless from savegame slot 1, visits every room, and writes for
each one `roomNNN.png` (the whole room, scrolled and stitched into one picture) and
`roomNNN.json` (its objects with ids, names, boxes and states, and the actors present).
It uses the extraction mode built into the Neuro module (`engines/scumm/neuro/extract.cpp`),
so it works for any game the SCUMM engine runs. Set `GAME_DIR` for games other than
Monkey Island 1.

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
branch, including lines no walkthrough reaches. It is not a decompiler: it finds the
instructions that carry text and knows nothing about when they run.

## 3. Draft and review

```sh
python3 neuro/tools/transcribe/review.py ../extract/monkey1 neuro/data/monkey1.ini
```

Opens a page on http://127.0.0.1:8070/ with each room's picture, its objects outlined
(hover a row to find it in the picture), and the description file's text for it. Edit
the room name, description, object notes and actor names, set the review status
(draft, needs work, checked), and save: the change goes straight into the description
file, keeping its comments and everything the page does not edit.

`status` and `review_note` are for reviewers only; the game ignores them.

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
  mention a word.
- **Alerts and rules:** alerts, watched variables, notes on dialogue choices, speakers
  set for single lines, state labels, fallback actor names and the game's switches, plus
  a list of the rooms that have settings of their own.

Every section has a box for its comment in the file. A comment saved from the page is
written above the section.

The page writes every kind of section and key the game reads (formats:
`engines/scumm/neuro/journal.h` and `data.h`). It does not reorder the file, rename a
section, or edit the headings between groups of sections.

## Reading the game's logic: `disasm.py`

`disasm.py SCRIPT_DUMP_DIR room N` (or `script N`, or `--out DIR` for everything) prints the
scripts from the same dump as readable instructions, one per line with its offset. Use it to
find out how a puzzle works: which variable gates an exit, where a character walks, what an
object does for each verb. Add `--decimal` to compare offsets with `lines.json`.

