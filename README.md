# Neuro-sama plays The Secret of Monkey Island

This is a fork of [ScummVM](https://www.scummvm.org/) that lets [Neuro-sama](https://www.twitch.tv/vedal987), the AI streamer, play *The Secret of Monkey Island* through the [Neuro SDK](https://github.com/VedalAI/neuro-sdk). The game connects to her as a WebSocket client, tells her in words what a player would see and hear, and carries out the actions she sends back.

It is an unofficial fan project. It is not affiliated with the ScummVM team, Vedal, or the owners of Monkey Island, and it contains no game data: you need your own copy of the game.

**Status: early.** The opening of Part One works: the lookout, the SCUMM Bar and its kitchen, and the town on Mêlée Island. A stand-in language model can play through that stretch. The rest of the game runs, but has no written descriptions yet. Only Linux (including WSL) has been built and tested so far.

## How it works

Neuro cannot see the screen, so everything goes through text.

- **What she is told.** A description of each room and the things, people and exits in it; every line of dialogue, with who said it; and what changed as a result of her actions.
- **What she can do.** `interact` uses any of the game's verbs on anything in the room or her inventory, and also walks. `choose_dialogue` picks a line in a conversation. `answer` replies to a quick yes/no question. `read_journal` and `add_note` keep track of goals. `wait` and `skip_cutscene` do what they say.
- **Timing.** Moments that need a fast reaction (slipping past the cook, for example) are sent as a high-priority question, timed to her reaction speed. An action can also carry a second step that runs the instant the first one ends.
- **Hand-written text.** Room descriptions, names for characters the game leaves unnamed, goals and alerts live in a plain text file, [`neuro/data/monkey1.ini`](neuro/data/monkey1.ini). They say what is on screen and what characters have told her, never how to solve a puzzle.

The Neuro code is in [`engines/scumm/neuro/`](engines/scumm/neuro/). The development plan and its history are in [`neuro/PLAN.md`](neuro/PLAN.md).

## What you need

- *The Secret of Monkey Island: Special Edition* (the Steam release). The classic game is read straight out of its `Monkey1.pak`.
- A C++ compiler, `make`, SDL2 and libcurl, with their development packages.
- A Neuro API server to connect to. For testing without Neuro, [Gary](https://github.com/Govorunb/gary) works.

## Building

```sh
mkdir build && cd build
../configure --disable-all-engines --enable-engine=scumm,neuro
make -j$(nproc)
```

This builds only the SCUMM engine (Monkey Island 1 and 2 among others) with the Neuro module. Without a server URL configured, the result behaves like ordinary ScummVM.

## Running

[`neuro/run.sh`](neuro/run.sh) starts the game with the settings below. Point it at your game folder and build:

```sh
MONKEY1_DIR="/path/to/The Secret of Monkey Island Special Edition" \
BUILD_DIR=build \
NEURO_SDK_WS_URL=ws://127.0.0.1:8000 \
neuro/run.sh
```

Add `-x N` to start from savegame slot N.

| Setting | Default | Meaning |
|---|---|---|
| `NEURO_SDK_WS_URL` | `ws://127.0.0.1:8000` in `run.sh` | The Neuro API server. Unset, the Neuro module stays off. |
| `NEURO_DESCRIPTIONS` | `neuro/data/monkey1.ini` in `run.sh` | The description file. |
| `NEURO_DIALOGUE_MODE` | `speech` | `speech` reports dialogue with the SDK's `speech/started` and `speech/ended`. `context` sends it as ordinary context messages, for servers that do not know those commands (Gary 1.0.x). |
| `NEURO_IDLE_FORCE_SECONDS` | `45` | Prompt her after this long with nothing happening. `0` turns it off. |
| `NEURO_REACTION_MS` | `300` | How long she takes to react; timed questions go out that much early. |
| `NEURO_CONTEXT_TOKENS` | `32000` | After roughly this much traffic, text that is only said once (room descriptions, the introduction) is said again. |
| `NEURO_FAST_MODE` | off | `always`, or `scenes` to run fast only while she cannot act. For testing. |

Each setting can also be given as a ScummVM config key of the same name in lower case, for example `neuro_idle_force_seconds`. The one exception is the server URL, whose key is `neuro_ws_url`. An environment variable wins over the config key.

## Changes to ScummVM itself

Three fixes outside the Neuro module, all for the Special Edition's audio: its music now plays (it is stored as headerless WMA, which upstream does not decode yet), music resumes after loading a savegame, and a voice line that is interrupted by the next speaker keeps playing instead of being cut off.

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
