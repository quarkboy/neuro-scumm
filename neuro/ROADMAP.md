# Roadmap: the rest of The Secret of Monkey Island

Status 2026-11-14. Picks up where [PLAN.md](PLAN.md) leaves off: the opening of Part One works,
and the description file covers the whole game in unchecked draft. This file says what has to be
built, in what order, for Neuro to be able to finish the game, and how each step is shown to work.

It is based on the walkthrough reference in `../extract/research/monkey1-walkthrough.md`
(eight cross-checked sources; kept outside the repository with the extracted rooms). Section
numbers like "ref 3.7" point into it. Where that reference marks something as disputed or
unverified, the matching work item starts with checking it in the game.

## How to read it

- **Work items** have ids (E1, P1-3, ...), a size (S: an hour or two, M: a day, L: several days),
  and what they depend on.
- **"Done when"** is the test. Unless it says otherwise, the test is: the GPT stand-in in Gary,
  started from the named checkpoint with no nudges, gets past the step in most of five runs, and
  Gary reports no protocol errors.
- **Reaction time.** Timed steps are judged with `NEURO_REACTION_MS=2500` (GPT) and also checked by
  hand at 300 (Neuro). Where GPT is too slow but the right prompt goes out at the right moment,
  the step counts as done.
- **Rule for all text:** say what is on screen and what characters said; never how to solve
  anything. The one exception is the hint action (E9), which she has to ask for, and which the
  operator controls.

## The order, in one line

E1 E2 E3 first (they unblock testing everything else), E10's line dump with them (it makes every
goal and clue trigger exact), E9 alongside, then the game in story
order: P1 (rest of Part One), P2 (the voyage), P3 (Monkey Island), P4 (the finale), then a full
unaided run. Each P item also writes the hints for its puzzles (E9).

---

## E. Engine and tooling work that everything else needs

### E1. Scripted walkthrough and checkpoints (L)
A script that plays the whole game through Gary's MCP endpoint by sending the actions a
walkthrough would take, and saves a checkpoint at each milestone. It is three things at once: the
way to make checkpoints for testing later parts, a regression test for the whole integration, and
a check of the unverified goal and alert triggers (it logs which ones fire).
- Built on `neuro/tools/dev/` (the driver already sends actions, waits for events, saves and loads).
- Steps are data: one line per action, with an optional "wait for" condition.
- Checkpoints to produce (slot: state):
  4 sword bought and trained; 5 Sword Master beaten; 6 map bought; 7 treasure dug up;
  8 idol stolen, out of the water; 9 all three trials done (kidnapping seen);
  10 Otis freed; 11 crew complete; 12 ship bought, on board; 13 recipe cooked;
  14 ashore on Monkey Island; 15 monkey following; 16 inside the Monkey Head;
  17 on the ghost ship; 18 back on Mêlée for the finale.
- Where the walkthroughs disagree (treasure route, ref 3.12; cannon, ref 3.13), the script is
  where we find out which is right.
- **Done when:** the script reaches the end credits from a fresh start, and every checkpoint loads.

### E2. Text that is only shown, never spoken (M)
The treasure map, the recipe, the captain's log, LeChuck's note, the notes and memos on Monkey
Island, Stan's leaflets and the compass label are read from a close-up (room 84, "sheet of paper",
is probably the backdrop). Dialogue reaches her through the talk hook in `actorTalk`; it is not
known whether this text goes the same way.
- First: read each document in the game and check the bridge log for the text.
- If missing: hook the other text paths (`printString` slots 1 and 3, `drawString`) for close-ups
  and send the text as "Written on it: ...".
- **Done when:** reading the treasure map and the recipe puts their full text in her context.

### E3. Capture rooms during play (M)
The extractor only enters rooms cold. 18 rooms crash that way (13, 22, 24, 26, 46, 47, 54, 55, 56,
66, 67, 68, 88, 91-94, 99) and cold pictures miss the people who are normally there.
- Add a "capture this room now" debug action and a mode that captures each room the first time
  the E1 script enters it.
- **Done when:** every room in the game has a picture and an object list, and the 18 missing
  rooms have draft descriptions.

### E4. Watching objects change: `[watch.*]` (M)
Several puzzles show their information only as an object changing its look: the safe handle
(ref 3.7), the catapult rock (ref 3.16), the head of the navigator's nose in the catacombs
(ref 3.17). Today only changes to the object she just acted on are reported, and only as "Changed".
- New description-file section: room, object id, a label per state ("pointing up", "turned a
  quarter left", ...), and whether to report changes caused by others.
- First: record each object's states in the game (the extractor already lists state numbers).
- **Done when:** watching the storekeeper open the safe produces a readable sequence of turns.

### E5. Overhead maps and moving markers (M)
The Mêlée map (room 85) and the Monkey Island maps (rooms 2-6) are walked like rooms: places are
objects, so "Walk to fork" already works. What is missing is anything that moves: the storekeeper
she has to follow (ref 3.6), the road pirates who start sword fights (ref 3.4), the monkey.
- Report markers as people on the map with a rough place ("near the fork", "heading east") and
  report when they change place.
- Monkey Island's map scrolls when Guybrush reaches an edge: check what she is told.
- **Done when:** she can follow the storekeeper from the store to the Sword Master's using only
  what she is told.

### E6. Going somewhere she has been: `go_to` (M)
The grog run alone is five room changes with pours in between. A `go_to` action that walks to any
room she has already visited, using exits learned as she walks through them, saves her many round
trips everywhere, and makes timed errands possible at GPT speed.
- The route is learned, not written: every time a walk through an exit changes the room, record
  room, exit and destination.
- **Done when:** `go_to jail` from the SCUMM Bar kitchen walks there in one action.

### E7. Insult notebook (S)
Sword fighting is won by remembering which comeback answers which insult across many fights
(ref 3.4, 3.5, 4). The game only offers lines Guybrush has heard; matching them is up to her.
- Record every insult and the comeback that beat it into the journal automatically, as the
  fights happen. That is the game's own mechanic (Guybrush learns them), not a hint.
- Make the notebook readable with `read_journal`.
- **Done when:** after three road fights her journal lists the pairs she has seen.

### E8. Human review of the description file (M, not engineering)
81 rooms in draft. Review them on the review page, fix what is wrong, and mark them checked.
Rooms in the part being worked on come first.

### E9. Graduated hints: a `hint` action (M, plus writing per puzzle)
A hint book in the style of the old Universal Hint System: each puzzle is a question ("How do I
get into the kitchen?") with answers that go from a gentle nudge to the full solution. She asks
for one level at a time, so she gets only as much help as she wants, and chat can watch her decide
whether to look.
- **Wording is our own.** The UHS hints are copyrighted text and were not saved; the research kept
  facts only. Hints are drafted from the walkthrough reference and reviewed on the review page
  like the room descriptions (add a hints view to it).
- **Description file:** `[hint.ID]` sections with `question`, `when` (the triggers already used by
  goals: an active goal, a room, an item held, a line heard), and `1`, `2`, `3` ... from vague to
  explicit. The last level is always the full solution.
- **Action:** `hint` with an optional `topic`. Without one, she gets the questions that apply right
  now (by room, active goals and inventory). With one, she gets the next level she has not seen
  for that question. Levels already seen are kept per savegame, like the journal.
- **Operator controls** (console, and environment variables until then): hints on or off, a
  cooldown between hints, the highest level she may reach, and whether each hint is also
  announced to chat ("Neuro asked for a hint!").
- **Questions to write**, at least: the kitchen, the seagull, the troll, the sword fights, finding
  the Sword Master, the treasure route, the poodles, getting the idol, the underwater idol, the
  safe, Otis and the grog, Meathook, Stan, the recipe, the cannon, the catapult, the dam, the
  monkey, the cannibals, the catacombs, the ghost ship, the final fight.
- **Done when:** with hints on, GPT stuck at the safe asks for hints and gets through using them;
  with hints off, the action says hints are switched off.

### E10. Clues in the journal, and a dump of every line in the game (L)
Proposed by Samuel, 2026-11-14. Goals tell her what to do; clues are what she has learned that
helps with them, written down the moment she hears it. Example: the prisoner says he was jailed
for picking a flower, and names it.
- **Journal:** `read_journal` shows the unfinished goals as now. A new option shows the clues
  gathered so far, under the goal each belongs to. Clues of finished goals are hidden with them.
  When a clue is recorded she is told at the next pause, like a new goal
  ("Clue noted under Thievery trial: ...").
- **Description file:** `[clue.ID]` sections with `goal` (which goal it belongs to), `when` (the
  same triggers as goals, almost always `heard:` with the exact line) and `text` (the clue as a
  short note in plain words). Clues replace the `[nudge.*]` entries.
- **A dump of every line:** extract every line of dialogue and text from the game's scripts, per
  room, with its speaker. Ways to get it, to be tried in this order: decompile the scripts with
  ScummVM's `descumm` tool; scan the script resources for the text-printing instructions from
  inside the engine; record every line heard during the E1 scripted run (covers only the
  walkthrough's path, but is easy).
- **Review page:** a "Lines" view per room listing every line, where a reviewer marks a line as
  a new goal, a goal done, or a clue for a chosen goal. Saving writes the section with the exact
  text as its trigger. Claude drafts the marks from the dump and the walkthrough; a person checks.
- **Why it matters:** every `heard:` trigger in the file is currently guessed from walkthrough
  paraphrases (many are marked unverified). With the dump they become exact, and the whole game's
  goals and clues can be laid out in one pass instead of discovered during play.
- **Careful with:** clues must only record what she has actually heard, and only what a careful
  player would write down; a clue list that solves the puzzle makes the hint system pointless.
- **Done when:** the dump covers every room; Part One's goals and clues are marked from it; and
  in a GPT run, talking to the prisoner records the flower clue under the thievery trial.

---

## P1. The rest of Part One (Mêlée Island)

Checkpoint to start from: 2 (pirate leaders met). Order matches the usual play order; the three
trials can be done in any order.

### P1-1. Shopping and money (S)
Minutes and 2 pieces of eight from the Men of Low Moral Fiber; sword, shovel, breath mints at the
store; the map from the Citizen of Mêlée. All dialogue and Pick up, which already works. The store
will not let her leave with unpaid goods; check that the shopkeeper's line explains why.
- Check: prices (map 100 or 300, ref 4); breath mints only offered after trying to talk to Otis.

### P1-2. Training and road fights (M; needs E5, E7)
Get past the troll on the bridge, pay Captain Smirk 30 at the "House" on the far east of the map
to train (he has to be told three times that she has what it takes), then fight road pirates on
the map until one says she is ready (ref 3.4).
- The troll wants the fish (red herring). Dialogue plus Give: works.
- Road pirates approach on the map: E5 must report them; an alert should ask whether to fight.
- **Done when:** a checkpoint after three won fights, made by GPT.

### P1-3. Following the storekeeper to the Sword Master (M; needs E5)
Ask the storekeeper about the Sword Master; he leaves; follow him on the map and through the
forest (ref 3.6). Alert when he leaves the store ("Follow him?"). On each forest screen, report
which way he went.
- Check: whether the signpost must be pushed; whether he waits if she is slow.

### P1-4. The Sword Master (S; needs E7)
Dialogue only. Win with comebacks from the notebook. Proof: the T-shirt (goal trigger
`have: cotton T-shirt`: check the real item name).

### P1-5. Treasure (M; needs E2, and E4 or a forest fix)
Read the map (dance steps: "back, left, right ..."), walk the forest from the fork, dig at the X
with the shovel (ref 3.12).
- Forest screens are drawn by script from one room (58): check whether objects say which exit is
  "back", "left" and "right", and name them so the dance steps can be followed.
- Find out which route is right (three versions in the sources) with the E1 script.
- **Done when:** GPT, given the map text, digs up the treasure.

### P1-6. The idol (M)
Yellow petal plus meat for the poodles; the mansion sequences run by themselves (ref 3.9). The joke
there is the stream of commands on the sentence line; send them to her as they appear so she gets
the gag. Otis: mints, gopher repellent for the cake, the file. Underwater: pick up the idol
(10-minute limit, ref 3.10; tell her she is holding her breath).
- Check: the Fester and Elaine dialogue afterwards; the goal `have: fabulous idol`.

### P1-7. After the kidnapping: crew and ship (L; needs E4, E6)
- Kidnapping news from the lookout (goal triggers `heard: kidnapped`, check).
- **Otis (grog run, ref 3.3):** five mugs, grog from the barrel, pour before each mug dies, melt the
  lock. Alert `grog-mug` exists; check the item names ("melting mug", "mug near death") and that
  `Use mug near death with mug` works. With E6, `go_to jail` makes it one trip.
- **Meathook (ref 3.14):** across to Hook Isle on the rubber chicken's pulley; the parrot replaces
  the verb panel with joke verbs. Check what the verb detection makes of that (it may offer them
  as dialogue lines, which would be fine).
- **Carla:** dialogue.
- **Note of credit (ref 3.7):** needs E4 for the safe handle. Combination differs every game;
  she watches twice, then pushes and pulls.
- **Stan (ref 4):** a long haggle; dialogue only.
- **Done when:** a GPT run from checkpoint 9 ends on board.

## P2. The voyage (Part Two)

Checkpoint 12.

### P2-1. Finding out what to do (S; needs E2)
Nobody gives a goal: the captain's log and the recipe do. Goal `recipe` fires on picking up the
piece of paper: check, and that its text reaches her (E2).

### P2-2. Cooking (M)
Eight ingredients, each a pun on the recipe (ref 3.13): cinnamon sticks, breath mints, Jolly Roger,
ink, wine, rubber chicken, gunpowder, cereal. "Use X with cooking pot", one by one. Items she must
keep are refused by Guybrush, which is fine.
- The small key is inside the cereal prize; the cabinet holds the recipe. Works with existing verbs.
- **Done when:** GPT cooks the recipe from checkpoint 12 with only the recipe text.

### P2-3. Firing herself ashore (M)
Rope as fuse, gunpowder in the nozzle, a burning item from the stove, the pot as helmet
(ref 3.13). Whether lighting the fuse starts a timer is disputed: find out with the E1 script; if
it does, add an alert ("The fuse is burning. Climb into the cannon?").

## P3. Monkey Island (Part Three)

Checkpoint 14.

### P3-1. Getting around (M; needs E5)
The scrolling island map (rooms 2-6), the beaches, the rowboat once she has the oars (it puts her
on the same map, at sea).

### P3-2. Bananas and the monkey (M; needs E4)
Catapult on the cliff ledge: push and pull the see-saw rock, then fire (ref 3.16). The default aim
sinks her own ship (harmless, changes the ending). E4 reports the rock's position; Guybrush's
"I think I hit the banana tree!" confirms a hit. Then five bananas for the monkey, who follows her
and holds the totem's nose open.

### P3-3. The fort, the dam, the crack (M)
Rope, spyglass lens, gunpowder from the fort's cannon; blow the dam with the lens and the sun or
the flint and the cannon ball (ref 3.15); ropes on the stump and branch at the crack for the oars.
All existing verbs. Check that "the sun" is an object she can use the lens on.

### P3-4. Herman and the cannibals (M)
The cannibals catch her on every visit until she gives them an offering (ref 3.20); escape via
the skull and the loose board. Wimpy idol, banana picker, Herman's key, the leaflet for the head of
the navigator, the necklace. Dialogue and items.

### P3-5. The catacombs (M; needs E4)
Randomised screens that look alike. The head of the navigator points the way: check whether
"Look at head" gives the direction as text (ref 3.17). If not, E4 on the nose.
- **Done when:** GPT reaches the ghost ship from checkpoint 16.

### P3-6. The ghost ship (M)
Guybrush is invisible with the necklace; footprints show where he is. Squeaky door (grease),
sleeping ghost (tickle with the feather, maybe timing-sensitive), rat (grog in its dish), key via
the compass's magnet, the root in the crate (ref 3.18). Check what she is told when nothing shows
where Guybrush is.

## P4. The finale (Last Part)

Checkpoint 18.

### P4-1. Ghosts on Mêlée and the wedding (S)
Seltzer on the ghosts through dialogue (ref 3.19). Goal `wedding` (trigger `heard: wedding`: check).

### P4-2. Root beer (S)
Alert `root-beer` exists (critical priority; picks it up and throws it). Check the room (59), the
item name, and that "LeChuck" is the actor's name there. A miss is not fatal: she lands back at
Stan's.

---

## Then

- **Full run:** GPT from a fresh start to the credits, with the operator allowed to type
  suggestions as a chat stand-in, then unaided.
- **The ending** depends on the catapult (ref 3.16); both are fine.
- Things outside this roadmap, in [PLAN.md](PLAN.md): the Windows build, the operator console,
  Monkey Island 2.

## Risks

- **Visual-only puzzles beyond E4.** If a puzzle turns out to need more than state labels (the
  catapult's arc, the catacombs if the head's direction is only an animation), the fallback is the
  hint action (E9): she can always ask her way through.
- **Context load in long dialogues.** Stan's haggle and the sword fights are long. Watch token use
  per decision in Gary; the 32,000-token repeat budget may need lowering.
- **Walkthrough facts are second-hand.** Every work item starts by checking its facts in the game;
  the E1 script is where most of that happens.
