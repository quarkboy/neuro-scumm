# Neuro-sama plays The Secret of Monkey Island: design notes

How the integration is put together and why. For what is left to do, see [ROADMAP.md](ROADMAP.md).

## Decisions

| Topic | Decision |
|---|---|
| Architecture | A Neuro module inside a ScummVM fork (C++, in the SCUMM engine), speaking the Neuro API directly over WebSocket |
| Platforms | Linux (including WSL) is where it is developed; Windows and Linux packages are built for each release |
| Game data | The Steam Special Edition's `Monkey1.pak`, read in place; never copied into the repository |
| Testing | A stand-in language model behind a Neuro API test server, driven headless by scripts or watched in a window |
| Upstream | A standalone repository, not a GitHub fork. The `upstream` remote tracks scummvm/scummvm and work happens on the `neuro` branch |

## What the survey found

### Game files
- Current ScummVM reads the classic game straight out of the Special Edition's `.pak` and detects it as `scumm:monkey`, "The Secret of Monkey Island (SE/DOS/English)". No extraction step is needed.
- The Special Edition can play its remastered voice acting, music and effects over the classic game (`engines/scumm/soundse.cpp`).

### ScummVM
- Licence: GPL v3 or later, so the fork is too, and binaries must ship with source. The Neuro SDK is MIT, which is compatible.
- `configure --disable-all-engines --enable-engine=scumm,neuro` builds only the SCUMM engine with the Neuro module (it defines `ENABLE_NEURO`).
- `AI-GUIDELINES.md` sets rules for contributions to upstream ScummVM (disclosure, human-authored commits, no AI co-authorship). They bind upstream pull requests, not a separate fork, but they mean this work cannot go upstream as it is.

Engine hooks the module relies on (all in `engines/scumm/`):

| Need | Where |
|---|---|
| Per-frame tick | `ScummEngine::scummLoop` |
| Is the player in control? | `_userPut`, `_cursor.state`, `vm.cutSceneStackPointer`, `_sentenceNum` |
| Verbs and dialogue choices | `_verbs[]` (`VerbSlot`); in v5 dialogue choices are verb slots created by scripts |
| Execute "verb object [object]" | `doSentence(verb, objA, objB)`, which runs the game's own sentence script, including walking there |
| Room objects and names | `_objs[]`, `getObjOrActorName`, `findObject` |
| Inventory | `_inventory`, `getInventoryCount`, `findInventory` |
| Spoken lines | `actorTalk`, `stopTalk`, `_talkDelay`, `_haveMsg` |
| Cutscenes | `beginCutscene` / `endCutscene` / `abortCutscene` |
| JSON | `common/formats/json.h` |
| TCP socket | libcurl in connect-only mode |

Two gaps had to be filled:
- ScummVM has no WebSocket client. The module has a small one of its own (`websocket.cpp`: text frames, masking, ping/pong; plain `ws://` only).
- Rooms have no names or descriptions in the game data, and Neuro cannot see the screen. See "The description file" below.

### Neuro API
- The game is the WebSocket client. The server URL comes from `NEURO_SDK_WS_URL` or the `neuro_ws_url` config key; without either the module stays off and the build behaves like ordinary ScummVM.
- `speech/started` and `speech/ended` fit SCUMM dialogue exactly: one pair per line, with the speaker's name and the text, so she does not talk over the characters.
- Points from the SDK's guidance that shaped the design:
  - Keep the registered action set stable; do not re-register per room or per conversation.
  - For option sets that change (objects, inventory) take free-form strings, validate them here, and answer a wrong one with a failure that lists the valid options.
  - Send `action/result` right after validation, before the action plays out.
  - Use `actions/force` when the game is blocked on her (a dialogue choice), and after a while when she idles.
  - Give her what a human player would have, and be explicit.

## Design

### Layout
```
engines/scumm/neuro/     the module
  neuro.{h,cpp}          settings, the connection, messages in and out, per-frame update
  websocket.{h,cpp}      minimal WebSocket client
  actions.cpp            action definitions, validating and carrying out a command
  dialogue.cpp           lines to say, long lists of them, and prompting her
  pointer.cpp            simulated clicks and the travelling pointer
  world.cpp              snapshot of room, objects, people, inventory and verbs; what changed
  speech.cpp             what is said and shown as text, as speech events or context
  alerts.cpp             alerts and watched variables from the description file
  data.{h,cpp}           the description file
  journal.{h,cpp}        goals, clues and notes, stored beside each savegame
  args.{h,cpp}           reading the arguments of an action, however they are spelled
  panel.cpp              the column beside the game in a stream window
  debug.cpp              the operator console's commands and developer aids
  extract.cpp            developer tool: dumps rooms and scripts for writing descriptions
neuro/
  games/monkey1/         everything about one game: its description file, what the
                         launchers need to know about it, notes and status
  run.sh                 launcher
  console/               the operator's web console
  tools/                 extraction, review page and test scripts
```
Changes to existing ScummVM files are limited to short hooks behind `#ifdef ENABLE_NEURO`, plus a few fixes to the Special Edition's audio, so rebasing on upstream stays cheap.

### Actions
Registered once at startup:

| Action | What it does |
|---|---|
| `interact` | Uses one of the game's verbs on a thing in the room or the inventory, optionally with a second thing ("Use X with Y", "Give X to Y"). "Walk to" is one of the verbs. Can carry a second step that runs the instant the first ends |
| `choose_dialogue` | Picks a line to say, by number or by text. Asked for with a force whenever the game shows lines |
| `answer` | Replies yes or no to a timed question |
| `read_journal`, `add_note` | Her goals, the clues gathered under each, and her own notes |
| `wait` | Lets time pass |
| `skip_cutscene` | Skips the scene that is playing |

How commands are carried out:
- A verb sentence normally goes through `doSentence`, and the pointer is shown travelling over the verb and the things first, as a player's would.
- Walking, picking a line, and rooms that install their own mouse handler (the circus, the SCUMM Bar, the overhead maps and others) get real simulated clicks, because that is the only path the game's scripts take there.
- Things that share a name are numbered from left to right ("door 1", "door 2").

### What she is told
- At startup: who she is playing and how the actions map to the game.
- On entering a room: its name and description, the things, people and exits in it.
- Dialogue: one `speech/started` / `speech/ended` pair per line, with the speaker's name.
- After each action: only what changed (an item gained or lost, a thing that looks different, a new room).
- Text that is only drawn on the screen (documents, signs, close-ups) is sent as text.
- With a force: the current state, and the lines to choose from when there are any, as ephemeral context.

### The description file
The game has object names but no text describing a room. The game's `descriptions.ini` (`neuro/games/monkey1/`) supplies:
- `[room.*]`: a name and a few sentences per room, saying only what is visible on screen, plus names and notes for individual things where the picture carries meaning the name does not.
- Names for characters the game leaves unnamed, and speaker overrides for lines that would otherwise be credited to the wrong voice.
- `[goal.*]` and `[clue.*]`: journal entries, revealed by lines heard, items held and rooms entered. A clue records only what a character said or what she saw.
- `[alert.*]`: timed questions (see below).
- `[watch.*]` and `[states]`: things whose change of look carries information, with a label per state.
- `[game]` and `[synonyms]`: the game's name, who she plays, and what the module cannot work out about the game's screen (which verb slot is the sentence line, how the inventory's buttons are numbered, the verbs for walking, giving and opening).

The rule for all of it: say what is on screen and what characters said, never how to solve anything.

### Timing
Neuro reacts in a fraction of a second; a stand-in language model takes a few seconds per decision. Moments that need a quick reaction are therefore not handled by slowing the game. An `[alert.*]` entry watches for the moment and sends a high-priority `actions/force` that says what just happened and asks a yes/no question; answering yes carries out a preset command at once. `NEURO_REACTION_MS` decides how early the question goes out. First use: slipping into the kitchen while the cook is out.

### Pacing
- With remastered speech on, a line lasts as long as its audio.
- She can act whenever the player has control. A force goes out at once for a dialogue choice, and after `NEURO_IDLE_FORCE_SECONDS` when nothing is happening.
- The bridge owns one outstanding prompt and tracks the outcome of the action in progress. Reading the journal, adding a note, waiting, or sending an invalid answer leaves the prompt open. An accepted gameplay action settles it. Changing rooms or reaching the deadline withdraws it and releases any held timer.
- The bridge withdraws the prompted action and registers it again when the prompt ends. The console uses that signal to clear the displayed question and its replay cache. Reconnecting the console to the upstream server replays the current prompt. Loading a save or losing the game's connection discards pending input.
- A room change cancels clicks and follow-up movements tied to the old room. A named follow-up is resolved again when control returns in the new room.

## Risks
- **No vision.** Clues that are purely visual have to be in the descriptions without giving solutions away.
- **Context volume.** Long dialogue trees and sword fights are long; text that is said once is repeated after `NEURO_CONTEXT_TOKENS` of traffic.
- **A stand-in is not Neuro.** Behaviour on the real system can differ. The design follows the SDK's guidance, not the quirks of one test model.
