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

#include "common/system.h"

#include "scumm/actor.h"
#include "scumm/object.h"
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

static Common::String getStringArg(const Common::JSONValue *args, const char *key) {
	if (!args || !args->isObject() || !args->hasChild(key))
		return Common::String();
	const Common::JSONValue *value = args->asObject().getVal(key);
	if (!value || !value->isString())
		return Common::String();
	Common::String result = value->asString();
	result.trim();
	return result;
}

/** Lower case, with punctuation dropped: "pot o' stew" and "Pot o stew" are the same name. */
static Common::String simplifyName(const Common::String &name) {
	Common::String result;
	for (uint i = 0; i < name.size(); i++) {
		const char c = name[i];
		if (Common::isAlnum(c) || c == ' ' || c == '#' || (c & 0x80))
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
		const Common::String candidate = simplifyName(thing.name);
		if (candidate.contains(needle)) {
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
		sendActionResult(id, true, "There is no skippable cutscene playing right now.");
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

void NeuroBridge::actionChooseDialogue(const Common::String &id, const Common::JSONValue *args) {
	NeuroWorld world;
	collectWorld(world);
	if (!playerHasControl() || !world.inDialogue) {
		sendActionResult(id, false, "There is nothing to choose right now.");
		return;
	}

	int option = 0;
	if (args && args->isObject() && args->hasChild("option")) {
		const Common::JSONValue *value = args->asObject().getVal("option");
		if (value->isIntegerNumber())
			option = (int)value->asIntegerNumber();
		else if (value->isNumber())
			option = (int)value->asNumber();
		else if (value->isString())
			option = atoi(value->asString().c_str());
	}
	if (option < 1 || option > (int)world.verbs.size()) {
		sendActionResult(id, false, "Pick one of these lines by its number:\n" + listDialogue(world.verbs));
		// The server retries a failed force by itself; do not send another
		_forceActive = true;
		_forceSentAt = g_system->getMillis();
		return;
	}

	sendActionResult(id, true);
	_forcedDialogue.clear();
	_vm->runInputScript(kVerbClickArea, world.verbs[option - 1].id, 1);
}

void NeuroBridge::updateForces(const NeuroWorld &world) {
	const uint32 now = g_system->getMillis();

	// A force that was never answered must not block the game forever
	if (_forceActive && now - _forceSentAt > 60000)
		_forceActive = false;

	if (world.inDialogue) {
		const Common::String lines = listDialogue(world.verbs);
		if (lines != _forcedDialogue || !_forceActive) {
			_forcedDialogue = lines;
			sendForce("Lines Guybrush can say:\n" + lines, "Choose what to say next.", "choose_dialogue");
		}
		return;
	}
	_forcedDialogue.clear();

	if (_idleForceMs && !_forceActive && now - _lastActivity > _idleForceMs) {
		_lastActivity = now;
		sendForce(describeRoom(world), "The game is waiting for you. Decide what to do next.", "interact");
	}
}

void NeuroBridge::actionInteract(const Common::String &id, const Common::JSONValue *args) {
	const Common::String verbName = getStringArg(args, "verb");
	const Common::String targetName = getStringArg(args, "target");
	const Common::String secondName = getStringArg(args, "second_target");

	if (verbName.empty() || targetName.empty()) {
		sendActionResult(id, false, "interact needs a verb and a target.");
		return;
	}
	if (!playerHasControl()) {
		sendActionResult(id, false, "You can't do anything right now: wait until the current scene has finished.");
		return;
	}

	NeuroWorld world;
	collectWorld(world);
	if (world.inDialogue) {
		sendActionResult(id, false, "You are in a conversation: use choose_dialogue to pick what to say.");
		return;
	}

	// Walking is what a click does when no verb is selected, so it has no verb of its own
	const bool walk = verbName.equalsIgnoreCase(kWalkVerbName) || verbName.equalsIgnoreCase("walk") || verbName.equalsIgnoreCase("go to");
	const NeuroThing *verb = walk ? nullptr : findThing(world.verbs, verbName);
	if (!walk && !verb) {
		sendActionResult(id, false, Common::String::format("There is no verb '%s'. The verbs are: %s, %s.",
			verbName.c_str(), listNames(world.verbs).c_str(), kWalkVerbName));
		return;
	}

	NeuroThingList room = world.objects;
	room.push_back(world.actors);

	NeuroThingList everything = room;
	everything.push_back(world.inventory);
	everything.push_back(world.spots);

	int objects[2] = {0, 0};
	const Common::String *names[2] = {&targetName, &secondName};
	for (int i = 0; i < 2; i++) {
		if (names[i]->empty())
			continue;
		const NeuroThing *thing = findThing(everything, *names[i]);
		if (!thing) {
			sendActionResult(id, false, Common::String::format(
				"There is nothing called '%s' here. In the room: %s. In your inventory: %s.",
				names[i]->c_str(), listNames(room).c_str(), listNames(world.inventory).c_str()));
			return;
		}
		objects[i] = thing->id;
	}

	if (objects[0] < 0 || objects[1] < 0) {
		if (!walk || objects[1]) {
			sendActionResult(id, false, "That is just a place: all you can do there is \"Walk to\" it.");
			return;
		}
		const Common::Point &spot = world.spotPositions[-objects[0] - 1];
		sendActionResult(id, true);
		clickInRoom(spot.x, spot.y);
		return;
	}

	if (walk) {
		int x, y;
		if (!findThing(room, targetName) || !findClickPoint(objects[0], x, y)) {
			sendActionResult(id, false, Common::String::format("You can only walk to things in the room: %s.", listNames(room, world.spots).c_str()));
			return;
		}
		sendActionResult(id, true);
		clickInRoom(x, y);
		return;
	}

	sendActionResult(id, true);
	_lastTargets[0] = objects[0];
	_lastTargets[1] = objects[1];
	_vm->doSentence(verb->id, objects[0], objects[1]);
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
	_clickX = x;
	_clickY = y;
	_clickPhase = kClickHover;
	_clickFrames = 0;
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
	_vm->_virtualMouse.x = _clickX;
	_vm->_virtualMouse.y = _clickY;
	_vm->_mouse.x = CLIP<int>(_clickX - vs.xstart, 0, _vm->_screenWidth - 1);
	_vm->_mouse.y = CLIP<int>(_clickY + vs.topline, vs.topline, vs.topline + vs.h - 1);
}

} // End of namespace Scumm
