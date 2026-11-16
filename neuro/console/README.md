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

It prints the page's address, which carries a token. It needs Python 3.9 or newer and nothing else.

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
  sound volumes).
- **To Neuro:** your messages, sent as context from the game and marked with your name.
- **Taking her controls away** unregisters her actions with the server, as the SDK prescribes
  for actions that are not available; giving them back registers them again and repeats the
  question she had been asked, if any. The game itself keeps running meanwhile.

Settings that ScummVM only reads when it starts (the game folder, the stream window, the way
dialogue is sent) apply the next time the game is started. If the Neuro server goes away, the console
keeps trying and, once it is back, tells it again what the game has registered.

## The stream window

The game is 4:3. The window is the size chosen in the settings (1920 x 1080, say); the game is
as tall as the window, and the rest of the width is a column drawn by the game itself:

- a title (a `|` in it starts a new line),
- an empty box to lay the stream's chat over,
- where she is, what she is doing ("Thinking..." while she decides), her goals, what she is
  carrying and her last few actions.

In OBS, capture that one window. The column can be on either side or switched off, in which case
the window is 4:3. The window keeps the exact size asked for even where it does not fit on the
screen, so that the captured picture is never a few pixels short.

The picture is drawn by ScummVM's software renderer: the game is enlarged by a whole number
first, which keeps its pixels sharp, and then fitted to the window.

## Reaching it from another machine

By default only this machine can open it. With `--bind 0.0.0.0:7878` others on the network can,
with the token. It is plain HTTP: fine at home, never something to forward a port to.
