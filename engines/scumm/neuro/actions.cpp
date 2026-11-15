/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "scumm/neuro/neuro.h"

#include "common/debug.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/resource.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

static const char *const kWalkVerbName = "Walk to";

// Mirrors the button state flags private to input.cpp
enum {
	kMouseDown = 1,
	kMouseClicked = 2
};

static Common::String listNames(const NeuroThingList &a, const NeuroThingList &b = NeuroThingList()) {
	Common::String result;
	for (const NeuroThingList *list : {&a, &b}) {
		for (const auto &thing : *list) {
			if (!result.empty())
				result += ", ";
			result += thing.name;
		}
	}
	return result.empty() ? Common::String("nothing") : result;
}

/** Lower case, with punctuation dropped: "pot o' stew" and "Pot o stew" are the same name. */
static Common::String simplifyName(const Common::String &name) {
	Common::String result;
	for (uint i = 0; i < name.size(); i++) {
		const char c = name[i];
		if (Common::isAlnum(c) || c == ' ' || (c & 0x80))
			result += Common::isAlpha(c) ? tolower(c) : c;
	}
	result.trim();
	return result;
}

const NeuroThing *NeuroBridge::findThing(const NeuroThingList &things, const Common::String &name) const {
	const Common::String needle = simplifyName(name);
	for (const auto &thing : things) {
		if (simplifyName(thing.name) == needle)
			return &thing;
	}

	// Accept a partial name as long as it is unambiguous
	const NeuroThing *match = nullptr;
	for (const auto &thing : things) {
		// The note counts too: "kitchen" finds "door 2 (leads to the kitchen)"
		const Common::String candidate = simplifyName(thing.name + " " + thing.note);
		if (candidate.contains(needle)) {
			if (match)
				return nullptr;
			match = &thing;
		}
	}
	if (match)
		return match;

	// Last, every word she used appears somewhere in the name or note:
	// "kitchen door" finds "door 2 (leads to the kitchen)"
	Common::StringTokenizer words(needle, " ");
	Common::StringArray wanted = words.split();
	if (wanted.empty())
		return nullptr;
	for (const auto &thing : things) {
		const Common::String candidate = " " + simplifyName(thing.name + " " + thing.note) + " ";
		bool all = true;
		for (const auto &word : wanted)
			all &= candidate.contains(" " + word + " ");
		if (all) {
			if (match)
				return nullptr;
			match = &thing;
		}
	}
	return match;
}

void NeuroBridge::actionSkipCutscene(const Common::String &id) {
	const int depth = _vm->vm.cutSceneStackPointer;
	if (depth == 0 || _vm->vm.cutScenePtr[depth] == 0) {
		sendActionResult(id, true, "No cutscene to skip.");
		return;
	}
	sendActionResult(id, true);
	_vm->abortCutscene();
}

static Common::String listDialogue(const NeuroThingList &lines) {
	Common::String text;
	for (uint i = 0; i < lines.size(); i++)
		text += Common::String::format("%u. %s\n", i + 1, lines[i].name.c_str());
	return text;
}

void NeuroBridge::scrollArrows(const NeuroWorld &world, int &up, int &down) {
	up = down = 0;
	if (!world.inDialogue || world.verbs.empty())
		return;
	int firstLine = 0x7FFF;
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (vs.verbid == world.verbs[0].id)
			firstLine = vs.curRect.top;
	}
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (!vs.verbid || vs.saveid != 0 || vs.curmode != 1 || vs.type != kTextVerbType)
			continue;
		if (!cleanText(_vm->getResourceAddress(rtVerb, slot), true).empty())
			continue;
		// An arrow has no text; the one level with the first line scrolls up
		if (vs.curRect.top <= firstLine + 4)
			up = vs.verbid;
		else
			down = vs.verbid;
	}
}

bool NeuroBridge::pagerCovers(const NeuroWorld &world) const {
	if (_pagerLines.empty() || world.verbs.empty())
		return false;
	for (const auto &verb : world.verbs) {
		bool listed = false;
		for (const auto &line : _pagerLines)
			listed |= line.name == verb.name;
		if (!listed)
			return false;
	}
	return true;
}

void NeuroBridge::pressArrow(int verb, const NeuroWorld &world) {
	_pagerSaw = world.verbs[0].name;
	_pagerPressedAt = g_system->getMillis();
	_vm->runInputScript(kVerbClickArea, verb, 1);
}

void NeuroBridge::updatePager() {
	if (_pagerState == kPagerIdle)
		return;

	NeuroWorld world;
	collectWorld(world);
	if (!playerHasControl() || !world.inDialogue || world.verbs.empty()) {
		_pagerState = kPagerIdle;
		_pagerLines.clear();
		return;
	}
	// The list moves a frame or so after an arrow is pressed
	const uint32 sincePress = g_system->getMillis() - _pagerPressedAt;
	if (_pagerPressedAt && world.verbs[0].name == _pagerSaw && sincePress < 1000)
		return;
	_pagerPressedAt = 0;

	int up, down;
	scrollArrows(world, up, down);

	if (_pagerState == kPagerToTop) {
		if (up) {
			pressArrow(up, world);
			return;
		}
		_pagerLines.clear();
		_pagerState = kPagerDown;
	}
	if (_pagerState == kPagerDown) {
		for (const auto &verb : world.verbs) {
			bool listed = false;
			for (const auto &line : _pagerLines)
				listed |= line.name == verb.name;
			if (!listed)
				_pagerLines.push_back(verb);
		}
		if (down)
			pressArrow(down, world);
		else
			_pagerState = kPagerIdle;
		return;
	}

	// Seeking the line she chose
	for (const auto &verb : world.verbs) {
		if (verb.name == _pagerTarget) {
			_pagerState = kPagerIdle;
			_pagerLines.clear();
			_commandStartedAt = g_system->getMillis();
			_vm->runInputScript(kVerbClickArea, verb.id, 1);
			return;
		}
	}
	uint target = 0, first = 0;
	for (uint i = 0; i < _pagerLines.size(); i++) {
		if (_pagerLines[i].name == _pagerTarget)
			target = i;
		if (_pagerLines[i].name == world.verbs[0].name)
			first = i;
	}
	const int arrow = target < first ? up : down;
	if (!arrow) {
		warning("Neuro: Could not scroll to the line '%s'", _pagerTarget.c_str());
		_pagerState = kPagerIdle;
		_pagerLines.clear();
		return;
	}
	pressArrow(arrow, world);
}

void NeuroBridge::actionChooseDialogue(const Common::String &id, const NeuroArgs &args) {
	NeuroWorld world;
	collectWorld(world);
	if (!playerHasControl() || !world.inDialogue || _pagerState != kPagerIdle) {
		sendActionResult(id, false, "Nothing to choose now.");
		return;
	}
	// The whole list, where only part of it is on screen
	int up, down;
	scrollArrows(world, up, down);
	const bool paged = (up || down) && pagerCovers(world);
	if (paged)
		world.verbs = _pagerLines;

	// A number, possibly dressed up ("option 3", "3."), or the line itself
	const Common::String choice = args.get("option|choice|line|number|index|id|text", true);
	int option = 0;
	for (uint i = 0; i < choice.size() && !option; i++) {
		if (Common::isDigit(choice[i]))
			option = atoi(choice.c_str() + i);
	}
	if (!option && !choice.empty()) {
		const NeuroThing *line = findThing(world.verbs, choice);
		for (uint i = 0; line && i < world.verbs.size(); i++) {
			if (&world.verbs[i] == line)
				option = i + 1;
		}
	}
	if (option < 1 || option > (int)world.verbs.size()) {
		sendActionResult(id, false, "Pick a line by number:\n" + listDialogue(world.verbs));
		return;
	}

	sendActionResult(id, true);
	// What is said next is the reply to her line, not something to react to separately
	_commandStartedAt = g_system->getMillis();
	_awaitingOutcome = true;
	_outcomeTold = false;
	_outcomeSpoken = false;
	// Answered; it is not what her next choice is about
	_lastHeard.clear();
	if (paged) {
		_pagerTarget = world.verbs[option - 1].name;
		_pagerState = kPagerSeek;
		return;
	}
	_pagerLines.clear();
	_vm->runInputScript(kVerbClickArea, world.verbs[option - 1].id, 1);
}

bool NeuroBridge::resolvePreset(const NeuroWorld &world, const Common::String &preset, NeuroCommand &command) {
	Common::StringTokenizer parts(preset, "|");
	Common::StringArray fields = parts.split();
	for (auto &field : fields)
		field.trim();
	while (fields.size() < 3)
		fields.push_back(Common::String());
	Common::String error;
	return resolveCommand(world, fields[0], fields[1], fields[2], command, error);
}

void NeuroBridge::actionAnswer(const Common::String &id, const NeuroArgs &args) {
	Common::String answer = args.get("answer|response|reply|value|choice", true);
	answer.toLowercase();
	if (answer == "y" || answer == "true" || answer == "1" || answer == "ok" || answer == "sure" || answer.hasPrefix("yes"))
		answer = "yes";
	else if (answer == "n" || answer == "false" || answer == "0" || answer.hasPrefix("no"))
		answer = "no";
	if (answer != "yes" && answer != "no") {
		sendActionResult(id, false, "Answer \"yes\" or \"no\".");
		return;
	}

	const Common::String command = _questionCommand;
	const Common::String thenCommand = _questionThen;
	_questionCommand.clear();
	_questionThen.clear();
	// These questions are about a passing moment
	if (command.empty() || g_system->getMillis() - _questionAskedAt > kQuestionOpenMs) {
		sendActionResult(id, true, "No question is waiting.");
		return;
	}
	if (answer == "no") {
		sendActionResult(id, true);
		return;
	}

	NeuroWorld world;
	collectWorld(world);
	NeuroCommand resolved;
	if (!playerHasControl() || !resolvePreset(world, command, resolved)) {
		// Not a failure she should retry: the moment has simply gone
		sendActionResult(id, true, "Too late.");
		return;
	}
	sendActionResult(id, true);
	_queue.clear();
	// The second step is checked when it runs: what it needs may only appear after the first
	if (!thenCommand.empty()) {
		NeuroCommand next;
		next.preset = thenCommand;
		_queue.push_back(next);
	}
	runCommand(resolved);
}

void NeuroBridge::updateForces(const NeuroWorld &world) {
	const uint32 now = g_system->getMillis();

	// A force that was never answered must not block the game forever
	if (_forceActive && now - _forceSentAt > 90000)
		_forceActive = false;

	if (world.inDialogue) {
		if (_pagerState != kPagerIdle)
			return;
		int up, down;
		scrollArrows(world, up, down);
		const bool covered = (up || down) && pagerCovers(world);
		if ((up || down) && !covered) {
			// Wait out the moment in which her last choice is still on screen
			if (!_forceActive && now - _lastActionAt > 1000) {
				_pagerPressedAt = 0;
				_pagerState = kPagerToTop;
			}
			return;
		}
		if (!covered)
			_pagerLines.clear();
		const Common::String lines = listDialogue(covered ? _pagerLines : world.verbs);
		// A second force while one is outstanding confuses the server, even if the lines have changed
		// The chosen line can stay on screen for a moment after she picks it
		const bool fresh = lines != _forcedDialogue || now - _lastActionAt > 3000;
		if (!_forceActive && fresh) {
			_forcedDialogue = lines;
			// The line she is answering goes with the choice, so it cannot be missed
			Common::String state = lines;
			if (!_lastHeard.empty() && now - _lastHeardAt < 30000)
				state = "Last said to you: " + _lastHeard + "\n\n" + lines;
			Common::String query = _data.roomPrompt(world.room);
			if (query.empty())
				query = "Pick a line to say.";
			sendForce(state, query, "choose_dialogue");
		}
		return;
	}
	_forcedDialogue.clear();
	_pagerLines.clear();

	if (_idleForceMs && !_forceActive && now - _lastActivity > _idleForceMs) {
		_lastActivity = now;
		sendForce(describeRoom(world, kRoomStatus), "Your move.", "interact");
	}
}

bool NeuroBridge::resolveCommand(const NeuroWorld &world, const Common::String &verbName, const Common::String &targetName,
		const Common::String &secondName, NeuroCommand &command, Common::String &error) {
	command = NeuroCommand();

	// Walking is what a click does when no verb is selected, so it has no verb of its own
	static const char *const walkWords[] = {"walk", "go to", "go", "move to", "walk to the", "enter", "approach"};
	command.walk = verbName.equalsIgnoreCase(kWalkVerbName);
	for (const char *word : walkWords)
		command.walk |= verbName.equalsIgnoreCase(word);
	if (!command.walk) {
		const NeuroThing *verb = findThing(world.verbs, verbName);
		// Words she is likely to use for a verb the game spells differently
		static const char *const synonyms[][2] = {
			{"take", "Pick up"}, {"grab", "Pick up"}, {"get", "Pick up"}, {"pickup", "Pick up"},
			{"examine", "Look at"}, {"inspect", "Look at"}, {"read", "Look at"},
			{"speak", "Talk to"}, {"speak to", "Talk to"}, {"speak with", "Talk to"}, {"talk with", "Talk to"}, {"ask", "Talk to"}
		};
		for (uint i = 0; !verb && i < ARRAYSIZE(synonyms); i++) {
			if (verbName.equalsIgnoreCase(synonyms[i][0]))
				verb = findThing(world.verbs, synonyms[i][1]);
		}
		if (!verb) {
			error = Common::String::format("No verb '%s'. Verbs: %s, %s.",
				verbName.c_str(), listNames(world.verbs).c_str(), kWalkVerbName);
			return false;
		}
		command.verb = verb->id;
	}

	NeuroThingList room = world.objects;
	room.push_back(world.actors);

	NeuroThingList everything = room;
	everything.push_back(world.inventory);
	everything.push_back(world.spots);

	const Common::String *names[2] = {&targetName, &secondName};
	for (int i = 0; i < 2; i++) {
		if (names[i]->empty())
			continue;
		const NeuroThing *thing = findThing(everything, *names[i]);
		if (!thing) {
			error = Common::String::format("No '%s' here. Room: %s. Inventory: %s.",
				names[i]->c_str(), listNames(room, world.spots).c_str(), listNames(world.inventory).c_str());
			return false;
		}
		command.objects[i] = thing->id;
	}

	if (command.objects[0] < 0 || command.objects[1] < 0) {
		if (!command.walk || command.objects[1]) {
			error = "That is a place: you can only \"Walk to\" it.";
			return false;
		}
		const int spot = -command.objects[0] - 1;
		command.x = world.spotPositions[spot].x;
		command.y = world.spotPositions[spot].y;
		command.note = world.spots[spot].note;
		command.isSpot = true;
		return true;
	}

	if (command.walk && (!findThing(room, targetName) || !findClickPoint(command.objects[0], command.x, command.y))) {
		error = Common::String::format("Walk to something in the room: %s.", listNames(room, world.spots).c_str());
		return false;
	}
	// Normally the thing acted on has its own script for the verb, and running
	// the sentence directly reaches it. Where it has none, and the room has
	// swapped in its own mouse handler for a scene (giving the circus brothers
	// a helmet), only that handler knows what to do, and it only sees clicks.
	const bool handled = !_vm->objIsActor(command.objects[0]) && _vm->getVerbEntrypoint(command.objects[0], command.verb) != 0;
	if (!command.walk && !handled && needsClicks() && !planClicks(world, command))
		debug(1, "Neuro: Room %d has its own mouse handler, but this command could not be made as clicks", world.room);
	return true;
}

void NeuroBridge::runCommand(const NeuroCommand &command) {
	_commandStartedAt = g_system->getMillis();
	_awaitingOutcome = true;
	_outcomeTold = false;
	_outcomeSpoken = false;
	_queueSawBusy = false;
	_queueIdleSince = 0;
	if (command.walk) {
		clickInRoom(command.x, command.y);
		return;
	}
	_lastTargets[0] = command.objects[0];
	_lastTargets[1] = command.objects[1];
	if (!command.clicks.empty()) {
		_clickQueue = command.clicks;
		clickAt(_clickQueue.remove_at(0));
		return;
	}
	_vm->doSentence(command.verb, command.objects[0], command.objects[1]);
}

void NeuroBridge::updateQueuedCommand() {
	if (_queue.empty())
		return;

	// A simulated click has not started anything yet
	if (_clickPhase != kClickIdle)
		return;

	const uint32 elapsed = g_system->getMillis() - _commandStartedAt;
	if (elapsed > 15000) {
		_queue.clear();
		return;
	}

	// Go as soon as the first command has played out
	const bool busy = _vm->_sentenceNum > 0
		|| _vm->isScriptRunning(_vm->VAR(_vm->VAR_SENTENCE_SCRIPT))
		|| _vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_moving;
	// The first command takes a moment to get going, so wait until it has
	// been seen running, or long enough that it evidently did nothing.
	if (busy)
		_queueSawBusy = true;
	const uint32 now = g_system->getMillis();
	if (busy || !playerHasControl() || (!_queueSawBusy && elapsed < 1500)) {
		_queueIdleSince = 0;
		return;
	}
	if (!_queueIdleSince)
		_queueIdleSince = now;
	if (now - _queueIdleSince < _queue[0].settleMs)
		return;

	NeuroCommand next = _queue.remove_at(0);
	if (!next.preset.empty()) {
		NeuroWorld world;
		collectWorld(world);
		if (!resolvePreset(world, next.preset, next)) {
			_queue.clear();
			return;
		}
	}
	debug(1, "Neuro: Running queued step after %u ms, Guybrush at %d,%d", elapsed,
		_vm->_actors[_vm->VAR(_vm->VAR_EGO)]->getPos().x, _vm->_actors[_vm->VAR(_vm->VAR_EGO)]->getPos().y);
	_lastQueuedStepAt = g_system->getMillis();
	runCommand(next);
}

/**
 * Splits a plain sentence ("look at the fireplace please", "use meat with pot")
 * into a verb and what follows it. The verb has to be one of the given names.
 */
static bool splitSentence(const Common::String &sentence, const Common::StringArray &verbNames,
		Common::String &verb, Common::String &target, Common::String &second) {
	Common::String text = sentence;
	text.toLowercase();

	// Longest verb first, so that "walk to" wins over "walk"
	size_t best = 0;
	for (const auto &name : verbNames) {
		Common::String candidate = name;
		candidate.toLowercase();
		if (candidate.size() > best && (text == candidate || text.hasPrefix(candidate + " "))) {
			best = candidate.size();
			verb = name;
		}
	}
	if (!best)
		return false;

	Common::String rest = " " + text.substr(best) + " ";
	static const char *const fillers[] = {" the ", " a ", " an ", " please ", " now ", " my "};
	for (const char *filler : fillers) {
		size_t at;
		while ((at = rest.find(filler)) != Common::String::npos)
			rest = rest.substr(0, at) + " " + rest.substr(at + strlen(filler));
	}
	static const char *const joiners[] = {" with ", " on ", " to ", " in "};
	for (const char *joiner : joiners) {
		const size_t at = rest.find(joiner);
		// not the "to" of "talk to": something has to come before it
		if (at != Common::String::npos && at > 0) {
			second = rest.substr(at + strlen(joiner));
			rest = rest.substr(0, at);
			break;
		}
	}
	rest.trim();
	second.trim();
	target = rest;
	return !target.empty();
}

void NeuroBridge::actionInteract(const Common::String &id, const NeuroArgs &args) {
	Common::String verbName = args.get("verb|action|command");
	Common::String targetName = args.get("target|object|thing|item|name|first_target|target1");
	Common::String secondName = args.get("second_target|second|with|on|to|target2|recipient");
	const Common::String thenVerb = args.get("then_verb|then|next_verb|second_verb");
	const Common::String thenTarget = args.get("then_target|next_target");

	if (!playerHasControl()) {
		sendActionResult(id, false, "Not now: a scene is playing.");
		return;
	}

	NeuroWorld world;
	collectWorld(world);
	if (world.inDialogue) {
		sendActionResult(id, false, "In a conversation: use choose_dialogue.");
		return;
	}

	// A sentence instead of fields, or the whole sentence put in the verb
	if (targetName.empty()) {
		Common::StringArray verbNames;
		for (const auto &verb : world.verbs)
			verbNames.push_back(verb.name);
		verbNames.push_back(kWalkVerbName);
		const Common::String sentence = verbName.empty() ? args.get("text|sentence|input", true) : verbName;
		splitSentence(sentence, verbNames, verbName, targetName, secondName);
	}

	if (verbName.empty() || targetName.empty()) {
		sendActionResult(id, false, "interact needs a verb and a target.");
		return;
	}
	if (thenVerb.empty() != thenTarget.empty()) {
		sendActionResult(id, false, "then_verb and then_target must be given together.");
		return;
	}

	NeuroCommand first, second;
	Common::String error;
	if (!resolveCommand(world, verbName, targetName, secondName, first, error)
			|| (!thenVerb.empty() && !resolveCommand(world, thenVerb, thenTarget, args.get("then_second_target|then_with|then_on"), second, error))) {
		sendActionResult(id, false, error);
		return;
	}

	// For a place, say what stepping there looks like
	sendActionResult(id, true, first.note);
	_queue.clear();
	// A place may only react to a step taken from a standstill right next
	// to it (a loose plank, say): walk there, pause, then step onto it again.
	if (first.isSpot) {
		NeuroCommand again = first;
		again.settleMs = 1000;
		_queue.push_back(again);
	}
	if (!thenVerb.empty())
		_queue.push_back(second);
	runCommand(first);
}

bool NeuroBridge::findClickPoint(int thing, int &x, int &y) const {
	if (_vm->objIsActor(thing)) {
		const Actor *actor = _vm->_actors[_vm->objToActor(thing)];
		x = actor->getPos().x;
		y = (actor->_top + actor->_bottom) / 2;
		return true;
	}

	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr != thing)
			continue;
		// Another object may overlap this one, so probe a few points
		const int fractions[] = {2, 1, 3};
		for (int row = 0; row < 3; row++) {
			for (int col = 0; col < 3; col++) {
				x = od.x_pos + (od.width - 1) * fractions[col] / 4;
				y = od.y_pos + (od.height - 1) * fractions[row] / 4;
				if (_vm->findObject(x, y) == thing)
					return true;
			}
		}
	}
	return false;
}

void NeuroBridge::clickInRoom(int x, int y) {
	NeuroClick click;
	click.x = x;
	click.y = y;
	clickAt(click);
}

void NeuroBridge::clickAt(const NeuroClick &click) {
	_clickX = click.x;
	_clickY = click.y;
	_clickScreen = click.screen;
	_clickPhase = kClickHover;
	_clickFrames = 0;
}

bool NeuroBridge::needsClicks() const {
	// Scripts numbered past the global ones belong to the current room
	return _vm->VAR(_vm->VAR_VERB_SCRIPT) >= _vm->_numGlobalScripts;
}

bool NeuroBridge::verbButton(int verbId, NeuroClick &click) const {
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (vs.verbid != verbId || vs.curmode != 1 || vs.saveid != 0 || vs.curRect.isEmpty())
			continue;
		click.x = (vs.curRect.left + vs.curRect.right) / 2;
		click.y = (vs.curRect.top + vs.curRect.bottom) / 2;
		click.screen = true;
		return true;
	}
	return false;
}

/**
 * Turns a sentence into the clicks a player would make: the verb, then each
 * thing, in the inventory or in the room.
 */
bool NeuroBridge::planClicks(const NeuroWorld &world, NeuroCommand &command) {
	// Monkey Island's inventory: picture buttons 200 and up, filled in order
	static const int kFirstInventoryButton = 200;

	command.clicks.clear();
	NeuroClick click;
	if (!verbButton(command.verb, click))
		return false;
	command.clicks.push_back(click);

	for (const int thing : command.objects) {
		if (!thing)
			break;
		int index = -1;
		for (uint i = 0; i < world.inventory.size(); i++) {
			if (world.inventory[i].id == thing)
				index = i;
		}
		if (index >= 0) {
			if (!verbButton(kFirstInventoryButton + index, click))
				return false;
		} else {
			int x, y;
			if (!findClickPoint(thing, x, y))
				return false;
			// Only what is on screen can be clicked
			const int screenX = x - _vm->_virtscr[kMainVirtScreen].xstart;
			if (screenX < 0 || screenX >= _vm->_screenWidth)
				return false;
			click.x = x;
			click.y = y;
			click.screen = false;
		}
		command.clicks.push_back(click);
	}
	return true;
}

void NeuroBridge::updateClick() {
	switch (_clickPhase) {
	case kClickHover:
		if (++_clickFrames >= 3)
			_clickPhase = kClickPress;
		break;
	case kClickPress:
		_vm->_leftBtnPressed |= kMouseClicked | kMouseDown;
		_clickPhase = kClickRelease;
		break;
	case kClickRelease:
		_vm->_leftBtnPressed &= ~kMouseDown;
		_clickPhase = kClickIdle;
		_lastClickAt = g_system->getMillis();
		// The next click of the same command, once the game has taken this one in
		if (!_clickQueue.empty()) {
			clickAt(_clickQueue.remove_at(0));
			_clickFrames = -3;
		}
		break;
	default:
		break;
	}
}

void NeuroBridge::overridePointer() {
	if (_clickPhase == kClickIdle)
		return;

	// The target may be scrolled out of view, so only the room position is
	// exact; the screen position just has to be somewhere over the room.
	const VirtScreen &vs = _vm->_virtscr[kMainVirtScreen];
	if (_clickScreen) {
		// On the verbs or the inventory, below the room
		_vm->_mouse.x = _clickX;
		_vm->_mouse.y = _clickY;
		_vm->_virtualMouse.x = _clickX + vs.xstart;
		_vm->_virtualMouse.y = (_clickY >= vs.topline && _clickY < vs.topline + vs.h) ? _clickY - vs.topline : -1;
		return;
	}
	_vm->_virtualMouse.x = _clickX;
	_vm->_virtualMouse.y = _clickY;
	_vm->_mouse.x = CLIP<int>(_clickX - vs.xstart, 0, _vm->_screenWidth - 1);
	_vm->_mouse.y = CLIP<int>(_clickY + vs.topline, vs.topline, vs.topline + vs.h - 1);
}

} // End of namespace Scumm
