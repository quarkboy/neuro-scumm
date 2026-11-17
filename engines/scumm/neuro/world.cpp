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

// What is in the room and in her hands, and telling her when it changes.

#include "scumm/neuro/neuro.h"

#include "common/algorithm.h"
#include "common/system.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/resource.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

static const uint32 kWorldPollIntervalMs = 100;
/** The player has to have had control for this long before the world is looked at. */
static const uint32 kControlSettleMs = 300;
/** Talk that goes on longer than this while the player has control no longer holds reports back. */
static const uint32 kEndlessTalkMs = 4000;
/** No line for this long means the talking has stopped. */
static const uint32 kTalkOverMs = 1500;
/** A door opened this soon after a follow-up step was part of that step, not something to ask about. */
static const uint32 kDoorQuestionQuietMs = 5000;
/** How near (in pixels) a character must have been to a thing to count as having left by it. */
static const int kLeftByDistance = 30;



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
				all[j]->name = Common::String::format("%s %d", name.c_str(), ++copies);
		}
		if (copies > 1)
			all[i]->name += " 1";
	}
}

void NeuroBridge::collectWorld(NeuroWorld &world) {
	world = NeuroWorld();
	world.room = _vm->_currentRoom;
	_data.setRoomResource(world.room, _vm->_roomResource);
	if (ego()->isInCurrentRoom())
		world.standing = _data.standing(world.room, ego()->_walkbox);

	// Things that cannot be used right now still take part in numbering, so
	// that "door 2" stays "door 2" when door 1 becomes unusable.
	Common::Array<bool> usable;
	collectObjects(world, usable);
	collectActors(world);
	collectInventory(world);
	collectVerbs(world);

	numberDuplicates(world.objects, world.actors);

	NeuroThingList usableObjects;
	for (uint i = 0; i < world.objects.size(); i++) {
		if (usable[i])
			usableObjects.push_back(world.objects[i]);
	}
	world.objects = usableObjects;
	Common::sort(world.objects.begin(), world.objects.end(), [](const NeuroThing &a, const NeuroThing &b) {
		return a.x < b.x;
	});

	collectSpots(world);
	noteLines(world);
}

void NeuroBridge::collectObjects(NeuroWorld &world, Common::Array<bool> &usable) {
	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr < 1 || od.width == 0 || od.height == 0)
			continue;
		// Scripts park what a screen does not use beyond the edge of the room
		if (od.x_pos >= _vm->_roomWidth || od.x_pos + od.width <= 0)
			continue;
		const bool available = !untouchable(od.obj_nr) && _vm->whereIsObject(od.obj_nr) == WIO_ROOM;

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
		const bool renamed = _data.objectName(world.room, od.obj_nr, thing.name);
		if (!renamed)
			thing.name = cleanText(_vm->getObjOrActorName(od.obj_nr), true);
		thing.note = _data.objectNote(world.room, od.obj_nr);
		if (_data.relativePlaces(world.room)) {
			// Left or right is all the game's own pointer tells apart in such a room
			const Common::String where = "to " + egoName() + (thing.x < ego()->getPos().x ? "'s left" : "'s right");
			thing.note = thing.note.empty() ? where : thing.note + "; " + where;
		}
		// Several objects given one name and note are one thing to her;
		// whichever can be used stands for all
		bool merged = false;
		for (uint j = 0; renamed && j < world.objects.size(); j++) {
			if (world.objects[j].name != thing.name || world.objects[j].note != thing.note)
				continue;
			merged = true;
			if (!usable[j] && available && reachable) {
				world.objects[j] = thing;
				usable[j] = true;
			}
		}
		if (merged)
			continue;
		if (!thing.name.empty()) {
			world.objects.push_back(thing);
			usable.push_back(available && reachable);
		}
	}
}

void NeuroBridge::collectActors(NeuroWorld &world) {
	for (int i = 1; i < _vm->_numActors; i++) {
		Actor *actor = _vm->_actors[i];
		if (actor == ego() || !actor->_visible || !actor->isInCurrentRoom())
			continue;
		if (untouchable(i))
			continue;

		NeuroThing thing;
		thing.id = i;
		thing.x = actor->getPos().x;
		thing.name = actorName(i, true);
		if (!thing.name.empty())
			world.actors.push_back(thing);
	}
}

void NeuroBridge::collectInventory(NeuroWorld &world) {
	for (int i = 0; i < _vm->_numInventory; i++) {
		const int obj = _vm->_inventory[i];
		if (!obj || _vm->getOwner(obj) != _vm->VAR(_vm->VAR_EGO))
			continue;

		NeuroThing thing;
		thing.id = obj;
		thing.name = cleanText(_vm->getObjOrActorName(obj), true);
		if (!thing.name.empty())
			world.inventory.push_back(thing);
	}
}

/** Whether any two of the verbs stand on the same row: verbs form a grid, lines to say are listed one to a row. */
static bool inRows(const NeuroThingList &verbs) {
	for (uint i = 0; i < verbs.size(); i++) {
		for (uint j = i + 1; j < verbs.size(); j++) {
			if (verbs[i].y == verbs[j].y)
				return true;
		}
	}
	return false;
}

void NeuroBridge::collectVerbs(NeuroWorld &world) {
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		// The sentence line above the verbs can be a verb slot too
		if (!vs.verbid || vs.verbid == _data.game().sentenceLineVerb || vs.saveid != 0 || vs.curmode != 1)
			continue;

		NeuroThing thing;
		thing.id = vs.verbid;
		thing.x = vs.curRect.left;
		thing.y = vs.curRect.top;
		// A verb drawn as a picture has no text; the description file can name it
		if (vs.type == kTextVerbType)
			thing.name = cleanText(_vm->getResourceAddress(rtVerb, slot), true);
		else
			thing.name = _data.verbName(vs.verbid);
		if (!thing.name.empty())
			world.verbs.push_back(thing);
	}
	if (world.verbs.empty())
		return;

	// Conversation lines replace the normal verbs while they are shown
	if (_standardVerbs.empty()) {
		// Loaded in mid-conversation, before the normal verbs were ever seen
		world.inDialogue = !inRows(world.verbs);
		return;
	}
	world.inDialogue = true;
	for (const auto &verb : world.verbs) {
		for (const auto &standard : _standardVerbs)
			world.inDialogue &= (verb.id != standard.id);
	}
	// Other verbs than the normal ones, but verbs all the same
	if (world.inDialogue && inRows(world.verbs)) {
		world.customVerbs = true;
		world.inDialogue = false;
	}
}

void NeuroBridge::collectSpots(NeuroWorld &world) {
	const Common::Array<NeuroData::Spot> &spots = _data.spots(world.room);
	for (uint i = 0; i < spots.size(); i++) {
		NeuroThing thing;
		thing.id = -(int)(i + 1);
		thing.x = spots[i].x;
		thing.name = spots[i].name;
		thing.note = spots[i].effect;
		world.spots.push_back(thing);
		world.spotPositions.push_back(Common::Point(spots[i].x, spots[i].y));
		world.spotTwice.push_back(spots[i].twice);
	}

	// A close-up of something she is reading has nothing in it; a click anywhere puts it away
	if (world.objects.empty() && world.actors.empty() && world.spots.empty()) {
		NeuroThing thing;
		thing.id = -1;
		thing.x = _vm->_roomWidth / 2;
		thing.name = "away (stop looking at this)";
		world.spots.push_back(thing);
		world.spotPositions.push_back(Common::Point(_vm->_roomWidth / 2, _vm->_roomHeight / 2));
		world.spotTwice.push_back(false);
	}
}

/** What she would remember about a line she can say, from what the game keeps about it. */
void NeuroBridge::noteLines(NeuroWorld &world) {
	for (const auto &lineNote : _data.lineNotes()) {
		if (!world.inDialogue || !_vm->isScriptRunning(lineNote.script))
			continue;
		for (auto &line : world.verbs) {
			const int slot = line.id - lineNote.firstVerb;
			if (slot < 0 || slot > 16)
				continue;
			const int entry = readDataVar(lineNote.slotVar + slot);
			if (entry >= lineNote.min && entry <= lineNote.max && (!lineNote.unlessBit || !readDataVar(0x8000 | (lineNote.unlessBit + entry))))
				line.note = lineNote.note;
		}
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

Common::String neuroListNames(const NeuroThingList &a, const NeuroThingList &b) {
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

Common::String NeuroBridge::describeRoom(const NeuroWorld &world, RoomDetail detail) const {
	Common::String name = _data.roomName(world.room);
	if (name.empty())
		name = Common::String::format("room %d", world.room);
	Common::String text = "## Location: " + name + "\n\n";

	// The prose is worth its length once; after that she only needs what is here
	const Common::String description = _data.roomDescription(world.room);
	if (detail == kRoomFirstVisit && !description.empty())
		text += description + "\n\n";

	if (!world.standing.empty())
		text += world.standing + "\n\n";
	text += "- Things: " + joinDescribed(world.objects, _data) + "\n";
	if (!world.actors.empty())
		text += "- People: " + neuroListNames(world.actors) + "\n";
	if (!world.spots.empty())
		text += "- Places: " + neuroListNames(world.spots) + "\n";
	// Inventory changes are announced as they happen
	if (detail == kRoomStatus)
		text += "- Inventory: " + neuroListNames(world.inventory) + "\n";
	return text;
}

/** Lists the things in 'to' that have no counterpart in 'from'. */
static NeuroThingList difference(const NeuroThingList &from, const NeuroThingList &to, bool byName = false) {
	NeuroThingList result;
	for (const auto &thing : to) {
		bool found = false;
		for (const auto &other : from) {
			// In a room, what replaces a thing under the same name is the same thing
			// to her; of several things carried under one name, each is its own
			if (other.name == thing.name && (byName || other.id == thing.id)) {
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
	int best = kLeftByDistance;
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

Common::String NeuroBridge::describeChanges(const NeuroWorld &before, const NeuroWorld &after, bool &layoutChanged, bool &comings) {
	const NeuroThingList got = difference(before.inventory, after.inventory);
	const NeuroThingList lost = difference(after.inventory, before.inventory);
	const NeuroThingList appeared = difference(before.objects, after.objects, true);
	NeuroThingList gone = difference(after.objects, before.objects, true);

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
	layoutChanged = !appeared.empty() || !gone.empty();
	comings = !quiet && (!lines[4].things.empty() || !lines[5].things.empty());

	Common::String text;
	for (const auto &line : lines) {
		const bool coming = &line == &lines[4] || &line == &lines[5];
		if (!line.things.empty() && !(quiet && coming))
			text += Common::String::format("%s: %s\n", line.label, neuroListNames(line.things).c_str());
	}

	// Getting to a part of the room that is cut off from the rest
	if (!after.standing.empty() && after.standing != before.standing)
		text += after.standing + "\n";

	// A state change is only visible on screen (a door swinging open, a
	// lever moving), so it has to be spelled out.
	NeuroThingList changed;
	for (const auto &thing : after.objects) {
		for (const auto &old : before.objects) {
			// Things that move by themselves in the background are noise
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
		if (label == (_data.game().doorOpenState.empty() ? Common::String("open") : _data.game().doorOpenState))
			_openedDoor = changed[i].name;
		changed.remove_at(i);
	}
	if (!changed.empty())
		text += Common::String::format("Changed: %s\n", neuroListNames(changed).c_str());
	return text;
}

Common::String NeuroBridge::describeWorld() {
	NeuroWorld world;
	collectWorld(world);
	Common::String text = describeRoom(world, kRoomFirstVisit);
	text += "- Inventory: " + neuroListNames(world.inventory) + "\n";
	text += "- Verbs on screen: " + neuroListNames(world.verbs) + "\n";
	text += Common::String::format("- Player has control: %s\n", playerHasControl() ? "yes" : "no");
	return text;
}

void NeuroBridge::pollWorld() {
	const uint32 now = g_system->getMillis();
	if (now < _nextWorldPoll)
		return;
	_nextWorldPoll = now + kWorldPollIntervalMs;

	trackActors();
	// Between the lines of a scene, so that it reads in the order it happened
	checkWatches();
	if (!worldSettled(now))
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

	// Some games let the player switch between several characters
	const int egoNow = _vm->VAR(_vm->VAR_EGO);
	if (_egoTold && egoNow != _egoTold) {
		sendContext("You are now playing " + egoName() + ".", false);
		// Each carries their own things; that is not something gained or lost
		_described.inventory = world.inventory;
	}
	_egoTold = egoNow;

	if (world.room != _described.room)
		reportRoom(world);
	else
		reportChanges(world);
	finishAction(world);
	checkAlerts(_described, world);
	askAboutDoor(world, now);
	learnVerbs(world);
	updateForces(world);

	_leftAt.clear();
	_described = world;
}

/** Notes where each character is, to say later which way one went. */
void NeuroBridge::trackActors() {
	if (_actorSeenRoom != _vm->_currentRoom) {
		_actorSeenRoom = _vm->_currentRoom;
		_actorSeen.clear();
		// Coming back to a room shows its text again
		_lastDrawnText.clear();
	}
	for (int i = 1; i < _vm->_numActors; i++) {
		const Actor *actor = _vm->_actors[i];
		if (actor == ego() || !actor->_visible || !actor->isInCurrentRoom() || untouchable(i))
			continue;
		const Common::String name = actorName(i, true);
		if (!name.empty())
			_actorSeen[i] = {actor->getPos(), name};
	}
}

/** Whether things have settled enough to report: mid-cutscene and mid-action states are noise. */
bool NeuroBridge::worldSettled(uint32 now) {
	if (!playerHasControl() || _vm->_sentenceNum > 0) {
		_controlSince = 0;
		_speechBlockSince = 0;
		_lastActivity = now;
		return false;
	}
	// Wait for whoever is talking to finish, unless the talking never stops
	if (_speechOpen) {
		if (!_speechBlockSince)
			_speechBlockSince = now;
		if (now - _speechBlockSince < kEndlessTalkMs) {
			_controlSince = 0;
			_lastActivity = now;
			return false;
		}
	} else if (now - _lastTalkTime > kTalkOverMs) {
		_speechBlockSince = 0;
	}
	// Still carrying out the last action
	if (ego()->_moving)
		_lastActivity = now;
	if (!_controlSince)
		_controlSince = now;
	return now - _controlSince >= kControlSettleMs;
}

void NeuroBridge::reportRoom(const NeuroWorld &world) {
	// The opening explanation has scrolled out of her context by now
	if (_tokensSent - _introSentAt > _contextTokens)
		sendIntro();

	// Rooms that share one description are told it once
	const int seenAs = _data.descriptionRoom(world.room);
	const bool remembered = _seenRooms.contains(seenAs) && _tokensSent - _seenRooms[seenAs] <= _contextTokens;
	if (!remembered)
		_seenRooms[seenAs] = _tokensSent;
	Common::String text = describeRoom(world, remembered ? kRoomRevisit : kRoomFirstVisit);
	const NeuroThingList left = departed(world);
	if (!left.empty())
		text += "- Just left: " + neuroListNames(left) + "\n";
	// What changed hands in a scene that ended somewhere else
	if (_described.room) {
		const NeuroThingList got = difference(_described.inventory, world.inventory);
		const NeuroThingList lost = difference(world.inventory, _described.inventory);
		if (!got.empty())
			text += "Got: " + neuroListNames(got) + "\n";
		if (!lost.empty())
			text += "Lost: " + neuroListNames(lost) + "\n";
	}
	sendContext(text, false);
	_interaction.outcome.report();
}

void NeuroBridge::reportChanges(const NeuroWorld &world) {
	bool layoutChanged, comings;
	Common::String changes = describeChanges(_described, world, layoutChanged, comings);
	// A room redrawn as somewhere else is told like a new room
	if (_data.shiftingLayout(world.room) && layoutChanged) {
		changes = "The place has changed around " + egoName() + ".\n" + describeRoom(world, kRoomRevisit);
		comings = false;
	}
	if (changes.empty())
		return;
	// Her cue to act: someone coming or going, or the result of what she just did
	sendContext(changes, !(_interaction.outcome.pending() || comings));
	_interaction.outcome.report();
}

/** Opening a door is nearly always the first half of walking through it. */
void NeuroBridge::askAboutDoor(const NeuroWorld &world, uint32 now) {
	if (_openedDoor.empty())
		return;
	// Not when the opening was itself the first half of an answer
	const NeuroData::Game &game = _data.game();
	if (game.doorQuestion && !game.openVerb.empty() && _interaction.queue.empty() && now - _lastQueuedStepAt > kDoorQuestionQuietMs && !_interaction.prompt.active() && !world.inDialogue && world.room == _described.room)
		ask("You opened: " + _openedDoor + ".", "Go through it?", game.openVerb + " | " + _openedDoor, _walkVerb + " | " + _openedDoor);
	_openedDoor.clear();
}

/** Keeps the verbs her interact action offers in step with the ones on screen. */
void NeuroBridge::learnVerbs(const NeuroWorld &world) {
	// The first verbs to appear in a grid are the game's normal ones
	if (_standardVerbs.empty() && inRows(world.verbs) && !world.customVerbs) {
		_standardVerbs = world.verbs;
		_offeredVerbs = _standardVerbs;
		registerActions("interact");
	}
	// A scene can swap in verbs of its own
	if (!_standardVerbs.empty() && !world.inDialogue && !world.verbs.empty()) {
		const NeuroThingList &wanted = world.customVerbs ? world.verbs : _standardVerbs;
		if (neuroListNames(wanted) != neuroListNames(_offeredVerbs)) {
			_offeredVerbs = wanted;
			registerActions("interact");
			if (world.customVerbs)
				sendContext("The verbs on screen have been replaced. They are now: " + neuroListNames(wanted) + ". Use them with interact, like any verb.", false);
			else
				sendContext("The usual verbs are back on screen.", true);
		}
	}
}

} // End of namespace Scumm
