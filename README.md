# Neuro-sama plays The Secret of Monkey Island

This is a fork of [ScummVM](https://www.scummvm.org/) that lets [Neuro-sama](https://www.twitch.tv/vedal987), the AI streamer, play *The Secret of Monkey Island* through the [Neuro SDK](https://github.com/VedalAI/neuro-sdk). The game connects to her as a WebSocket client, tells her in words what a player would see and hear, and carries out the actions she sends back.

It is an unofficial fan project. It is not affiliated with the ScummVM team, Vedal, or the owners of Monkey Island, and it contains no game data: you need your own copy of the game.

**Status: playable from start to finish.** A stand-in language model has played the whole game through it, from a new game to the end credits, with stops for fixes along the way. Part One's descriptions have been reviewed by a person; the other three parts are a first pass. It is built for Linux and Windows; nearly all of the testing so far has been on Linux under WSL. Neuro herself has not played it. What is left is in [`neuro/ROADMAP.md`](neuro/ROADMAP.md).

**Other games.** The module is not tied to Monkey Island: everything about a game lives in a folder of its own, and it starts for any SCUMM game of versions 1 to 6 that has a verb bar. *Monkey Island 2* is set up as a second game and starts and plays, but its descriptions are not written. See [Other SCUMM games](#other-scumm-games) below.

## How it works

Neuro cannot see the screen, so everything goes through text.

- **What she is told.** A description of each room and the things, people and exits in it; every line of dialogue, with who said it; text that is only drawn on the screen, such as a map or a note; and what changed as a result of her actions.
- **What she can do.** `interact` uses any of the game's verbs on anything in the room or her inventory, and also walks. `choose_dialogue` picks a line in a conversation. `answer` replies to a quick yes/no question. `read_journal` and `add_note` keep track of goals and of the clues she has come across. `wait` lets time pass, and `skip_cutscene` skips a scene the way the Escape key does.
- **Timing.** Moments that need a fast reaction (slipping past the cook, for example) are sent as a high-priority question, timed to her reaction speed. An action can also carry a second step that runs the instant the first one ends.
- **Not talking over the game.** A line's end is reported together with the start of the next one when lines follow each other closely, and a line that the script follows straight away with another is marked as not to be replied to, so she is not handed the short gaps inside a conversation.
- **On screen.** The mouse pointer travels to each thing she uses, the way a player's would, so viewers can follow what she is doing.
- **Hand-written text.** Room descriptions, names for characters the game leaves unnamed, goals, clues and timed questions live in a plain text file, [`neuro/games/monkey1/descriptions.ini`](neuro/games/monkey1/descriptions.ini). They say what is on screen and what characters have told her, never how to solve a puzzle.

The Neuro code is in [`engines/scumm/neuro/`](engines/scumm/neuro/) and knows nothing about any one game; everything about Monkey Island is in [`neuro/games/monkey1/`](neuro/games/monkey1/README.md). The design is explained in [`neuro/PLAN.md`](neuro/PLAN.md).

## Quick start

1. **Get the game.** You need *The Secret of Monkey Island: Special Edition* from Steam, installed. Nothing is copied out of it or written to it.
2. **Get a server for the game to talk to.** That is Neuro herself, or a stand-in for testing: the SDK lists several, among them [Gary](https://github.com/Govorunb/gary), Tony and Jippity. Start it and note its address (often `ws://127.0.0.1:8000`). If your stand-in rejects the SDK's `speech/started` and `speech/ended` messages, set "Dialogue sent as" to "Plain context" on the console page.
3. **Download a package** for Windows or Linux from the [Releases](https://github.com/quarkboy/neuro-scumm/releases) page and unpack it anywhere. You also need Python 3.9 or newer, for the console.
4. **Start the console:** double-click `console.bat` on Windows, or run `./console.sh` on Linux. A page opens in your browser.
5. **Check the settings on the page:** the Neuro server address, and the game folder if yours is not where Steam installs it (left empty, the Steam folder is used).
6. **Press "Start the game".** The game to play is chosen in the box beside the button. The game opens in a 16:9 window made to be captured for a stream: the game on one side, and a column showing where she is and what she is doing on the other.

From the page you can also save and load, send her a message, take her controls away for a moment, and watch what she is told and what she does. More about it in [`neuro/console/README.md`](neuro/console/README.md).

The rest of this page is about building it yourself and running it without the console.

## What you need to build it

- A C++ compiler, `make`, SDL2 and libcurl, with their development packages.
- The game and a server, as above.

The steps below are for Linux. The Windows packages are built with Visual Studio and vcpkg by [`.github/workflows/ci.yml`](.github/workflows/ci.yml), which is the reference for doing the same by hand.

## Building

On Debian or Ubuntu, the packages are:

```sh
sudo apt-get install g++ make libsdl2-dev libsdl2-net-dev libcurl4-openssl-dev \
    libflac-dev libfreetype6-dev libmad0-dev libogg-dev libpng-dev libvorbis-dev zlib1g-dev
```

Then, from the top of the repository:

```sh
mkdir build && cd build
../configure --disable-all-engines --enable-engine=scumm,neuro
make -j$(nproc)
```

This builds only the SCUMM engine with the Neuro module. The module is off unless it is asked for with `--enable-engine=neuro`, and needs libcurl. Without a server address configured, the result behaves like ordinary ScummVM.

## Pointing it at the game

The console and the launcher script used without it, `neuro/run.sh`, look for the game where Steam installs it. If yours is elsewhere, give the console the folder in its settings, or `run.sh` the folder in `GAME_DIR`. Nothing is copied out of that folder or written to it. [`neuro/games/monkey1/README.md`](neuro/games/monkey1/README.md) has the details for this game.

## Running

Without the console, [`neuro/run.sh`](neuro/run.sh) starts the game with the settings below:

```sh
GAME_DIR="/path/to/The Secret of Monkey Island Special Edition" \
NEURO_SDK_WS_URL=ws://127.0.0.1:8000 \
neuro/run.sh
```

It looks for the build in the `build` folder made above; set `BUILD_DIR` if yours is elsewhere. Add `-x N` to start from savegame slot N. Savegames go to the folder in `NEURO_SAVE_DIR` (by default a `saves` folder beside the repository).

The console works from a source checkout too: `python3 neuro/console/console.py --open`. It finds the build in a `build` folder at the top of the repository, or takes its path in the settings.

| Setting | Default | Meaning |
|---|---|---|
| `NEURO_SDK_WS_URL` | `ws://127.0.0.1:8000` in `run.sh` | The Neuro API server. Unset, the Neuro module stays off. |
| `NEURO_GAME` | `monkey1` | For `run.sh` and the tools: which game, a folder under `neuro/games`. |
| `NEURO_DESCRIPTIONS` | the game's `descriptions.ini` in `run.sh` | The description file. |
| `NEURO_DIALOGUE_MODE` | `speech` | `speech` reports dialogue with the SDK's `speech/started` and `speech/ended`. `context` sends it as ordinary context messages, for servers that do not know those commands yet. |
| `NEURO_IDLE_FORCE_SECONDS` | `45` | Prompt her after this long with nothing happening. `0` turns it off. |
| `NEURO_REACTION_MS` | `300` | How long she takes to react; timed questions go out that much early. A language model standing in for her needs about `2500`. |
| `NEURO_CONTEXT_TOKENS` | `32000` | After roughly this much traffic, text that is only said once (room descriptions, the introduction) is said again. |
| `NEURO_POINTER` | on | `off` makes clicks land at once instead of the pointer travelling to them. |
| `NEURO_SKIP_CUTSCENES` | on | `off` takes the `skip_cutscene` action away from her, so every scene plays out. |
| `NEURO_FAST_MODE` | off | `always`, or `scenes` to run fast only while she cannot act. For testing. |
| `NEURO_STREAM_SIZE` | unset | A window size such as `1920x1080`. The game stays 4:3 and whatever is wider becomes a column showing where she is and what she is doing. Only ScummVM's SDL software renderer (`--gfx-mode=surfacesdl`) does this. |
| `NEURO_PANEL_SIDE`, `NEURO_PANEL_TITLE`, `NEURO_PANEL_CHAT` | `right`, "Neuro plays" and the game's name, `30` | The column's side, its title (a vertical bar in it starts a new line), and the height in percent of the empty box for chat. |
| `NEURO_DEBUG_ACTIONS` | off | `operator` lets the server side save, load, read the status and change settings with unregistered `debug_` actions (the console sets this for itself and passes none on from the server). `all` adds the developer's ones, which can change the game's state. |
| `NEURO_PANEL_HIDE` | unset | Parts of the column to leave out, separated by commas: `title`, `chat`, `location`, `now`, `goals`, `inventory`, `recent`. |

Each setting can also be given as a ScummVM config key of the same name in lower case, for example `neuro_idle_force_seconds`. The one exception is the server URL, whose key is `neuro_ws_url`. An environment variable wins over the config key.

In the game window, Space pauses the game until it is pressed again.

## Other SCUMM games

Everything the module needs to know about a game is in a folder under [`neuro/games/`](neuro/games/README.md): a description file (rooms, names, goals, clues, timed questions, and a `[game]` section for what cannot be worked out from the game itself, such as how its inventory buttons are numbered) and a small `game.json` for the launchers. The console lists every game it finds there, and `run.sh` takes one in `NEURO_GAME`.

| Games | State |
|---|---|
| The Secret of Monkey Island (Special Edition, classic mode) | Played from start to finish |
| Monkey Island 2 (Special Edition, classic mode) | Starts and plays from its folder; its descriptions, goals and clues are not written |
| Other games of SCUMM versions 4 and 5 (Monkey Island 1 on floppy or CD, Fate of Atlantis) | Same engine code; each needs a folder. Not tried |
| Day of the Tentacle, Indiana Jones and the Last Crusade, Maniac Mansion, Zak McKracken | The differences known from reading the engine are handled. Never run |
| Sam & Max, Loom, Full Throttle, The Dig, The Curse of Monkey Island, the Humongous Entertainment games | Not supported; the module stays off |

Writing a game's folder is most of the work. The tools for it (a dump of every room and every line from the game's scripts, a script disassembler, and a review page that edits the description file) are in [`neuro/tools/transcribe/`](neuro/tools/transcribe/README.md).

## Changes to ScummVM itself

Outside the Neuro module there are fixes for the Special Edition's audio, and the stream window:

- Its music plays (it is stored as headerless WMA), and resumes after loading a savegame.
- Its sound effects play, and are the right ones. They were looked up by position in a file that is not in that order, and a third of them decoded to silence.
- A voice line that another character interrupts keeps playing underneath instead of being cut off, and a line that the game prints repeatedly to make it flash is heard once.
- The SDL software renderer can open a window of an exact size, wider than the game, and show a column drawn by the engine beside it (`graphics/streampanel.h`).
- A screen update asked for during startup, before any screen exists, is skipped instead of failing now and then.

## License and credits

Like ScummVM, this fork is licensed under the GNU General Public License, version 3 or later; see [COPYING](COPYING). ScummVM is the work of the ScummVM team. The Neuro SDK is by Vedal AI.

The Neuro module was written with the help of an AI coding assistant (Claude Code), which is recorded in the commit history. It is not intended for submission to the ScummVM project.

---

*The original ScummVM README follows.*

# [ScummVM README](https://www.scummvm.org/) · [![Translation status](https://translations.scummvm.org/widgets/scummvm/-/scummvm/svg-badge.svg)](https://translations.scummvm.org/engage/scummvm/?utm_source=widget) [![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md#pull-requests)

## About ScummVM

ScummVM allows you to play classic graphic point-and-click adventure games, text adventure games, and RPGs, as long as you already have the game data files. ScummVM replaces the executable files shipped with the games, which means you can now play your favorite games on all your favorite devices.

So how did ScummVM get its name? Many of the famous LucasArts adventure games, such as Maniac Mansion and the Monkey Island series, were created using a utility called SCUMM (Script Creation Utility for Maniac Mansion). The ‘VM’ in ScummVM stands for Virtual Machine.

While ScummVM was originally designed to run LucasArts’ SCUMM games, over time support has been added for many other games: see the full list [on our wiki](https://wiki.scummvm.org/index.php?title=Category:Supported_Games). Noteworthy titles include Broken Sword, Myst and Blade Runner, although there are countless other hidden gems to explore.

For more information, compatibility lists, details on donating, the
latest release, progress reports and more, please visit the ScummVM [home
page](https://www.scummvm.org/).

## Quickstart

For the impatient among you, here is how to get ScummVM running in five simple steps.

1. Download ScummVM from [our website](https://www.scummvm.org/downloads/) and install it.

2. Create a directory on your hard drive and copy the game datafiles from the original media to this directory. Repeat this for every game you want to play.

3. Start ScummVM, choose 'Add game', select the directory containing the game datafiles (do not try to select the datafiles themselves!) and press Choose.

4. The Game Options dialog opens to allow configuration of various settings for the game. These can be reconfigured at any time, but for now everything should be OK at the default settings.

5. Select the game you want to play in the list, and press Start. To play a game next time, skip to step 5, unless you want to add more games.

>
> Hint:
>
> To add multiple games in one go, click the small arrow on the 'Add game' button and choose 'Mass Add'. You are again asked to select a directory, only this time ScummVM will search through all subdirectories for supported games.



## Reporting a bug

To report a bug, go to the ScummVM [Issue Tracker](https://bugs.scummvm.org/) and log in with your GitHub account.

Please make sure the bug is reproducible, and still occurs in the latest git/[Daily build](https://buildbot.scummvm.org/#/dailybuilds) version. Also check the [compatibility list](https://www.scummvm.org/compatibility/) for that game, to ensure the issue is not already known. Please do not report bugs for games that are not listed as completable on the [Supported Games](https://wiki.scummvm.org/index.php?title=Category:Supported_Games) wiki page, or on the compatibility list. We already know those games have bugs!

Please include the following information in the bug report:

- ScummVM version (test the latest git/[Daily build](https://buildbot.scummvm.org/#/dailybuilds))
- Bug details, including instructions for how to reproduce the bug. If possible, include log files, screenshots, and any other relevant information.
- Game language
- Game version (for example, talkie or floppy)
- Platform and Compiler (for example, Win32, Linux or FreeBSD)
- An attached saved game, if possible.
- If this bug only occurred recently, include the last version without the bug, and the first version with the bug. That way we can fix it quicker by looking at the changes made.

Finally, please report each issue separately; do not file multiple issues on the same ticket. It is difficult to track the status of each individual bug when they aren't on their own tickets.

## Documentation

### User documentation

For everything you need to know about how to use ScummVM, see our [user documentation](https://docs.scummvm.org/).

### The ScummVM Wiki

[The wiki](https://wiki.scummvm.org/) is the place to go for information about every game supported by ScummVM. If you're a developer, there's also some very handy information in the Developer section.

### Changelog

Our extensive change log is available [here](NEWS.md).

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=github&utm_medium=organic&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.

## Credits

A massive thank you to the entire team for making the ScummVM project possible. See the credits [here](AUTHORS)!

-----

> Good Luck and Happy Adventuring\!
> The ScummVM team.
> <https://www.scummvm.org/>
