# Neuro-sama plays The Secret of Monkey Island: development plan

Status: survey done, no integration code written yet. 2026-11-13.

## Decisions so far

| Topic | Decision |
|---|---|
| Architecture | Neuro module inside a ScummVM fork (C++, in the SCUMM engine), speaking the Neuro API directly over WebSocket |
| Platforms | Linux (WSL) as the dev loop, plus a native Windows build |
| Scope | Vertical slice first: start of Part One on Mêlée Island |
| Test server | Gary (GPT-backed), plus its MCP endpoint so the coding agent can drive sessions |
| Game data | Steam Special Edition `Monkey1.pak`, read in place; never copied into the repo |

## Survey findings

### Game files
- Steam install: `C:\Program Files (x86)\Steam\steamapps\common\The Secret of Monkey Island Special Edition\` (`Monkey1.pak`, 1.2 GB).
- Current ScummVM reads the classic game straight out of the `.pak`. Verified: a SCUMM-only build made here detects it as `scumm:monkey`, "The Secret of Monkey Island (SE/DOS/English)". No extraction step needed.
- The SE variant has an option to play the remastered voice acting over the classic game (`GAMEOPTION_USE_REMASTERED_AUDIO`, `engines/scumm/soundse.cpp`), and copy protection is already bypassed for it.

### ScummVM
- License: GPL v3 or later. The fork must be GPL v3+; binaries must ship with source. The Neuro SDK is MIT, which is compatible.
- No stripping required: `configure --disable-all-engines --enable-engine=scumm --disable-engine=he,scumm_7_8` builds only SCUMM v0-v6 (Monkey 1 and 2, among others). Builds cleanly here with GCC 16, SDL2 2.32, libcurl 8.21. Build dir: `build-linux/`.
- `AI-GUIDELINES.md` in the repo sets rules for contributions to upstream ScummVM (disclosure, human-authored commits, no AI co-authorship). These bind upstream PRs, not a separate fork, but they mean this work is unlikely to be upstreamable as is.

Engine hooks relevant to us (all in `scummvm/engines/scumm/`):

| Need | Where |
|---|---|
| Per-frame tick | `ScummEngine::scummLoop` (`scumm.cpp:3147`) |
| Is the player in control? | `_userPut`, `_cursor.state`, `vm.cutSceneStackPointer`, `_sentenceNum` |
| Verbs and dialogue choices | `_verbs[]` (`VerbSlot`, `verbs.h:47`); in v5 dialogue choices are verb slots created by scripts |
| Trigger a verb as if clicked | `runInputScript(kVerbClickArea, verbid, 1)` (`script.cpp:1443`) |
| Execute "verb object [object]" | `doSentence(verb, objA, objB)` (`script.cpp:1110`), which runs the game's own sentence script, including walking there |
| Room objects and names | `_objs[]`, `getObjOrActorName` (`object.cpp:1344`), `findObject` |
| Ready-made list of interactable objects and NPCs | `getHotspotPositions` (`object.cpp:559`), added upstream this week |
| Inventory | `_inventory`, `getInventoryCount`, `findInventory` (`object.cpp:79`) |
| Spoken lines | `actorTalk` (`actor.cpp:3466`), `stopTalk` (`actor.cpp:3564`), `_talkDelay`, `_haveMsg`; the existing `USE_TTS` hooks in `string.cpp` mark every place text reaches the screen |
| Cutscenes | `beginCutscene` / `endCutscene` / `abortCutscene` (`script.cpp:1597`) |
| JSON | `common/formats/json.h` |
| TCP socket | `Networking::Socket` (`backends/networking/basic/socket.h`, libcurl-backed) |
| Debug console | `debugger.cpp` (`room`, `objects`, `loadgame`, ...), plus an ImGui debugger |

Gaps:
- No WebSocket client anywhere in the tree. We write a small one (RFC 6455 text frames, masking, ping/pong) on top of `Networking::Socket`. Only plain `ws://` is needed.
- Rooms have no names or descriptions in the game data, and Neuro cannot see the screen. See "Scene descriptions" below.

### Neuro API
- Game is the WebSocket client; server URL must be configurable (convention: `NEURO_SDK_WS_URL`; we also add a ScummVM config key).
- Game sends `startup`, `context`, `actions/register`, `actions/unregister`, `actions/force`, `action/result`, and `speech/started` / `speech/ended`. Server sends `action` (and `startup` ack, `speech_finished`).
- `speech/started` / `speech/ended` fit SCUMM dialogue exactly: one pair per talk line, with speaker name and text, so she does not talk over characters.
- Best-practice points that shape the design:
  - Keep the registered action set stable; do not re-register per room.
  - For changing option sets (objects, inventory) use free-form string parameters, validate ourselves, and return a failure that lists valid options.
  - Send `action/result` right after validation, before the action plays out (20 s timeout).
  - Use `actions/force` when the game is blocked on her (dialogue choice), and timeout-then-force when she idles.
  - Give her what a human player would have; be explicit.

### Gary
- Tauri app, default server port 8000. Offers random, manual and LLM engines and protocol diagnostics.
- Has an MCP endpoint (`http://127.0.0.1:8000/mcp`) with `send_action`, `wait_for_events`, `get_context`, `get_diagnostics`. With that registered in Claude Code, the agent can test the integration end to end without a human in the loop.

## Design

### Layout
```
neuro-scumm/
  PLAN.md
  scummvm/                    fork, branch `neuro`
    engines/scumm/neuro/      all new code
      neuro.{h,cpp}           NeuroBridge: lifecycle, per-frame update, state machine
      websocket.{h,cpp}       minimal WS client
      protocol.{h,cpp}        Neuro API messages (JSON build/parse)
      worldstate.{h,cpp}      snapshot of room/objects/actors/inventory/verbs, diffing
      actions.{h,cpp}         action definitions, validation, execution
      speech.{h,cpp}          talk-line tracking -> speech/started|ended
    dists/engine-data/neuro/  room description data (text, per game)
  build-linux/  build-win/
  ref/                        neuro-sdk, gary (reference only)
```
Changes to existing upstream files are limited to a handful of one-line hooks behind `#ifdef USE_NEURO` (configure flag `--enable-neuro`), so rebasing on upstream stays cheap.

### Action set (first draft, to be tuned against Gary)
Registered once at startup:

| Action | Parameters | Notes |
|---|---|---|
| `interact` | `verb` (enum of the game's verbs), `target` (string), `second_target` (string, optional) | "Use X with Y", "Give X to Y". Names matched against visible objects, NPCs and inventory |
| `walk_to` | `target` (string) | Objects, NPCs, exits |
| `choose_dialogue` | `option` (integer) | Only valid while dialogue choices are showing; always sent as a force |
| `skip_cutscene` | none | Optional; may be disabled for stream pacing |
| `wait` | none | Lets time-based scenes proceed |

Whether `interact` should be one action or one per verb is an experiment for phase 3.

Execution uses `doSentence` for verb sentences and `runInputScript(kVerbClickArea, ...)` for dialogue choices. Fallback if a script depends on real clicks: synthesize mouse position and click.

Duplicate object names in a room ("door", "door") are disambiguated with a stable suffix.

### What Neuro is told
- Startup context: who she is playing, how verbs work, how the actions map to them.
- On room entry: room name and description, list of things she can interact with, exits, characters present.
- Dialogue: `speech/started` / `speech/ended` per line with speaker name; `reply: "never"` mid-cutscene, `"maybe"` at the end of an exchange.
- After each of her actions: only what changed (item gained or lost, object state change, room change).
- Force `state`: current room summary, inventory, and dialogue options when relevant, with `ephemeral_context: true`.

### Scene descriptions
The game has object names but no text describing a room. Proposal: a hand-written data file keyed by room number with a name and two or three sentences of description, containing only what is visible on screen. About ten rooms for the slice, about 90 for the full game. Rooms without an entry fall back to the object list alone.

### Pacing
- Text speed fixed to a known value; with remastered speech enabled, line duration comes from the audio.
- State machine: `Cutscene` (actions rejected with a reason) -> `Free` (actions accepted; force after N seconds idle) -> `Busy` (her sentence is running) -> `DialogueChoice` (force immediately).
- Only one force outstanding at a time; actions accepted at any time regardless of force state, per the API guide.

## Phases

**0. Foundation** (partly done)
- [x] Clone sources, SCUMM-only Linux build, detect Steam data.
- [ ] Turn `scummvm/` into a real fork: full history, your GitHub remote, `neuro` branch. Git repo for the outer project (plan, scripts, notes).
- [ ] Play to the SCUMM Bar manually in the Linux build to confirm video and audio under WSLg, and the remastered speech option.
- [ ] Run script: launches the game with the right path, config and server URL.

**1. Transport**
- WebSocket client, protocol layer, reconnect with re-`startup` and re-register.
- `--enable-neuro` configure flag, URL from env or config.
- Exit test: Gary shows the game connected with a dummy action and no diagnostics.

**2. Observation**
- World snapshot and diffing; room-entry context; speech events; cutscene tracking.
- Debug console command to dump exactly what Neuro would be sent.
- Exit test: a human plays the opening while Gary's context log reads as a coherent transcript.

**3. Action**
- The action set above, validation and error messages, state machine, forces.
- Exit test: via Gary's manual mode (driven over MCP), complete: lookout dialogue -> walk to SCUMM Bar -> talk to the important-looking pirates -> pick up items in the kitchen.

**4. Vertical slice with an LLM**
- Room descriptions for the slice. Gary with GPT plays from a fresh start.
- Tune action shape, descriptions, context volume and idle-force timing from transcripts.
- Exit test: the LLM gets through the slice unaided in most runs, and transcripts show no spam or stuck loops.

**5. Windows build**
- MinGW-w64 cross-compile from WSL (needs `sudo dnf install mingw64-gcc-c++ mingw64-SDL2 ...`), or MSYS2 on the Windows side. Package exe, DLLs, source pointer and a README.
- Can start any time after phase 1; listed late because nothing depends on it.

**Later, after the slice is judged**
- Remaining rooms' descriptions; Mêlée map screen; insult sword fighting (it is dialogue choices, so mostly a context problem); timed puzzles (grog mugs, underwater); forest and cave mazes; saves and resume; hint or unstuck mechanism; Monkey Island 2.

## Risks
- **Direct `doSentence` vs. script expectations.** Some game scripts may assume state set by the click-handling script. Mitigation: synthetic-click fallback; found early in phase 3.
- **No vision.** Puzzle clues that are purely visual need to be in the room descriptions without giving solutions away.
- **Context volume.** Long dialogue trees can flood her context. Needs measuring in phase 4.
- **Timed sequences.** LLM round-trips take seconds. The slice has few; later ones may need slowed timers.
- **Gary is not Neuro.** Behaviour on the production system can differ; follow the SDK best practices rather than tuning to GPT quirks.

## Decisions from review (2026-11-13)
- **Gary:** the WSL install, already configured with a GPT provider. Default server URL `ws://127.0.0.1:8000`.
- **Descriptions:** written by Claude, reviewed by Samuel. Covers rooms and, where the picture carries meaning the name does not, individual objects and inventory items (more important in the sequels).
- **Meta actions for timing puzzles:** where a puzzle needs reactions faster than an LLM round-trip (seagull and fish, watching the shopkeeper open the safe, grog mugs), add higher-level actions beyond the native verbs. Preferred shapes, least intrusive first:
  1. `wait_for` an in-game event ("until the seagull lands"), then act.
  2. Queue a short sequence of actions executed back to back.
  3. Slow or pause the relevant game timer while she decides.
  Each case is decided when we reach it; none are in the slice before the kitchen.
- **Cutscene skipping:** allowed only for cutscenes she has already seen (seen-set persisted across runs), with an operator override.
- **Operator web console:** a small local web page for whoever runs the stream, as in the Evil DOOM integration. First set of controls: connect/disconnect and server URL, remastered voice on/off, text speed, cutscene-skip policy, pause Neuro's control, idle-force timeout, save/load, and a live view of what Neuro is being sent. ScummVM ships a local web server (`backends/networking/sdl_net/localwebserver.*`, needs SDL_net) that we can probably build on; to be confirmed in phase 1. Built as phase 4b, after actions work.

## Open questions
None currently. Repo: standalone GitHub repo (not a GitHub fork), private during development; `upstream` remote tracks scummvm/scummvm; work happens on the `neuro` branch; commits keep the Claude co-author line.
