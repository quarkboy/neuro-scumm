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
static void numberDuplicates(NeuroThingList &objects, NeuroThingList &actors) {
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
				all[j]->name = Common::String::format("%s #%d", name.c_str(), ++copies);
		}
		if (copies > 1)
			all[i]->name += " #1";
	}
}

void NeuroBridge::collectWorld(NeuroWorld &world) {
	world = NeuroWorld();
	world.room = _vm->_currentRoom;

	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr < 1 || _vm->getClass(od.obj_nr, kObjectClassUntouchable))
			continue;
		if (_vm->whereIsObject(od.obj_nr) != WIO_ROOM)
			continue;

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
		if (!reachable)
			continue;

		NeuroThing thing;
		thing.id = od.obj_nr;
		thing.x = od.x_pos + od.width / 2;
		thing.state = od.state;
		thing.name = cleanText(_vm->getObjOrActorName(od.obj_nr), true);
		if (!thing.name.empty())
			world.objects.push_back(thing);
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
		thing.name = cleanText(actor->getActorName(), true);
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
		thing.name = cleanText(_vm->getResourceAddress(rtVerb, slot), true);
		if (!thing.name.empty())
			world.verbs.push_back(thing);
	}

	numberDuplicates(world.objects, world.actors);

	// Conversation lines replace the normal verbs while they are shown
	if (!world.verbs.empty() && !_standardVerbs.empty()) {
		world.inDialogue = true;
		for (const auto &verb : world.verbs) {
			for (const auto &standard : _standardVerbs)
				world.inDialogue &= (verb.id != standard.id);
		}
	}
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

Common::String NeuroBridge::describeRoom(const NeuroWorld &world) const {
	Common::String text = Common::String::format("## Location: room %d\n\n", world.room);
	text += "- Things here: " + joinNames(world.objects) + "\n";
	if (!world.actors.empty())
		text += "- Characters here: " + joinNames(world.actors) + "\n";
	text += "- You are carrying: " + joinNames(world.inventory) + "\n";
	if (text.contains(" #2"))
		text += "- Things with the same name are numbered from left to right.\n";
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

Common::String NeuroBridge::describeChanges(const NeuroWorld &before, const NeuroWorld &after) const {
	struct Category {
		const NeuroThingList &before;
		const NeuroThingList &after;
		const char *added;
		const char *removed;
	};
	const Category categories[] = {
		{before.inventory, after.inventory, "You now have: %s\n", "You no longer have: %s\n"},
		{before.objects, after.objects, "Now here: %s\n", "No longer here: %s\n"},
		{before.actors, after.actors, "Arrived: %s\n", "Left: %s\n"}
	};

	Common::String text;
	for (const auto &category : categories) {
		NeuroThingList added = difference(category.before, category.after);
		NeuroThingList removed = difference(category.after, category.before);
		if (!added.empty())
			text += Common::String::format(category.added, joinNames(added).c_str());
		if (!removed.empty())
			text += Common::String::format(category.removed, joinNames(removed).c_str());
	}

	// A state change is only visible on screen (a door swinging open, a
	// lever moving), so it has to be spelled out.
	NeuroThingList changed;
	for (const auto &thing : after.objects) {
		for (const auto &old : before.objects) {
			// Doors that townspeople use and other background animation are noise
			const bool hers = (thing.id == _lastTargets[0] || thing.id == _lastTargets[1]);
			if (hers && old.id == thing.id && old.name == thing.name && old.state != thing.state)
				changed.push_back(thing);
		}
	}
	if (!changed.empty())
		text += Common::String::format("Changed in appearance: %s\n", joinNames(changed).c_str());
	return text;
}

Common::String NeuroBridge::describeWorld() {
	NeuroWorld world;
	collectWorld(world);
	Common::String text = describeRoom(world);
	text += "- Verbs on screen: " + joinNames(world.verbs) + "\n";
	text += Common::String::format("- Player has control: %s\n", playerHasControl() ? "yes" : "no");
	return text;
}

void NeuroBridge::pollWorld() {
	const uint32 now = g_system->getMillis();
	if (now < _nextWorldPoll)
		return;
	_nextWorldPoll = now + kWorldPollIntervalMs;

	// Only report once things have settled: mid-cutscene and mid-action
	// states are noise.
	if (!playerHasControl() || _speechOpen || _vm->_sentenceNum > 0) {
		_controlSince = 0;
		_lastActivity = now;
		return;
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

	if (world.room != _described.room) {
		sendContext(describeRoom(world), false);
	} else {
		Common::String changes = describeChanges(_described, world);
		if (!changes.empty())
			sendContext(changes, true);
	}

	// The first verbs to appear are the game's normal ones
	if (_standardVerbs.empty() && !world.verbs.empty()) {
		_standardVerbs = world.verbs;
		registerActions();
	}

	updateForces(world);

	_described = world;
}

void NeuroBridge::onTalkStarted(int actor, const byte *text) {
	onTalkStopped();

	Common::String line = cleanText(text, false);
	if (line.empty() || !_ws.isConnected())
		return;

	Common::String speaker;
	if (actor > 0 && actor < _vm->_numActors)
		speaker = cleanText(_vm->_actors[actor]->getActorName(), true);
	if (speaker.empty()) {
		if (actor == _vm->VAR(_vm->VAR_EGO))
			speaker = "Guybrush";
		else if (actor > 0 && actor < 0x80)
			speaker = "Someone";
		else
			speaker = "Narrator";
	}

	// Crowds shout the same line once per member; report it once
	const Common::String spoken = speaker + ": " + line;
	if (spoken == _lastLine && g_system->getMillis() - _lastTalkTime < 5000)
		return;
	_lastLine = spoken;

	_speechOpen = true;
	if (_dialogueAsContext) {
		// Text that is typed out a bit at a time ("Ps", "Pss", "Psssst.") is one line
		if (!_lastLineText.empty() && line.hasPrefix(_lastLineText) && _lastLineOffset <= _pendingDialogue.size()
				&& _pendingDialogue.substr(_lastLineOffset).hasPrefix(speaker + ": \"" + _lastLineText))
			_pendingDialogue = _pendingDialogue.substr(0, _lastLineOffset);
		_lastLineText = line;
		_lastLineOffset = _pendingDialogue.size();
		_pendingDialogue += Common::String::format("%s: \"%s\"\n", speaker.c_str(), line.c_str());
		return;
	}

	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(Common::String::format("%s-%u", _speechIdPrefix.c_str(), ++_speechCounter)));
	data.setVal("speaker", new Common::JSONValue(speaker));
	data.setVal("text", new Common::JSONValue(line));
	send("speech/started", &data);
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
