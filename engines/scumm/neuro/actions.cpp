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

// Her actions: what she is offered, and carrying out a verb sentence or a walk.

#include "scumm/neuro/neuro.h"

#include "common/debug.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/scumm.h"

namespace Scumm {

/** What a click sets off takes this long to show. */
static const uint32 kClickTakesMs = 1000;
/** A command is not judged finished before it has had this long to start. */
static const uint32 kCommandStartsMs = 600;
/** After someone has spoken, lines to choose from may still appear for this long. */
static const uint32 kLinesFollowMs = 2500;
/** A follow-up step is dropped if the step before it has not played out by then. */
static const uint32 kStepTimeoutMs = 15000;
static const uint32 kPassageTimeoutMs = 40000;
/** A step that has not been seen running by then evidently did nothing. */
static const uint32 kStepStartsMs = 1500;
/** The pause before stepping onto a place a second time. */
static const uint32 kSpotSettleMs = 1000;

// JSON values own their children, so every use needs a fresh copy
static Common::JSONValue *typeSchema(const char *type) {
	Common::JSONObject schema;
	schema.setVal("type", new Common::JSONValue(type));
	return new Common::JSONValue(schema);
}

static Common::JSONValue *objectSchema(const Common::JSONObject &properties, const char *required = nullptr, const char *alsoRequired = nullptr) {
	Common::JSONObject schema;
	schema.setVal("type", new Common::JSONValue("object"));
	schema.setVal("properties", new Common::JSONValue(properties));
	if (required) {
		Common::JSONArray names;
		names.push_back(new Common::JSONValue(required));
		if (alsoRequired)
			names.push_back(new Common::JSONValue(alsoRequired));
		schema.setVal("required", new Common::JSONValue(names));
	}
	return new Common::JSONValue(schema);
}

static Common::JSONValue *actionDefinition(const char *name, const char *description, Common::JSONValue *schema = nullptr) {
	Common::JSONObject action;
	action.setVal("name", new Common::JSONValue(name));
	action.setVal("description", new Common::JSONValue(description));
	if (schema)
		action.setVal("schema", schema);
	return new Common::JSONValue(action);
}

void NeuroBridge::unregisterAction(const char *name) {
	Common::JSONArray names;
	names.push_back(new Common::JSONValue(name));
	Common::JSONObject data;
	data.setVal("action_names", new Common::JSONValue(names));
	send("actions/unregister", &data);
}

void NeuroBridge::registerActions(const char *replace) {
	if (!replace)
		_registeredAt = g_system->getMillis();

	// Older servers keep the first definition of an action instead of
	// replacing it, so drop the old one before sending a new one.
	if (replace) {
		if (_interaction.prompt.action() == replace)
			endPrompt(false);
		unregisterAction(replace);
	}

	Common::JSONArray actions;
	const Common::String only = replace ? replace : "";
	// Once the game's verbs are known they are offered as an enum
	if (only.empty() || only == "interact") {
		Common::JSONObject verb;
		verb.setVal("type", new Common::JSONValue("string"));
		if (!_offeredVerbs.empty()) {
			Common::JSONArray names;
			for (const auto &offered : _offeredVerbs)
				names.push_back(new Common::JSONValue(offered.name));
			names.push_back(new Common::JSONValue(_walkVerb));
			verb.setVal("enum", new Common::JSONValue(names));
		}
		Common::JSONObject interact;
		interact.setVal("verb", new Common::JSONValue(verb));
		interact.setVal("target", typeSchema("string"));
		interact.setVal("second_target", typeSchema("string"));
		interact.setVal("then_verb", typeSchema("string"));
		interact.setVal("then_target", typeSchema("string"));

		const Common::String interactHelp = "Use a verb on something here or in your inventory. \"" + _walkVerb + "\" moves you. "
			"second_target: what an item is used on or given to. "
			"then_verb and then_target: an optional second step, done the instant the first ends.";
		actions.push_back(actionDefinition("interact", interactHelp.c_str(), objectSchema(interact, "verb", "target")));
	}

	if (only.empty() || only == "choose_dialogue") {
		Common::JSONObject option;
		option.setVal("type", new Common::JSONValue("integer"));
		option.setVal("minimum", new Common::JSONValue((long long int)1));
		Common::JSONObject choose;
		choose.setVal("option", new Common::JSONValue(option));
		actions.push_back(actionDefinition("choose_dialogue", "In a conversation, say the line with this number.", objectSchema(choose, "option")));
	}
	if (only.empty()) {
		actions.push_back(actionDefinition("check_inventory", "List everything you are carrying."));
		actions.push_back(actionDefinition("wait", "Let the game continue for a moment."));
		if (_allowSkip)
			actions.push_back(actionDefinition("skip_cutscene", "Skip the current cutscene."));

		Common::JSONObject journal;
		journal.setVal("show_completed", typeSchema("boolean"));
		journal.setVal("clues_for", typeSchema("string"));
		actions.push_back(actionDefinition("read_journal",
			"Read your goals and notes. clues_for reads one goal's clues, including a finished goal by its exact title or ID. show_completed lists finished goals.",
			objectSchema(journal)));

		Common::JSONObject note;
		note.setVal("text", typeSchema("string"));
		actions.push_back(actionDefinition("add_note", "Add a short note to your journal.", objectSchema(note, "text")));
	}
	if (only.empty() || only == "answer") {
		Common::JSONArray yesNo;
		yesNo.push_back(new Common::JSONValue("yes"));
		yesNo.push_back(new Common::JSONValue("no"));
		Common::JSONObject yesOrNo;
		yesOrNo.setVal("type", new Common::JSONValue("string"));
		yesOrNo.setVal("enum", new Common::JSONValue(yesNo));
		Common::JSONObject answer;
		answer.setVal("answer", new Common::JSONValue(yesOrNo));
		actions.push_back(actionDefinition("answer", "Answer a yes/no question from the game. Yes does what it offered, at once.", objectSchema(answer, "answer")));
	}

	Common::JSONObject data;
	data.setVal("actions", new Common::JSONValue(actions));
	send("actions/register", &data);
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
	if (needle.empty())
		return nullptr;
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
	// What the Escape key skips: a scene that has said where it resumes when
	// cut short. That need not be a cutscene in the game's own sense.
	if (_vm->vm.cutScenePtr[_vm->vm.cutSceneStackPointer] == 0) {
		sendActionResult(id, true, "No cutscene to skip.");
		return;
	}
	sendActionResult(id, true);
	_vm->abortCutscene();
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

	const Common::String command = _interaction.prompt.question().command;
	const Common::String thenCommand = _interaction.prompt.question().thenCommand;
	// These questions are about a passing moment
	if (command.empty() || _interaction.prompt.expired(_vm->_currentRoom, g_system->getMillis())) {
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
	cancelQueuedInput();
	// The second step is checked when it runs: what it needs may only appear after the first
	if (!thenCommand.empty()) {
		NeuroCommand next;
		next.preset = thenCommand;
		_interaction.queue.push_back(next);
	}
	runCommand(resolved);
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

bool NeuroBridge::resolveCommand(const NeuroWorld &world, const Common::String &verbName, const Common::String &targetName,
		const Common::String &secondName, NeuroCommand &command, Common::String &error) {
	command = NeuroCommand();

	// Walking is what a click does when no verb is selected, so it has no verb
	// of its own. Words she is likely to use for a verb the game spells
	// differently come from the description file.
	Common::String meant;
	const bool known = _data.synonym(verbName, meant);
	command.walk = verbName.equalsIgnoreCase(_walkVerb) || (known && meant.equalsIgnoreCase(_walkVerb));
	if (!command.walk) {
		const NeuroThing *verb = findThing(world.verbs, verbName);
		if (!verb && known)
			verb = findThing(world.verbs, meant);
		if (!verb) {
			error = Common::String::format("No verb '%s'. Verbs: %s, %s.",
				verbName.c_str(), neuroListNames(world.verbs).c_str(), _walkVerb.c_str());
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
				names[i]->c_str(), neuroListNames(room, world.spots).c_str(), neuroListNames(world.inventory).c_str());
			// It may be in sight but out of reach, across the water
			if (!world.standing.empty())
				error += " " + world.standing;
			return false;
		}
		command.objects[i] = thing->id;
	}

	if (command.objects[0] < 0 || command.objects[1] < 0) {
		if (!command.walk || command.objects[1]) {
			error = "That is a place: you can only \"" + _walkVerb + "\" it.";
			return false;
		}
		const int spot = -command.objects[0] - 1;
		command.x = world.spotPositions[spot].x;
		command.y = world.spotPositions[spot].y;
		command.note = world.spots[spot].note;
		command.isSpot = true;
		command.stepTwice = world.spotTwice[spot];
		return true;
	}

	if (command.walk && (!findThing(room, targetName) || !findClickPoint(command.objects[0], command.x, command.y))) {
		error = Common::String::format("%s something in the room: %s.", _walkVerb.c_str(), neuroListNames(room, world.spots).c_str());
		return false;
	}
	// Normally the thing acted on has its own script for the verb, and running
	// the sentence directly reaches it. Where it has none, and the room has
	// swapped in its own mouse handler for a scene, only that handler knows
	// what to do, and it only sees clicks.
	const bool handled = !_vm->objIsActor(command.objects[0]) && _vm->getVerbEntrypoint(command.objects[0], command.verb) != 0;
	if (!command.walk && !handled && needsClicks() && !planClicks(world, command))
		debug(1, "Neuro: Room %d has its own mouse handler, but this command could not be made as clicks", world.room);
	return true;
}

/** From here on, what happens is the outcome of her action, which she is told once it has played out. */
void NeuroBridge::beginAction() {
	_walkTarget = 0;
	_lastTargets[0] = _lastTargets[1] = 0;
	_interaction.outcome.begin(_vm->_currentRoom, g_system->getMillis());
}

void NeuroBridge::runCommand(const NeuroCommand &command) {
	beginAction();
	_queueSawBusy = false;
	_queueIdleSince = 0;
	_walkTarget = command.walk ? command.objects[0] : 0;
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
	// Show the pointer going to the verb and to each thing, then give the sentence
	if (_pointerGlide && _fastMode != kFastAlways) {
		NeuroWorld world;
		collectWorld(world);
		NeuroCommand shown = command;
		if (planClicks(world, shown)) {
			for (auto &click : shown.clicks)
				click.press = false;
			_shownSentence = true;
			_shownVerb = command.verb;
			_shownObjects[0] = command.objects[0];
			_shownObjects[1] = command.objects[1];
			_clickQueue = shown.clicks;
			clickAt(_clickQueue.remove_at(0));
			return;
		}
	}
	_vm->doSentence(command.verb, command.objects[0], command.objects[1]);
}

void NeuroBridge::updateQueuedCommand() {
	if (_interaction.queue.empty())
		return;

	// A simulated click has not started anything yet
	if (_clickPhase != kClickIdle)
		return;

	const uint32 elapsed = _interaction.outcome.age(g_system->getMillis());
	if (elapsed > (_interaction.queue[0].afterPassage ? kPassageTimeoutMs : kStepTimeoutMs)) {
		const Common::String step = _interaction.queue[0].preset;
		_interaction.queue.clear();
		sendContext((step.empty() ? Common::String("The follow-up movement") : "The follow-up (" + step + ")") +
			" did not start before time ran out.", false);
		_interaction.outcome.report();
		return;
	}

	// Go as soon as the first command has played out
	const bool busy = _vm->_sentenceNum > 0 || sentenceRunning() || ego()->_moving;
	// The first command takes a moment to get going, so wait until it has
	// been seen running, or long enough that it evidently did nothing.
	if (busy)
		_queueSawBusy = true;
	const uint32 now = g_system->getMillis();
	if (busy || !playerHasControl() || (!_queueSawBusy && elapsed < kStepStartsMs)) {
		_queueIdleSince = 0;
		return;
	}
	if (!_queueIdleSince)
		_queueIdleSince = now;
	if (now - _queueIdleSince < _interaction.queue[0].settleMs)
		return;

	NeuroCommand next = _interaction.queue.remove_at(0);
	if (!next.preset.empty()) {
		NeuroWorld world;
		collectWorld(world);
		const Common::String step = next.preset;
		if (!resolvePreset(world, step, next)) {
			_interaction.queue.clear();
			sendContext("Could not carry out the follow-up (" + step + ") with what is available now.", false);
			_interaction.outcome.report();
			return;
		}
	}
	debug(1, "Neuro: Running queued step after %u ms, at %d,%d", elapsed, ego()->getPos().x, ego()->getPos().y);
	_lastQueuedStepAt = g_system->getMillis();
	runCommand(next);
}

void NeuroBridge::finishAction(const NeuroWorld &world) {
	if (!_interaction.outcome.pending())
		return;

	// Still going: a click not yet made, steps queued, walking, the game's own script
	// (what a click sets off takes a moment to show)
	const bool busy = _clickPhase != kClickIdle || !_interaction.queue.empty() || !_clickQueue.empty() || _pagerState != kPagerIdle
		|| g_system->getMillis() - _lastClickAt < kClickTakesMs
		|| ego()->_moving || sentenceRunning();
	if (busy || _interaction.outcome.age(g_system->getMillis()) < kCommandStartsMs)
		return;
	// After someone has spoken, lines to choose from may be about to appear;
	// they bring their own prompt, so give them a moment first.
	if (_interaction.outcome.spoken() && !world.inDialogue && g_system->getMillis() - _controlSince < kLinesFollowMs)
		return;

	_interaction.outcome.finish();
	if (_walkTarget < 0 && !_interaction.outcome.reported() && !world.inDialogue && !_data.groundBetween(world.room).empty()) {
		sendContext(describeRoom(world, kRoomRevisit), false);
		return;
	}
	// A conversation prompts her with the lines to choose from. Otherwise she
	// needs a cue that is hers: the replies to her action were not sent as one.
	if (!_interaction.outcome.reported() && !world.inDialogue) {
		Common::String cue = _interaction.outcome.spoken() ? "Your move." : "No further visible response.";
		if (!_interaction.outcome.spoken() && !_walkTarget) {
			for (const auto &item : world.inventory) {
				if (item.id == _lastTargets[0])
					cue += " You are carrying " + item.name + ".";
			}
		}
		// Walking to a door goes through it only if it is open. A player sees
		// the character stop in front of a shut one; she has to be told.
		for (const auto &thing : world.objects) {
			const Common::String state = thing.id == _walkTarget ? _data.stateName(thing.name, thing.state) : "";
			if (!state.empty())
				cue = Common::String::format("%s is at the %s. It is %s.", egoNameCapital().c_str(), thing.name.c_str(), state.c_str());
		}
		sendContext(cue, false);
	}
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
	const Common::String thenSecond = args.get("then_second_target|then_with|then_on");

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
		verbNames.push_back(_walkVerb);
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
			|| (!thenVerb.empty() && !resolveCommand(world, thenVerb, thenTarget, thenSecond, second, error))) {
		sendActionResult(id, false, error);
		return;
	}

	// For a place, say what stepping there looks like
	sendActionResult(id, true, first.note);
	cancelQueuedInput();
	NeuroCommand passage;
	if (!first.isSpot && passageTo(world.room, first.objects[0], passage)) {
		first.afterPassage = true;
		_interaction.queue.push_back(first);
		first = passage;
	}
	// A place may only react to a step taken from a standstill right next
	// to it: walk there, pause, then step onto it again.
	if (first.stepTwice) {
		NeuroCommand again = first;
		again.settleMs = kSpotSettleMs;
		_interaction.queue.push_back(again);
	}
	if (!thenVerb.empty()) {
		second.preset = thenVerb + " | " + thenTarget;
		if (!thenSecond.empty())
			second.preset += " | " + thenSecond;
		_interaction.queue.push_back(second);
	}
	runCommand(first);
}

/**
 * Some wide rooms keep the picture on one part and only move it when the
 * player's character goes through a particular spot. Sent straight to
 * something in the other part, the character would walk out of the picture.
 * Finds the walk a player would make first.
 */
bool NeuroBridge::passageTo(int room, int target, NeuroCommand &passage) const {
	if (target <= 0 || _vm->objIsActor(target) || _vm->VAR_CAMERA_MIN_X == 0xFF || _vm->VAR_CAMERA_MAX_X == 0xFF)
		return false;
	const Common::Array<int> passages = _data.passages(room);
	if (passages.empty())
		return false;

	// The stretch of the room the picture can show as things stand
	const int left = _vm->VAR(_vm->VAR_CAMERA_MIN_X) - _vm->_screenWidth / 2;
	const int right = _vm->VAR(_vm->VAR_CAMERA_MAX_X) + _vm->_screenWidth / 2;
	int inView = 0;
	bool targetFound = false;
	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		const bool shown = od.x_pos + od.width > left && od.x_pos < right;
		if (od.obj_nr == target) {
			if (shown)
				return false;
			targetFound = true;
		} else if (shown && !inView) {
			for (int id : passages) {
				if (id == od.obj_nr)
					inView = id;
			}
		}
	}
	if (!targetFound || !inView)
		return false;

	passage = NeuroCommand();
	passage.walk = true;
	passage.objects[0] = inView;
	if (!findClickPoint(inView, passage.x, passage.y))
		return false;
	debug(1, "Neuro: Thing %d is out of view (%d to %d), going by way of %d", target, left, right, inView);
	return true;
}

} // End of namespace Scumm
