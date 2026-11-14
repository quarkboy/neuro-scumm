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

Some rooms crash or hang when entered out of order, because their scripts expect
something the story sets up first. Those are skipped and listed in `skipped`; capture
them by playing to them.

What the pictures show is the room as its scripts draw it when entered cold: people
and things that only appear later in the story may be missing, and the camera may
never have been meant to show every part.

## 2. Draft and review

```sh
python3 neuro/tools/transcribe/review.py ../extract/monkey1 neuro/data/monkey1.ini
```

Opens a page on http://127.0.0.1:8070/ with each room's picture, its objects outlined
(hover a row to find it in the picture), and the description file's text for it. Edit
the room name, description, object notes and actor names, set the review status
(draft, needs work, checked), and save: the change goes straight into the description
file, keeping its comments and everything the page does not edit.

`status` and `review_note` are for reviewers only; the game ignores them.
