# The operator's console

A web page for whoever runs the stream: choose the settings, start and stop the game, save and
load, send Neuro a message, take her controls away for a moment, and watch what she is told and
what she does. It also serves the column that OBS shows beside the game.

```sh
python3 neuro/console/console.py                    # this machine only, port 7878
python3 neuro/console/console.py --bind 0.0.0.0:7878
python3 neuro/console/console.py --new-token        # the old address stops working
```

It prints two addresses, each carrying a token. Open the first in a browser; give the second to
OBS. It needs Python 3.9 or newer and nothing else.

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

Settings that ScummVM only reads when it starts (the game folder, the window, the way dialogue
is sent) apply the next time the game is started. If the Neuro server goes away, the console
keeps trying and, once it is back, tells it again what the game has registered.

## The stream picture

The game is 4:3. For a 16:9 stream, capture the game window in OBS and put the column beside it
as a browser source:

| Stream | Game window | Column (browser source) |
|---|---|---|
| 1920 x 1080 | 1440 x 1080 | 480 x 1080 |
| 1280 x 720 | 960 x 720 | 320 x 720 |

The column scales with its width. It shows a title, an empty box to lay chat over, where she is,
what she is doing, her goals, what she is carrying and her last few actions. The title and the
height of the chat box are in the settings.

## Reaching it from another machine

By default only this machine can open it. With `--bind 0.0.0.0:7878` others on the network can,
with the token. It is plain HTTP: fine at home, never something to forward a port to.
