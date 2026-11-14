# Neuro-sama plays The Secret of Monkey Island: development plan

Status: phases 0 (mostly), 1, 2 and 3 done; phase 4 (vertical slice with an LLM) in progress. 2026-11-14.

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
Changes to existing upstream files are limited to a handful of one-line hooks behind `#ifdef ENABLE_NEURO`, so rebasing on upstream stays cheap.

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
- [x] Full history fetched, `upstream` remote, `neuro` branch. Plan and scripts live in `neuro/` inside the repo.
- [ ] Push to a private GitHub repo (needs the repo created, or `gh auth login`).
- [ ] Play to the SCUMM Bar manually in the Linux build to confirm video and audio under WSLg, and the remastered speech option.
- [x] Run script: `neuro/run.sh`.

**1. Transport** (done)
- WebSocket client, protocol layer, reconnect with re-`startup` and re-register.
- Built as a SCUMM sub-engine: `configure --disable-all-engines --enable-engine=scumm,neuro` (defines `ENABLE_NEURO`). URL from `NEURO_SDK_WS_URL` or the `neuro_ws_url` config key; without either the bridge stays off.
- Exit test passed: Gary lists the game with its `wait` action, an action round trip works, and the only diagnostic is an info note caused by answering Gary's own `actions/reregister_all`.

**2. Observation** (done, pending a human playthrough check)
- World snapshot and diffing; room-entry context; speech events; cutscene tracking.
- Debug console command to dump exactly what Neuro would be sent.
- Exit test: a human plays the opening while Gary's context log reads as a coherent transcript.
- Verified headless so far: intro and lookout dialogue arrive as speech lines with speaker names, and the first room (33, the dock street) is described once the player gains control. `skip_cutscene` was pulled forward from phase 3 to make this testable.
- Implementation notes: the world is polled, not hooked; a snapshot is sent only after the player has had control for 300 ms with no line being spoken. Rooms are still identified by number until the description data exists.

**3. Action** (done)
- The action set above, validation and error messages, state machine, forces.
- Exit test passed, headless, driven through Gary's MCP endpoint: lookout dialogue -> open door, walk into the SCUMM Bar -> talk to the important-looking pirates with dialogue choices -> into the kitchen -> pick up meat and pot -> use meat with pot o' stew.
- What was built, where it differs from the draft above:
  - `interact` covers walking too: "Walk to" is a pseudo-verb. The verb parameter becomes an enum once the game's verbs have been seen.
  - Verb sentences go through `doSentence`. Walking does not work that way (the game answers "That doesn't seem to work"), so "Walk to" is a simulated click: the pointer is held over the target for a few frames so the game's hover script sees it, then the button is pressed. The game then issues its own internal walk sentence (verb 11), which is also what takes Guybrush through doors.
  - Verb slot 100 is the sentence line, not a verb, and is filtered out.
  - Dialogue choices are verb slots that share no id with the normal verbs; they trigger an `actions/force` for `choose_dialogue`.
  - Idle prompt: after `NEURO_IDLE_FORCE_SECONDS` (default 45, 0 = off) with nothing happening, a force for `interact` with the room as ephemeral state.
  - Same-named things are numbered left to right ("door #1", "door #2"). Objects whose look changed are reported ("Changed in appearance: door"), since an opened door is otherwise invisible to her.
  - Gary 1.0.4 rejects `speech/started` / `speech/ended`. `NEURO_DIALOGUE_MODE=context` (the default in `neuro/run.sh`) sends dialogue as batched context messages instead; `speech` is the mode for the real server.
  - Unregistered `debug_screenshot` action for development.
- Found for phase 4:
  - The cook is an unnamed actor, so Neuro cannot tell when he leaves the kitchen. The description data needs to name unnamed actors, which then show up as "Arrived" / "Left". Getting into the kitchen is the first timing puzzle.
  - Off-screen speakers come through as "Narrator", unnamed ones as "Someone".

**4. Vertical slice with an LLM** (in progress)
- Done so far:
  - Description file `neuro/data/monkey1.ini`: lookout, dock, SCUMM Bar, kitchen, town square, high street, alley, mansion exterior, store, jail; names for the cook, seagull, storekeeper, LeChuck and others; open/closed labels for doors.
  - Gary runs GPT (gpt-6-luna, low reasoning) through `neuro/tools/openai-relay.py`. From a fresh start it opens the SCUMM Bar, talks to the pirate leaders about all three trials, then explores the town, the alley and the jail without help.
  - Two-step actions (`then_verb`, `then_target`) and walkable places from the description file; the seagull and fish puzzle works with them 3 times out of 3.
  - Journal: goals from `[goal.*]` sections, revealed and completed by lines heard, items held, rooms entered; `read_journal` and `add_note` actions; stored beside each savegame. It is not appended to forces: she reads it when she wants to.
  - `NEURO_FAST_MODE=always|scenes`, savegame checkpoints (`run.sh -x SLOT`), dev driver in `neuro/tools/dev/`.
  - Noise control: mid-scene dialogue is silent, background chatter and animation are filtered, unnamed speakers are numbered, stale actor names are ignored.
  - Alerts and yes/no questions (see the quick-time pattern under Later). Speakers that share one actor are numbered by text colour ("important-looking pirates #2").
  - Measured for the kitchen: the cook leaves the door open, and walking to it works from 3 s after he appears (tried up to 6 s); sooner and he shouts.
  - Gary fork in `../gary` (branch `speech-and-openai`, on upstream 1.0.6): accepts `speech/started` / `speech/ended`, and talks to api.openai.com through the Responses API, so `neuro/tools/openai-relay.py` is only needed with stock Gary. Run it with `../gary/src-tauri/target/release/gary --start-server`. The game launcher now defaults to speech mode; verified with GPT playing and no protocol errors.
- Still to do: describe remaining Mêlée rooms as she reaches them; decide what counts as passing the slice.
- Room descriptions for the slice. Gary with GPT plays from a fresh start.
- Tune action shape, descriptions, context volume and idle-force timing from transcripts.
- Exit test: the LLM gets through the slice unaided in most runs, and transcripts show no spam or stuck loops.

**5. Windows build**
- MinGW-w64 cross-compile from WSL (needs `sudo dnf install mingw64-gcc-c++ mingw64-SDL2 ...`), or MSYS2 on the Windows side. Package exe, DLLs, source pointer and a README.
- Can start any time after phase 1; listed late because nothing depends on it.

**Later, after the slice is judged**
- **Description file covers the whole game in draft** (2026-11-14): 81 rooms, goals for all four parts, quick-time alerts for the grog run and the final root-beer fight. Drafted by Claude from extracted room pictures (`neuro/tools/transcribe`) and a cross-checked walkthrough reference; every room is `status = draft` with a `review_note` until a person checks it on the review page. Many triggers are worded from walkthroughs and marked unverified.
- **Rooms not yet captured** (their scripts crash or hang when entered out of story order): 13, 22, 24, 26, 46, 47, 54, 55, 56, 66, 67, 68, 88, 91, 92, 93, 94, 99. Capture by playing to them.
- **Puzzles whose information is only visual, which need their own handling before she can solve them:**
  - The store safe: the storekeeper's push/pull sequence on the handle is only an animation, and the combination differs per game. Needs a narration of each turn (object 390's state changes in room 30).
  - The treasure map: its directions are shown as text on a close-up; check whether that text reaches her (it may not go through the talk path).
  - Other documents (captain's log, recipe, notes on Monkey Island): same question.
  - The forest maze and the catacombs: screens look alike, so each screen needs a way to tell them apart, or the head of the navigator's direction given as text.
  - The overhead maps: moving markers (the storekeeper she has to follow, road pirates) need reporting as they move.
  - The ghost ship: Guybrush is invisible there; check what she is told.
  - The catapult on Monkey Island: aiming is by push/pull with only a visual result.
- Timing assumption (from Samuel, 2026-11-14): Neuro herself can react in about 250 ms, far faster than the GPT stand-in (about 2.4 s per decision). So time-critical moments should be handled with a timely `actions/force` at `high` priority (second highest; `critical` cuts her off mid-sentence) rather than by slowing the game. GPT failing a timing puzzle is therefore weak evidence; judge those by whether the right force went out at the right moment.
- **Quick-time pattern (agreed 2026-11-14): use it for every timed moment.** An `[alert.*]` entry in the description file watches for the moment, then sends a `high` priority `actions/force` that says what just happened on screen and asks a yes/no question; answering yes through the `answer` action carries out a preset command immediately. `lead` and `NEURO_REACTION_MS` decide how early the question goes out. First use: the cook leaving the kitchen ("Slip through the open kitchen door while he is not looking?"), which GPT at 2.5 s per decision passed in 2 of 3 runs. Planned uses: each mug starting to melt on the grog run, and any other window too short for a free-form decision.
- Grog and the jail lock (when assembling a crew): grog eats through each mug, so it has to be poured from mug to mug while walking from the SCUMM Bar to the jail, across several rooms. A two-step action is not enough. First choice: the quick-time pattern above, one question each time a mug starts to melt ("Pour the grog into the next mug?"). Fallbacks: a longer list of steps in one action, or slowing the melt timer as an operator setting. Measure the real timer first.
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
  Settings that exist today as environment variables or config keys and should become console controls: `NEURO_SDK_WS_URL` (server), `NEURO_DIALOGUE_MODE` (speech or context), `NEURO_IDLE_FORCE_SECONDS` (idle prompt), `NEURO_REACTION_MS` (how early alerts fire), `NEURO_CONTEXT_TOKENS` (how much traffic before one-time text is repeated, default 32000), `NEURO_FAST_MODE` (always, scenes or off), `NEURO_DESCRIPTIONS` (description file), plus ScummVM's own remastered-audio and text-speed options.

## Open questions
None currently. Repo: standalone GitHub repo (not a GitHub fork), private during development; `upstream` remote tracks scummvm/scummvm; work happens on the `neuro` branch; commits keep the Claude co-author line.
