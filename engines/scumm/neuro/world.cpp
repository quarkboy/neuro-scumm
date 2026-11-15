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

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/system.h"

#include "scumm/actor.h"
#include "scumm/charset.h"
#include "scumm/object.h"
#include "scumm/resource.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

static const uint32 kWorldPollIntervalMs = 100;
static const uint32 kControlSettleMs = 300;

// In Monkey Island 1 and 2 this verb slot is not a verb but the sentence line
// ("Walk to door") above the verbs.
static const int kSentenceLineVerb = 100;

Common::String NeuroBridge::cleanText(const byte *text, bool resolveCodes) {
	if (!text)
		return Common::String();

	byte resolved[2048];
	if (resolveCodes) {
		_vm->convertMessageToString(text, resolved, sizeof(resolved));
		text = resolved;
	}

	Common::String raw;
	while (*text) {
		byte c = *text++;
		if (c == 0xFF || c == 0xFE) {
			byte code = *text;
			if (!code)
				break;
			text++;
			if (code == 1 || code == 3) {
				// Line break or new page
				raw += ' ';
			} else if (code != 2 && code != 8) {
				// Remaining codes carry a two byte argument
				for (int i = 0; i < 2 && *text; i++)
					text++;
			}
			continue;
		}
		// Names are padded with '@' to leave room for renaming
		if (c == '@' || c < 0x20)
			continue;
		if (c == '^') {
			// The game fonts draw this as an ellipsis
			raw += "...";
		} else if (c == 0xFA) {
			// Non-breaking space
			raw += ' ';
		} else {
			raw += (char)c;
		}
	}

	Common::String utf8 = _vm->_charset->convertText(raw, _vm->_language).encode();

	// Collapse runs of spaces left behind by line breaks
	Common::String result;
	for (uint i = 0; i < utf8.size(); i++) {
		if (utf8[i] == ' ' && (result.empty() || result.lastChar() == ' '))
			continue;
		result += utf8[i];
	}
	result.trim();
	return result;
}

bool NeuroBridge::playerHasControl() const {
	return _vm->_userPut > 0 && _vm->_cursor.state > 0;
}

/** Things sharing a name get numbered from left to right. */
static bool numberDuplicates(NeuroThingList &objects, NeuroThingList &actors) {
	bool numbered = false;
	// A character is often both an actor and a clickable object. Keep the
	// object, which is what a click finds first.
	for (uint i = 0; i < actors.size();) {
		bool duplicate = false;
		for (const auto &object : objects)
			duplicate |= object.name.equalsIgnoreCase(actors[i].name);
		if (duplicate)
			actors.remove_at(i);
		else
			i++;
	}

	Common::Array<NeuroThing *> all;
	for (auto &thing : objects)
		all.push_back(&thing);
	for (auto &thing : actors)
		all.push_back(&thing);
	Common::sort(all.begin(), all.end(), [](const NeuroThing *a, const NeuroThing *b) {
		return a->x < b->x;
	});

	for (uint i = 0; i < all.size(); i++) {
		const Common::String name = all[i]->name;
		int copies = 1;
		for (uint j = i + 1; j < all.size(); j++) {
			if (all[j]->name.equalsIgnoreCase(name))
				all[j]->name = Common::String::format("%s %d", name.c_str(), ++copies);
		}
		if (copies > 1)
			all[i]->name += " 1";
		numbered |= copies > 1;
	}
	return numbered;
}

void NeuroBridge::collectWorld(NeuroWorld &world) {
	world = NeuroWorld();
	world.room = _vm->_currentRoom;
	_data.setRoomResource(world.room, _vm->_roomResource);

	// Things that cannot be used right now still take part in numbering, so
	// that "door 2" stays "door 2" when door 1 becomes unusable.
	Common::Array<bool> usable;

	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr < 1 || od.width == 0 || od.height == 0)
			continue;
		// Scripts park what a screen does not use beyond the edge of the room
		if (od.x_pos >= _vm->_roomWidth || od.x_pos + od.width <= 0)
			continue;
		const bool available = !_vm->getClass(od.obj_nr, kObjectClassUntouchable) && _vm->whereIsObject(od.obj_nr) == WIO_ROOM;

		// Same test as findObject(): an object nested in another one only
		// exists while each parent is in the required state.
		bool reachable = false;
		int b = i;
		byte a;
		do {
			a = _vm->_objs[b].parentstate;
			b = _vm->_objs[b].parent;
			if (b == 0) {
				reachable = true;
				break;
			}
		} while ((_vm->_objs[b].state & 0xF) == a);

		NeuroThing thing;
		thing.id = od.obj_nr;
		thing.x = od.x_pos + od.width / 2;
		thing.state = od.state;
		if (!_data.objectName(world.room, od.obj_nr, thing.name))
			thing.name = cleanText(_vm->getObjOrActorName(od.obj_nr), true);
		thing.note = _data.objectNote(world.room, od.obj_nr);
		if (!thing.name.empty()) {
			world.objects.push_back(thing);
			usable.push_back(available && reachable);
		}
	}

	const int ego = _vm->VAR(_vm->VAR_EGO);
	for (int i = 1; i < _vm->_numActors; i++) {
		Actor *actor = _vm->_actors[i];
		if (i == ego || !actor->_visible || !actor->isInCurrentRoom())
			continue;
		if (_vm->getClass(i, kObjectClassUntouchable))
			continue;

		NeuroThing thing;
		thing.id = i;
		thing.x = actor->getPos().x;
		thing.name = actorName(i, true);
		if (!thing.name.empty())
			world.actors.push_back(thing);
	}

	for (int i = 0; i < _vm->_numInventory; i++) {
		const int obj = _vm->_inventory[i];
		if (!obj || _vm->getOwner(obj) != ego)
			continue;

		NeuroThing thing;
		thing.id = obj;
		thing.name = cleanText(_vm->getObjOrActorName(obj), true);
		if (!thing.name.empty())
			world.inventory.push_back(thing);
	}

	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (!vs.verbid || vs.verbid == kSentenceLineVerb || vs.saveid != 0 || vs.curmode != 1 || vs.type != kTextVerbType)
			continue;

		NeuroThing thing;
		thing.id = vs.verbid;
		// Where it is drawn tells a grid of verbs from a list of lines to say
		thing.x = vs.curRect.left;
		thing.state = vs.curRect.top;
		thing.name = cleanText(_vm->getResourceAddress(rtVerb, slot), true);
		if (!thing.name.empty())
			world.verbs.push_back(thing);
	}

	world.numbered = numberDuplicates(world.objects, world.actors);

	NeuroThingList usableObjects;
	for (uint i = 0; i < world.objects.size(); i++) {
		if (usable[i])
			usableObjects.push_back(world.objects[i]);
	}
	world.objects = usableObjects;
	Common::sort(world.objects.begin(), world.objects.end(), [](const NeuroThing &a, const NeuroThing &b) {
		return a.x < b.x;
	});

	const Common::Array<NeuroData::Spot> spots = _data.spots(world.room);
	for (uint i = 0; i < spots.size(); i++) {
		NeuroThing thing;
		thing.id = -(int)(i + 1);
		thing.x = spots[i].x;
		thing.name = spots[i].name;
		thing.note = spots[i].effect;
		world.spots.push_back(thing);
		world.spotPositions.push_back(Common::Point(spots[i].x, spots[i].y));
	}

	// Conversation lines replace the normal verbs while they are shown
	if (!world.verbs.empty() && !_standardVerbs.empty()) {
		world.inDialogue = true;
		for (const auto &verb : world.verbs) {
			for (const auto &standard : _standardVerbs)
				world.inDialogue &= (verb.id != standard.id);
		}
		// Lines to say are listed one to a row; verbs stand side by side
		for (uint i = 0; world.inDialogue && i < world.verbs.size(); i++) {
			for (uint j = i + 1; j < world.verbs.size(); j++)
				world.customVerbs |= world.verbs[i].state == world.verbs[j].state;
		}
		if (world.customVerbs)
			world.inDialogue = false;
	}
}

Common::String NeuroBridge::actorName(int actor, bool forList) {
	if (actor <= 0 || actor >= _vm->_numActors)
		return Common::String();

	Common::String name;
	if (_data.actorName(_vm->_currentRoom, actor, name)) {
		if (name.hasPrefix("-"))
			return forList ? Common::String() : name.substr(1);
		return name;
	}

	// Trust the game's name only while the actor still looks like who was named
	const int costume = _vm->_actors[actor]->_costume;
	if (!_namedCostume.contains(actor) || _namedCostume[actor] < 0) {
		if (costume)
			_namedCostume[actor] = costume;
	} else if (_namedCostume[actor] != costume) {
		return Common::String();
	}
	return cleanText(_vm->_actors[actor]->getActorName(), true);
}

void NeuroBridge::onActorNamed(int actor) {
	_namedCostume[actor] = -1;
}

void NeuroBridge::debugDump() {
	debug("NEURODUMP room %d (resource %d) width %d", _vm->_currentRoom, _vm->_roomResource, _vm->_roomWidth);
	NeuroWorld world;
	collectWorld(world);
	debug("NEURODUMP state: inDialogue %d verbs %u standard %u forceAge %u pending %u controlSince %u", world.inDialogue, world.verbs.size(),
		_standardVerbs.size(), g_system->getMillis() - _forceSentAt, _pendingDialogue.size(), _controlSince);
	debug("NEURODUMP state: control %d userPut %d cursor %d sentences %d speechOpen %d moving %d forceActive %d idle %u ms click %d queue %u",
		playerHasControl(), _vm->_userPut, _vm->_cursor.state, _vm->_sentenceNum, _speechOpen,
		_vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_moving, _forceActive, g_system->getMillis() - _lastActivity, (int)_clickPhase, _queue.size());
	debug("NEURODUMP verb script %d", _vm->VAR(_vm->VAR_VERB_SCRIPT));
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (!vs.verbid)
			continue;
		debug("NEURODUMP verbslot %d id %d '%s' mode %d saveid %d type %d rect %d,%d-%d,%d imgindex %d", slot, vs.verbid,
			cleanText(_vm->getResourceAddress(rtVerb, slot), true).c_str(), vs.curmode, vs.saveid, vs.type,
			vs.curRect.left, vs.curRect.top, vs.curRect.right, vs.curRect.bottom, vs.imgindex);
	}
	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr < 1)
			continue;
		debug("NEURODUMP object.%d '%s' at %d,%d %dx%d state %d untouchable %d", od.obj_nr,
			cleanText(_vm->getObjOrActorName(od.obj_nr), true).c_str(), od.x_pos, od.y_pos, od.width, od.height,
			od.state, _vm->getClass(od.obj_nr, kObjectClassUntouchable));
	}
	Common::String running;
	for (int i = 0; i < NUM_SCRIPT_SLOT; i++) {
		if (_vm->vm.slot[i].status != ssDead && _vm->vm.slot[i].where == WIO_GLOBAL)
			running += Common::String::format(" %d", _vm->vm.slot[i].number);
	}
	debug("NEURODUMP global scripts running:%s", running.c_str());
	for (int i = 1; i < _vm->_numActors; i++) {
		Actor *actor = _vm->_actors[i];
		if (!actor->isInCurrentRoom()) {
			if (actor->_room)
				debug("NEURODUMP actor.%d is in room %d", i, actor->_room);
			continue;
		}
		debug("NEURODUMP actor.%d '%s' at %d,%d costume %d visible %d untouchable %d", i,
			cleanText(actor->getActorName(), true).c_str(), actor->getPos().x, actor->getPos().y,
			actor->_costume, actor->_visible, _vm->getClass(i, kObjectClassUntouchable));
	}
}

/** Names with their notes, for telling her what is in a room. */
static Common::String joinDescribed(const NeuroThingList &things, const NeuroData &data) {
	if (things.empty())
		return "nothing";

	Common::String result;
	for (uint i = 0; i < things.size(); i++) {
		if (i)
			result += ", ";
		result += things[i].name;

		Common::String extra = data.stateName(things[i].name, things[i].state);
		if (!extra.empty() && !things[i].note.empty())
			extra += "; ";
		extra += things[i].note;
		if (!extra.empty())
			result += " (" + extra + ")";
	}
	return result;
}

static Common::String joinNames(const NeuroThingList &things) {
	if (things.empty())
		return "nothing";

	Common::String result;
	for (uint i = 0; i < things.size(); i++) {
		if (i)
			result += ", ";
		result += things[i].name;
	}
	return result;
}

Common::String NeuroBridge::describeRoom(const NeuroWorld &world, RoomDetail detail) const {
	Common::String name = _data.roomName(world.room);
	if (name.empty())
		name = Common::String::format("room %d", world.room);
	Common::String text = "## Location: " + name + "\n\n";

	// The prose is worth its length once; after that she only needs what is here
	const Common::String description = _data.roomDescription(world.room);
	if (detail == kRoomFirstVisit && !description.empty())
		text += description + "\n\n";

	text += "- Things: " + joinDescribed(world.objects, _data) + "\n";
	if (!world.actors.empty())
		text += "- People: " + joinNames(world.actors) + "\n";
	if (!world.spots.empty())
		text += "- Places: " + joinNames(world.spots) + "\n";
	// Inventory changes are announced as they happen
	if (detail == kRoomStatus)
		text += "- Inventory: " + joinNames(world.inventory) + "\n";
	return text;
}

/** Lists the things in 'to' that have no counterpart in 'from'. */
static NeuroThingList difference(const NeuroThingList &from, const NeuroThingList &to) {
	NeuroThingList result;
	for (const auto &thing : to) {
		bool found = false;
		for (const auto &other : from) {
			if (other.id == thing.id && other.name == thing.name) {
				found = true;
				break;
			}
		}
		if (!found)
			result.push_back(thing);
	}
	return result;
}

/** A name without the number that tells same-named things apart. */
static Common::String baseName(const Common::String &name) {
	Common::String base = name;
	while (!base.empty() && Common::isDigit(base.lastChar()))
		base.deleteLastChar();
	base.trim();
	return base;
}

Common::String NeuroBridge::lastSeenAt(int actor, const NeuroWorld &world) const {
	if (!_actorSeen.contains(actor))
		return Common::String();
	const Common::Point pos = _actorSeen[actor].pos;

	// Someone leaving walks up to the way out first, as she would
	Common::String nearest;
	int best = 30;
	for (const auto &thing : world.objects) {
		const int index = _vm->getObjectIndex(thing.id);
		if (index <= 0)
			continue;
		const ObjectData &od = _vm->_objs[index];
		const int dx = MAX(MAX<int>(od.x_pos - pos.x, pos.x - (od.x_pos + od.width)), 0);
		const int dy = MAX(MAX<int>(od.y_pos - pos.y, pos.y - (od.y_pos + od.height)), 0);
		const int walk = MAX(ABS(od.walk_x - pos.x), ABS(od.walk_y - pos.y));
		// Standing inside a thing's outline says less than standing where one walks to use it
		const int distance = MIN(walk, MAX(dx, dy) + 12);
		if (distance < best) {
			best = distance;
			nearest = thing.name;
		}
	}
	return nearest;
}

NeuroThingList NeuroBridge::departed(const NeuroWorld &world, const NeuroThingList *known) {
	// Not only those she was told about: someone can cross the screen and be
	// gone before the game hands control back
	NeuroThingList result;
	Common::Array<int> gone;
	_leftAt.clear();
	for (const auto &seen : _actorSeen) {
		const Actor *actor = _vm->_actors[seen._key];
		if (actor->_visible && actor->isInCurrentRoom())
			continue;
		gone.push_back(seen._key);
		bool wanted = !known;
		for (uint i = 0; known && i < known->size(); i++)
			wanted |= (*known)[i].id == seen._key;
		if (!wanted)
			continue;

		NeuroThing thing;
		thing.id = seen._key;
		thing.name = seen._value.name;
		Common::String key = thing.name;
		key.toLowercase();
		const Common::String exit = lastSeenAt(seen._key, world);
		_leftAt[key] = exit;
		if (!exit.empty())
			thing.name += " (last seen at: " + exit + ")";
		result.push_back(thing);
	}
	for (const int actor : gone)
		_actorSeen.erase(actor);
	return result;
}

Common::String NeuroBridge::describeChanges(const NeuroWorld &before, const NeuroWorld &after) {
	const NeuroThingList got = difference(before.inventory, after.inventory);
	const NeuroThingList lost = difference(after.inventory, before.inventory);
	const NeuroThingList appeared = difference(before.objects, after.objects);
	NeuroThingList gone = difference(after.objects, before.objects);

	// Picking something up makes it vanish from the room; saying so twice is noise
	for (uint i = 0; i < gone.size();) {
		bool pickedUp = false;
		for (const auto &item : got)
			pickedUp |= item.name.equalsIgnoreCase(baseName(gone[i].name));
		if (pickedUp)
			gone.remove_at(i);
		else
			i++;
	}

	struct Line {
		const char *label;
		NeuroThingList things;
	};
	const Line lines[] = {
		{"Got", got},
		{"Lost", lost},
		{"Appeared", appeared},
		{"Gone", gone},
		{"Arrived", difference(before.actors, after.actors)},
		{"Left", departed(after, &before.actors)}
	};
	const bool quiet = _data.quietComings(after.room);

	Common::String text;
	for (const auto &line : lines) {
		const bool comings = &line == &lines[4] || &line == &lines[5];
		if (!line.things.empty() && !(quiet && comings))
			text += Common::String::format("%s: %s\n", line.label, joinNames(line.things).c_str());
	}

	// A state change is only visible on screen (a door swinging open, a
	// lever moving), so it has to be spelled out.
	NeuroThingList changed;
	for (const auto &thing : after.objects) {
		for (const auto &old : before.objects) {
			// Doors that townspeople use and other background animation are noise
			const bool hers = (thing.id == _lastTargets[0] || thing.id == _lastTargets[1]);
			if (hers && old.id == thing.id && old.name == thing.name && old.state != thing.state) {
				changed.push_back(thing);
				// Once is enough: a thing that keeps animating afterwards is not news
				for (int &target : _lastTargets) {
					if (target == thing.id)
						target = 0;
				}
			}
		}
	}
	for (uint i = 0; i < changed.size();) {
		const Common::String label = _data.stateName(changed[i].name, changed[i].state);
		if (label.empty()) {
			i++;
			continue;
		}
		text += Common::String::format("Now %s: %s\n", label.c_str(), changed[i].name.c_str());
		if (label == "open")
			_openedDoor = changed[i].name;
		changed.remove_at(i);
	}
	if (!changed.empty())
		text += Common::String::format("Changed: %s\n", joinNames(changed).c_str());
	return text;
}

static bool containsName(const NeuroThingList &things, const Common::String &name) {
	for (const auto &thing : things) {
		Common::String candidate = thing.name;
		candidate.toLowercase();
		if (candidate.contains(name))
			return true;
	}
	return false;
}

/** Whether the situation an alert is about holds in the given world. */
static bool alertHolds(const NeuroData::Alert &alert, const NeuroWorld &world) {
	if (alert.kind == "arrived" || alert.kind == "left")
		return containsName(world.actors, alert.name) == (alert.kind == "arrived");
	if (alert.kind == "appeared" || alert.kind == "disappeared")
		return containsName(world.objects, alert.name) == (alert.kind == "appeared");
	if (alert.kind == "entered")
		return true;
	if (alert.kind == "gained" || alert.kind == "lost")
		return containsName(world.inventory, alert.name) == (alert.kind == "gained");
	return false;
}

/** Fills in {exit}; false if the alert needs it and nobody was seen leaving. */
static bool fillExit(Common::String &text, const Common::String &exit) {
	const size_t at = text.find("{exit}");
	if (at == Common::String::npos)
		return true;
	if (exit.empty())
		return false;
	text.replace(at, 6, exit);
	return true;
}

void NeuroBridge::fireAlert(const NeuroData::Alert &original, const NeuroWorld &world) {
	NeuroData::Alert alert = original;
	Common::String exit;
	for (const auto &left : _leftAt) {
		if (alert.kind == "left" && left._key.contains(alert.name))
			exit = left._value;
	}
	if (!fillExit(alert.say, exit) || !fillExit(alert.command, exit))
		return;

	// Only one force may be outstanding; otherwise say it as plainly as context allows
	if (!_forceActive && !world.inDialogue) {
		const bool question = !alert.command.empty();
		if (question) {
			_questionCommand = alert.command;
			_questionThen = alert.then;
			_questionAskedAt = g_system->getMillis();
			_questionHoldVar = alert.holdVar;
			if (_questionHoldVar)
				_questionHoldValue = _vm->readVar(_questionHoldVar);
		}
		sendForce(alert.say, alert.ask, question ? "answer" : "interact", alert.priority);
		_lastActivity = g_system->getMillis();
	} else {
		sendContext(alert.say + " " + alert.ask, false);
	}
}

void NeuroBridge::checkAlerts(const NeuroWorld &before, const NeuroWorld &after) {
	const uint32 now = g_system->getMillis();
	const Common::Array<NeuroData::Alert> &alerts = _data.alerts();

	// An alert waiting for its moment
	if (_pendingAlert >= 0) {
		const NeuroData::Alert &alert = alerts[_pendingAlert];
		if ((alert.room && alert.room != after.room) || !alertHolds(alert, after)) {
			_pendingAlert = -1;
		} else if (now > _pendingAlertDue + 1500) {
			// She was busy when the moment came; a late prompt would only mislead
			_pendingAlert = -1;
		} else if (now >= _pendingAlertDue) {
			_pendingAlert = -1;
			fireAlert(alert, after);
		}
		return;
	}

	for (uint i = 0; i < alerts.size(); i++) {
		const NeuroData::Alert &alert = alerts[i];
		if (alert.room && alert.room != after.room)
			continue;
		if (alert.whileScript && !_vm->isScriptRunning(alert.whileScript))
			continue;
		// It has to have just become true
		if (alert.kind == "entered") {
			if (before.room == after.room)
				continue;
		} else if (alert.kind == "left") {
			bool left = false;
			for (const auto &who : _leftAt)
				left |= who._key.contains(alert.name);
			if (!left)
				continue;
		} else if (before.room != after.room || alertHolds(alert, before) || !alertHolds(alert, after)) {
			continue;
		}

		if (alert.leadMs > _reactionMs) {
			_pendingAlert = i;
			_pendingAlertDue = now + alert.leadMs - _reactionMs;
		} else {
			fireAlert(alert, after);
		}
		return;
	}
}

Common::String NeuroBridge::describeWorld() {
	NeuroWorld world;
	collectWorld(world);
	Common::String text = describeRoom(world, kRoomFirstVisit);
	text += "- Inventory: " + joinNames(world.inventory) + "\n";
	text += "- Verbs on screen: " + joinNames(world.verbs) + "\n";
	text += Common::String::format("- Player has control: %s\n", playerHasControl() ? "yes" : "no");
	return text;
}

void NeuroBridge::pollWorld() {
	const uint32 now = g_system->getMillis();
	if (now < _nextWorldPoll)
		return;
	_nextWorldPoll = now + kWorldPollIntervalMs;

	// Where each character was last seen, to say which way one went
	if (_actorSeenRoom != _vm->_currentRoom) {
		_actorSeenRoom = _vm->_currentRoom;
		_actorSeen.clear();
	}
	const int ego = _vm->VAR(_vm->VAR_EGO);
	for (int i = 1; i < _vm->_numActors; i++) {
		const Actor *actor = _vm->_actors[i];
		if (i == ego || !actor->_visible || !actor->isInCurrentRoom() || _vm->getClass(i, kObjectClassUntouchable))
			continue;
		const Common::String name = actorName(i, true);
		if (!name.empty())
			_actorSeen[i] = {actor->getPos(), name};
	}

	// Between the lines of a scene, so that it reads in the order it happened
	checkWatches();

	// Only report once things have settled: mid-cutscene and mid-action
	// states are noise.
	if (!playerHasControl() || _vm->_sentenceNum > 0) {
		_controlSince = 0;
		_speechBlockSince = 0;
		_lastActivity = now;
		return;
	}
	// Wait for whoever is talking to finish, unless the talking never stops
	if (_speechOpen) {
		if (!_speechBlockSince)
			_speechBlockSince = now;
		if (now - _speechBlockSince < 4000) {
			_controlSince = 0;
			_lastActivity = now;
			return;
		}
	} else if (now - _lastTalkTime > 1500) {
		_speechBlockSince = 0;
	}
	// Still carrying out the last action
	if (_vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_moving)
		_lastActivity = now;
	if (!_controlSince)
		_controlSince = now;
	if (now - _controlSince < kControlSettleMs)
		return;

	flushDialogue();

	NeuroWorld world;
	collectWorld(world);

	Common::StringArray carried;
	for (const auto &item : world.inventory)
		carried.push_back(item.name);
	_journal.observed(world.room, carried);
	// Goals and nudges wait for a pause: the end of the conversation, or the
	// moment she is asked to pick her next line (the prompt for that follows
	// below), never between lines spoken one after another
	sendJournalNews();

	if (world.room != _described.room) {
		// The opening explanation has scrolled out of her context by now
		if (_tokensSent - _introSentAt > _contextTokens)
			sendIntro();

		// Screens that share one description (the forest) are told it once
		const int seenAs = _data.descriptionRoom(world.room);
		const bool remembered = _seenRooms.contains(seenAs) && _tokensSent - _seenRooms[seenAs] <= _contextTokens;
		if (!remembered)
			_seenRooms[seenAs] = _tokensSent;
		Common::String text = describeRoom(world, remembered ? kRoomRevisit : kRoomFirstVisit);
		const NeuroThingList left = departed(world);
		if (!left.empty())
			text += "- Just left: " + joinNames(left) + "\n";
		// What changed hands in a scene that ended somewhere else
		if (_described.room) {
			const NeuroThingList got = difference(_described.inventory, world.inventory);
			const NeuroThingList lost = difference(world.inventory, _described.inventory);
			if (!got.empty())
				text += "Got: " + joinNames(got) + "\n";
			if (!lost.empty())
				text += "Lost: " + joinNames(lost) + "\n";
		}
		sendContext(text, false);
		_outcomeTold = true;
	} else {
		Common::String changes = describeChanges(_described, world);
		// Her cue to act: someone coming or going, or the result of what she just did
		if (!changes.empty()) {
			sendContext(changes, !(_awaitingOutcome || changes.contains("Arrived:") || changes.contains("Left:")));
			_outcomeTold |= _awaitingOutcome;
		}
	}
	finishAction(world);
	checkAlerts(_described, world);

	// Opening a door is nearly always the first half of walking through it, and
	// townspeople using the same door shut it behind them
	if (!_openedDoor.empty()) {
		// Not when the opening was itself the first half of an answer
		if (_data.doorQuestion() && _queue.empty() && now - _lastQueuedStepAt > 5000 && !_forceActive && !world.inDialogue && world.room == _described.room) {
			_questionCommand = "Open | " + _openedDoor;
			_questionThen = "Walk to | " + _openedDoor;
			_questionAskedAt = now;
			_questionHoldVar = 0;
			sendForce("You opened: " + _openedDoor + ".", "Go through it?", "answer");
			_lastActivity = now;
		}
		_openedDoor.clear();
	}

	// The first verbs to appear are the game's normal ones
	if (_standardVerbs.empty() && !world.verbs.empty() && !world.customVerbs) {
		_standardVerbs = world.verbs;
		_offeredVerbs = _standardVerbs;
		registerActions(true);
	}
	// A scene can swap in verbs of its own
	if (!_standardVerbs.empty() && !world.inDialogue && !world.verbs.empty()) {
		const NeuroThingList &wanted = world.customVerbs ? world.verbs : _standardVerbs;
		if (joinNames(wanted) != joinNames(_offeredVerbs)) {
			_offeredVerbs = wanted;
			registerActions(true);
			if (world.customVerbs)
				sendContext("The verbs on screen have been replaced. They are now: " + joinNames(wanted) + ". Use them with interact, like any verb.", false);
			else
				sendContext("The usual verbs are back on screen.", true);
		}
	}

	updateForces(world);

	_leftAt.clear();
	_described = world;
}

void NeuroBridge::checkWatches() {
	Common::String text;
	for (const auto &watch : _data.watches()) {
		const int value = _vm->readVar(watch.var);
		// The first look only sets the starting point
		if (_watched.contains(watch.var) && value > _watched[watch.var]) {
			Common::String say = watch.say;
			const size_t at = say.find("{value}");
			if (at != Common::String::npos)
				say.replace(at, 7, Common::String::format("%d", value));
			text += say + "\n";
		}
		_watched[watch.var] = value;
	}
	if (text.empty())
		return;
	if (_dialogueAsContext) {
		if (_pendingDialogue.empty())
			_pendingSince = g_system->getMillis();
		_pendingDialogue += text;
		_lastLineText.clear();
	} else {
		sendContext(text, true);
	}
}

void NeuroBridge::finishAction(const NeuroWorld &world) {
	if (!_awaitingOutcome)
		return;

	// Still going: a click not yet made, steps queued, walking, the game's own script
	// (what a click sets off takes a moment to show)
	const bool busy = _clickPhase != kClickIdle || !_queue.empty() || !_clickQueue.empty() || _pagerState != kPagerIdle
		|| g_system->getMillis() - _lastClickAt < 1000
		|| _vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_moving
		|| _vm->isScriptRunning(_vm->VAR(_vm->VAR_SENTENCE_SCRIPT));
	if (busy || g_system->getMillis() - _commandStartedAt < 600)
		return;
	// After someone has spoken, lines to choose from may be about to appear;
	// they bring their own prompt, so give them a moment first.
	if (_outcomeSpoken && !world.inDialogue && g_system->getMillis() - _controlSince < 2500)
		return;

	_awaitingOutcome = false;
	// A conversation prompts her with the lines to choose from. Otherwise she
	// needs a cue that is hers: the replies to her action were not sent as one.
	if (!_outcomeTold && !world.inDialogue)
		sendContext(_outcomeSpoken ? "Your move." : "Done.", false);
}

void NeuroBridge::onTalkStarted(int actor, const byte *text) {
	_outcomeSpoken = true;

	onTalkStopped();

	Common::String line = cleanText(text, false);
	if (line.empty() || !_ws.isConnected())
		return;

	Common::String speaker = actorName(actor);
	if (_strangerRoom != _vm->_currentRoom) {
		_strangerRoom = _vm->_currentRoom;
		_strangers.clear();
		_voiceColors.clear();
	}
	if (speaker.empty()) {
		if (actor == _vm->VAR(_vm->VAR_EGO)) {
			speaker = "Guybrush";
		} else if (actor > 0 && actor < 0x80) {
			// Keep unnamed speakers apart so a conversation can be followed
			uint index = 0;
			while (index < _strangers.size() && _strangers[index] != actor)
				index++;
			if (index == _strangers.size()) {
				_strangers.push_back(actor);
				debug(1, "Neuro: Unnamed speaker: actor %d in room %d", actor, _vm->_currentRoom);
			}
			speaker = Common::String::format("Stranger %u", index + 1);
		} else {
			speaker = _data.voiceName(_vm->_currentRoom, _vm->_charsetColor);
			if (speaker == "-")
				return;
			if (speaker.empty()) {
				speaker = "Narrator";
				debug(1, "Neuro: Unattributed line in room %d, text colour %d: %s", _vm->_currentRoom, _vm->_charsetColor, line.c_str());
			}
		}
	}

	// A reviewer's word on this very line beats everything worked out above
	Common::String chosen;
	const bool overridden = _data.lineSpeaker(line, chosen);
	if (overridden) {
		if (chosen == "-")
			return;
		if (!chosen.empty())
			speaker = chosen;
	}

	// A group that is one actor (the three pirate leaders) speaks in a
	// different text colour per member. Number them once a second colour shows up.
	const Common::String voice = (actor > 0 && actor < 0x80) ? _data.voiceName(_vm->_currentRoom, _vm->_charsetColor, actor) : Common::String();
	if (!overridden && !voice.empty()) {
		speaker = voice;
	} else if (!overridden && actor > 0 && actor < 0x80 && actor != _vm->VAR(_vm->VAR_EGO)) {
		Common::Array<byte> &colors = _voiceColors[speaker];
		const byte color = _vm->_charsetColor;
		uint index = 0;
		while (index < colors.size() && colors[index] != color)
			index++;
		if (index == colors.size()) {
			colors.push_back(color);
			debug(1, "Neuro: Voice %u of '%s' (actor %d, room %d) uses text colour %d", index + 1, speaker.c_str(), actor, _vm->_currentRoom, color);
		}
		if (colors.size() > 1)
			speaker += Common::String::format(" %u", index + 1);
	}

	// Any news waits until the conversation is over (see pollWorld)
	_journal.heard(line);
	if (actor != _vm->VAR(_vm->VAR_EGO)) {
		_lastHeard = speaker + ": \"" + line + "\"";
		_lastHeardAt = g_system->getMillis();
	}

	// Crowds shout the same line once per member; report it once
	const Common::String spoken = speaker + ": " + line;
	if (spoken == _lastLine && g_system->getMillis() - _lastTalkTime < 5000)
		return;
	_lastLine = spoken;

	// A line she has heard recently that is not a reaction to something she did is background noise
	const uint32 now = g_system->getMillis();
	bool heard = false;
	for (uint i = 0; i < _recentLines.size();) {
		if (now - _recentLines[i].time > 60000) {
			_recentLines.remove_at(i);
			continue;
		}
		if (_recentLines[i].text == spoken) {
			heard = true;
			_recentLines[i].time = now;
		}
		i++;
	}
	if (!heard)
		_recentLines.push_back({spoken, now});
	else if (now - _lastActionAt > 10000)
		return;

	_speechOpen = true;
	if (_dialogueAsContext) {
		// Text that is typed out a bit at a time ("Ps", "Pss", "Psssst.") is one line
		if (!_lastLineText.empty() && line.hasPrefix(_lastLineText) && _lastLineOffset <= _pendingDialogue.size()
				&& _pendingDialogue.substr(_lastLineOffset).hasPrefix(speaker + ": \"" + _lastLineText))
			_pendingDialogue = _pendingDialogue.substr(0, _lastLineOffset);
		_lastLineText = line;
		_lastLineOffset = _pendingDialogue.size();
		if (_pendingDialogue.empty())
			_pendingSince = g_system->getMillis();
		_pendingDialogue += Common::String::format("%s: \"%s\"\n", speaker.c_str(), line.c_str());
		return;
	}

	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(Common::String::format("%s-%u", _speechIdPrefix.c_str(), ++_speechCounter)));
	data.setVal("speaker", new Common::JSONValue(speaker));
	data.setVal("text", new Common::JSONValue(line));
	// Lines she is only watching may get a reaction. Replies to her own action
	// must not prompt her one by one: she is prompted once, when it is her
	// turn, with the choice of lines or a cue.
	if (_awaitingOutcome)
		data.setVal("reply", new Common::JSONValue("never"));
	// Only a safety net in case the end message is lost: generous reading time
	data.setVal("expected_duration_ms", new Common::JSONValue((long long int)(3000 + line.size() * 150)));
	send("speech/started", &data);
}

void NeuroBridge::onTextDrawn(const byte *text, int color) {
	const Common::String line = cleanText(text, true);
	if (line.empty() || !_ws.isConnected())
		return;
	Common::String source = _data.voiceName(_vm->_currentRoom, color);
	if (source == "-")
		return;
	if (source.empty())
		source = "Text on the screen";
	_journal.heard(line);
	_drawnText += source + ": \"" + line + "\"\n";
}

void NeuroBridge::flushDrawnText() {
	if (_drawnText.empty())
		return;
	// A screen that redraws its text every frame says it once
	if (_drawnText != _lastDrawnText)
		sendContext(_drawnText, true);
	_lastDrawnText = _drawnText;
	_drawnText.clear();
}

void NeuroBridge::onTalkStopped() {
	if (!_speechOpen)
		return;
	_speechOpen = false;
	_lastTalkTime = g_system->getMillis();
	if (_dialogueAsContext)
		return;

	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(Common::String::format("%s-%u", _speechIdPrefix.c_str(), _speechCounter)));
	send("speech/ended", &data);
}

} // End of namespace Scumm
