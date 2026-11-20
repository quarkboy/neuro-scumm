# The operator's console

A web page for whoever runs the stream: choose the settings, start and stop the game, save and
load, send Neuro a message, take her controls away for a moment, and watch what she is told and
what she does. The game window it starts is made to be captured: 16:9, with the game on one side
and a column about what she is doing on the other.

```sh
python3 neuro/console/console.py                    # this machine only, port 7878
python3 neuro/console/console.py --bind 0.0.0.0:7878
python3 neuro/console/console.py --new-token        # the old address stops working
```

It prints the page's address, which carries a token; `--open` also opens it in the browser. It
needs Python 3.9 or newer and nothing else. In a release package, `console.bat` (Windows) or
`console.sh` (Linux) starts it.

## How it is put together

```
browser  --HTTP, polled, token-->  console.py
                                     | settings and token: console.local.json (not in git)
                                     | starts and stops the game
                                     |
the game  --WebSocket-->  console.py  --WebSocket-->  the Neuro API server
```

The console sits in the middle of the game's connection to Neuro. The game is told to connect
to the console; the console connects on to the server named in the settings and passes
everything through in both directions. That is where the live view comes from, and how the
console can talk to both sides without a restart:

- **To the game:** saving and loading, and the settings marked "changes at once" (her pace, the
  sound volumes, fast mode, and the column’s side, title, chat box and parts). **She may skip cutscenes** controls whether `skip_cutscene` is offered. Fast mode separately controls how quickly the game runs.
- **To Neuro:** your messages, sent as context from the game and marked with your name.
- **Taking her controls away** unregisters her actions with the server, as the SDK prescribes
  for actions that are not available; giving them back registers them again and repeats the
  question she had been asked, if any. The game itself keeps running meanwhile.

The game to play is chosen in the top bar, beside "Start the game": every folder under
`neuro/games` with a `game.json` is offered. The savegames listed and the game folder
follow the choice. The game folder and the column's title can be left empty, to use the
folder Steam installs that game to and "Neuro plays" with the game's name.

A save slot has a ScummVM save and a matching journal file in the selected save directory. Keep both when moving a checkpoint. The journal holds goals, learned and retired clues, and personal notes. A server such as Gary manages its own conversation history separately.

Settings that ScummVM only reads when it starts (the game, the game folder, the window's size, whether it
has a column, the way dialogue is sent) apply the next time the game is started. If the Neuro
server goes away, the console keeps trying and, once it is back, tells it again what the game has
registered. If a started game has not connected after twelve seconds, the console stops it and
starts it again, up to three times.

## The stream window

The game is 4:3. The window is the size chosen in the settings (1920 x 1080, say); the game is
as tall as the window, and the rest of the width is a column drawn by the game itself:

- a title (a `|` in it starts a new line),
- an empty box to lay the stream's chat over,
- where she is, what she is doing ("Thinking..." while she decides), her goals, what she is
  carrying and her last few actions.

Each of those parts can be switched off on the console page, also while the game runs.

In OBS, capture that one window. The column can be on either side or switched off, in which case
the window is 4:3. The window keeps the exact size asked for even where it does not fit on the
screen, so that the captured picture is never a few pixels short.

The picture is drawn by ScummVM's software renderer: the game is enlarged by a whole number
first, which keeps its pixels sharp, and then fitted to the window.

## Reaching it from another machine

By default only this machine can open it. With `--bind 0.0.0.0:7878` others on the network can,
with the token. It is plain HTTP: fine at home, never something to forward a port to.
