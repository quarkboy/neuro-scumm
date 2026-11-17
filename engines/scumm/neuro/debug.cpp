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

// Actions that are never registered: the operator console's commands and the developer's tools.

#include "scumm/neuro/neuro.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"

#include "graphics/streampanel.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/resource.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

// A result is only sent for an id the server issued. The developer's driver
// uses this one, which the server would only warn about.
static const char *const kQuietId = "dbg";

void NeuroBridge::handleDebugAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	const bool forOperator = name == "debug_save" || name == "debug_load" || name == "debug_status" || name == "debug_set";
	const bool allowed = _debugActions == kDebugAll || (forOperator && _debugActions == kDebugOperator);
	if (allowed && (handleOperatorAction(id, name, data) || handleDeveloperAction(id, name, data)))
		return;
	if (id != kQuietId)
		sendActionResult(id, false, "Unknown action.");
}

bool NeuroBridge::handleOperatorAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	if (name == "debug_save" || name == "debug_load") {
		int slot = 0;
		char label[64] = "";
		sscanf(data.c_str(), "%d %63[^\n]", &slot, label);
		if (id != kQuietId)
			sendActionResult(id, true);
		if (name == "debug_save")
			_vm->requestSave(slot, label[0] ? label : "neuro");
		else
			_vm->requestLoad(slot);
		return true;
	}
	if (name == "debug_status") {
		sendStatus(id);
		return true;
	}
	if (name == "debug_set") {
		// "NAME VALUE"; the value may have spaces in it
		const size_t space = data.findFirstOf(' ');
		Common::String value = space == Common::String::npos ? Common::String() : data.substr(space + 1);
		value.trim();
		applySetting(data.substr(0, space), value);
		return true;
	}
	return false;
}

void NeuroBridge::sendStatus(const Common::String &id) {
	NeuroWorld world;
	collectWorld(world);

	Common::JSONObject status;
	Common::String location = _data.roomName(world.room);
	if (location.empty())
		location = Common::String::format("room %d", world.room);
	status.setVal("location", new Common::JSONValue(location));
	Common::JSONArray inventory;
	for (const auto &item : world.inventory)
		inventory.push_back(new Common::JSONValue(item.name));
	status.setVal("inventory", new Common::JSONValue(inventory));
	status.setVal("journal", new Common::JSONValue(_journal.describe(false)));
	status.setVal("idle_force_seconds", new Common::JSONValue((long long int)(_idleForceMs / 1000)));
	status.setVal("reaction_ms", new Common::JSONValue((long long int)_reactionMs));
	status.setVal("context_tokens", new Common::JSONValue((long long int)_contextTokens));
	status.setVal("pointer", new Common::JSONValue(_pointerGlide));
	status.setVal("skip_cutscenes", new Common::JSONValue(_allowSkip));
	status.setVal("fast_mode", new Common::JSONValue(Common::String(_fastMode == kFastAlways ? "always" : _fastMode == kFastScenes ? "scenes" : "off")));
	Common::JSONValue root(status);

	// Not sendActionResult(), which would count this as her doing something
	Common::JSONObject result;
	result.setVal("id", new Common::JSONValue(id));
	result.setVal("success", new Common::JSONValue(true));
	result.setVal("message", new Common::JSONValue(root.stringify()));
	send("action/result", &result);
}

/** A setting that can change while the game runs. */
void NeuroBridge::applySetting(const Common::String &setting, const Common::String &value) {
	// The first word is the value for everything but the title
	const Common::String word = value.substr(0, value.findFirstOf(' '));
	const int number = atoi(word.c_str());
	if (setting == "idle_force_seconds") {
		_idleForceMs = MAX(0, number) * 1000;
	} else if (setting == "reaction_ms") {
		_reactionMs = MAX(0, number);
	} else if (setting == "context_tokens") {
		_contextTokens = MAX(0, number);
	} else if (setting == "panel_title") {
		_panelTitle = value.empty() ? Common::String(" ") : value;
	} else if (setting == "panel_chat") {
		_panelChat = CLIP(number, 0, 80);
		_panelKey.clear();
	} else if (setting == "panel_side") {
		Graphics::streamPanel().onLeft = word.equalsIgnoreCase("left");
	} else if (setting == "panel_hide") {
		// A comma-separated list of the column's parts, or "none"
		_panelHide = word == "none" ? Common::String(",,") : "," + word + ",";
	} else if (setting == "hold") {
		_panelHeld = word.equalsIgnoreCase("on");
	} else if (setting == "skip_cutscenes") {
		const bool allow = !word.equalsIgnoreCase("off");
		if (allow != _allowSkip) {
			_allowSkip = allow;
			if (allow)
				registerActions();
			else
				unregisterAction("skip_cutscene");
		}
	} else if (setting == "pointer") {
		_pointerGlide = !word.equalsIgnoreCase("off");
	} else if (setting == "fast_mode") {
		_fastMode = word.equalsIgnoreCase("always") ? kFastAlways : word.equalsIgnoreCase("scenes") ? kFastScenes : kFastOff;
		if (_fastMode == kFastOff && _speedSet) {
			_vm->_fastMode &= ~1;
			_speedSet = false;
		}
	} else if (setting == "music_volume" || setting == "sfx_volume" || setting == "speech_volume") {
		ConfMan.setInt(setting, CLIP(number, 0, 255));
		_vm->syncSoundSettings();
	}
}

bool NeuroBridge::handleDeveloperAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	Common::String text;
	if (name == "debug_screenshot") {
		g_system->saveScreenshot();
	} else if (name == "debug_dump") {
		debugDump();
	} else if (name == "debug_speed") {
		_fastMode = kFastOff;
		_vm->_fastMode = atoi(data.c_str());
	} else if (name == "debug_click") {
		// "X,Y" in the room
		int x = 0, y = 0;
		sscanf(data.c_str(), "%d,%d", &x, &y);
		clickInRoom(x, y);
	} else if (name == "debug_sentence") {
		// "VERB OBJECT [OBJECT]"
		int verb = 0, a = 0, b = 0;
		sscanf(data.c_str(), "%d %d %d", &verb, &a, &b);
		_vm->doSentence(verb, a, b);
	} else if (name == "debug_entry") {
		// "OBJ" or "OBJ VERB": where an object is, who owns it, which verbs it handles
		int obj = 0, verb = -1;
		sscanf(data.c_str(), "%d %d", &obj, &verb);
		text = Common::String::format("object %d: where %d, owner %d, class bits %x; handles verbs:",
			obj, _vm->whereIsObject(obj), _vm->objIsActor(obj) ? -1 : _vm->getOwner(obj), _vm->objIsActor(obj) ? 0 : _vm->_classData[obj]);
		for (int v = 1; v < 256; v++) {
			if ((verb < 0 || v == verb) && _vm->getVerbEntrypoint(obj, v))
				text += Common::String::format(" %d", v);
		}
	} else if (name == "debug_give") {
		// "OBJECT ROOM": put an object from that room into the inventory, as picking it up does
		int object = 0, room = 0;
		if (sscanf(data.c_str(), "%d %d", &object, &room) == 2) {
			_vm->addObjectToInventory(object, room);
			_vm->putOwner(object, _vm->VAR(_vm->VAR_EGO));
			_vm->putClass(object, kObjectClassUntouchable, 1);
			_vm->putState(object, 1);
			_vm->runInventoryScript(1);
		}
	} else if (name == "debug_var" || name == "debug_bit") {
		// "N" reads a game variable (or bit variable), "N VALUE" writes it
		int number = 0, value = 0;
		const int given = sscanf(data.c_str(), "%d %d", &number, &value);
		const uint var = name == "debug_bit" ? (0x8000 | number) : number;
		if (given == 2)
			_vm->writeVar(var, value);
		text = Common::String::format("%s %d = %d", name.c_str() + 6, number, _vm->readVar(var));
	} else if (name == "debug_room") {
		const int room = atoi(data.c_str());
		ego()->_room = room;
		_vm->startScene(room, nullptr, 0);
		_vm->_fullRedraw = true;
	} else {
		return false;
	}

	if (!text.empty())
		debug("NEURODUMP %s", text.c_str());
	if (id != kQuietId)
		sendActionResult(id, true, text);
	return true;
}

void NeuroBridge::debugDump() {
	debug("NEURODUMP room %d (resource %d) width %d", _vm->_currentRoom, _vm->_roomResource, _vm->_roomWidth);
	NeuroWorld world;
	collectWorld(world);
	debug("NEURODUMP state: inDialogue %d verbs %u standard %u forceAge %u pending %u controlSince %u", world.inDialogue, world.verbs.size(),
		_standardVerbs.size(), g_system->getMillis() - _forceSentAt, _pendingDialogue.size(), _controlSince);
	debug("NEURODUMP state: control %d userPut %d cursor %d sentences %d speechOpen %d moving %d forceActive %d idle %u ms click %d queue %u",
		playerHasControl(), _vm->_userPut, _vm->_cursor.state, _vm->_sentenceNum, _speechOpen,
		ego()->_moving, _forceActive, g_system->getMillis() - _lastActivity, (int)_clickPhase, _queue.size());
	debug("NEURODUMP verb script %d", _vm->VAR_VERB_SCRIPT == 0xFF ? -1 : _vm->VAR(_vm->VAR_VERB_SCRIPT));
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

} // End of namespace Scumm
