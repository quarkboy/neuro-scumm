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

#include "scumm/neuro/data.h"

#include "common/fs.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"
#include "common/util.h"

namespace Scumm {

void NeuroData::load(const Common::String &path) {
	Common::FSNode node(Common::Path(path, Common::Path::kNativeSeparator));
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream) {
		warning("Neuro: Could not open description file '%s'", path.c_str());
		return;
	}

	_ini.allowNonEnglishCharacters();
	const bool ok = _ini.loadFromStream(*stream);
	delete stream;
	if (!ok) {
		warning("Neuro: Could not parse description file '%s'", path.c_str());
		return;
	}

	_game.name = get("game", "name");
	_game.intro = get("game", "intro");
	_game.egoName = get("game", "ego_name");
	_game.walkVerb = get("game", "walk_verb");
	_game.giveVerb = get("game", "give_verb");
	_game.openVerb = get("game", "open_verb");
	_game.doorOpenState = get("game", "door_open_state");
	_game.doorQuestion = !get("game", "door_question").equalsIgnoreCase("no");
	_game.sentenceLineVerb = atoi(get("game", "sentence_line_verb").c_str());
	_game.inventoryFirstVerb = atoi(get("game", "inventory_first_verb").c_str());
	_game.inventorySlots = atoi(get("game", "inventory_slots").c_str());
	_game.inventorySlotVar = atoi(get("game", "inventory_slot_var").c_str());
	_game.inventoryUpVerb = atoi(get("game", "inventory_up_verb").c_str());
	_game.inventoryDownVerb = atoi(get("game", "inventory_down_verb").c_str());

	for (const auto &section : _ini.getSections()) {
		const Common::String &name = section.name;
		if (name.hasPrefix("line.")) {
			const Common::String text = get(name, "text");
			if (!text.empty())
				_lineSpeakers[text] = get(name, "speaker");
		} else if (name.hasPrefix("linenote.")) {
			addLineNote(name);
		} else if (name.hasPrefix("watch.")) {
			addWatch(name);
		} else if (name.hasPrefix("alert.")) {
			addAlert(name);
		}
	}
}

/** The N of "script N". */
static int scriptNumber(const Common::String &value) {
	return atoi(value.hasPrefixIgnoreCase("script") ? value.c_str() + 6 : value.c_str());
}

void NeuroData::addLineNote(const Common::String &section) {
	LineNote note;
	note.script = scriptNumber(get(section, "while"));
	note.firstVerb = atoi(get(section, "first_verb").c_str());
	note.slotVar = atoi(get(section, "slot_var").c_str());
	note.min = atoi(get(section, "min").c_str());
	note.max = atoi(get(section, "max").c_str());
	note.unlessBit = atoi(get(section, "unless_bit").c_str());
	note.note = get(section, "note");
	if (note.script && note.slotVar && !note.note.empty())
		_lineNotes.push_back(note);
}

void NeuroData::addWatch(const Common::String &section) {
	Watch watch;
	watch.var = atoi(get(section, "var").c_str());
	watch.say = get(section, "say");
	watch.anyChange = get(section, "change").equalsIgnoreCase("any");
	if (watch.var && !watch.say.empty())
		_watches.push_back(watch);
}

void NeuroData::addAlert(const Common::String &section) {
	Alert alert;
	alert.room = atoi(get(section, "room").c_str());
	alert.leadMs = (uint32)(atof(get(section, "lead").c_str()) * 1000);
	alert.priority = get(section, "priority");
	alert.say = get(section, "say");
	alert.ask = get(section, "ask");
	alert.command = get(section, "do");
	alert.then = get(section, "then");
	alert.holdVar = atoi(get(section, "hold").c_str());
	alert.whileScript = scriptNumber(get(section, "while"));

	const Common::String when = get(section, "when");
	const size_t colon = when.findFirstOf(':');
	if (when.equalsIgnoreCase("entered") && alert.room) {
		alert.kind = "entered";
	} else if (colon == Common::String::npos) {
		warning("Neuro: Alert '%s' has no usable 'when'", section.c_str());
		return;
	} else {
		alert.kind = when.substr(0, colon);
		alert.name = when.substr(colon + 1);
		alert.kind.trim();
		alert.name.trim();
		alert.kind.toLowercase();
		alert.name.toLowercase();
	}
	_alerts.push_back(alert);
}

Common::String NeuroData::get(const Common::String &section, const Common::String &key) const {
	Common::String value;
	if (_ini.hasSection(section))
		_ini.getKey(key, section, value);
	value.trim();
	return value;
}

static Common::String roomSection(int room) {
	return Common::String::format("room%d", room);
}

void NeuroData::setRoomResource(int room, int resource) {
	if (room != resource)
		_roomResources[room] = resource;
}

bool NeuroData::roomKey(int room, const Common::String &key, Common::String &value) const {
	const int rooms[2] = {room, _roomResources.getValOrDefault(room, 0)};
	for (const int candidate : rooms) {
		const Common::String section = roomSection(candidate);
		if (candidate && _ini.hasSection(section) && _ini.getKey(key, section, value)) {
			value.trim();
			return true;
		}
	}
	return false;
}

Common::String NeuroData::roomGet(int room, const Common::String &key) const {
	Common::String value;
	roomKey(room, key, value);
	return value;
}

int NeuroData::descriptionRoom(int room) const {
	if (get(roomSection(room), "description").empty() && _roomResources.contains(room))
		return _roomResources[room];
	return room;
}

Common::String NeuroData::roomName(int room) const {
	return roomGet(room, "name");
}

bool NeuroData::quietComings(int room) const {
	return roomGet(room, "comings").equalsIgnoreCase("quiet");
}

bool NeuroData::shiftingLayout(int room) const {
	return roomGet(room, "layout").equalsIgnoreCase("shifting");
}

bool NeuroData::relativePlaces(int room) const {
	return roomGet(room, "relative").equalsIgnoreCase("yes");
}

Common::String NeuroData::roomPrompt(int room) const {
	return roomGet(room, "prompt");
}

bool NeuroData::synonym(const Common::String &word, Common::String &verb) const {
	if (word.empty() || !_ini.hasSection("synonyms"))
		return false;
	for (const auto &kv : _ini.getKeys("synonyms")) {
		if (kv.key.equalsIgnoreCase(word)) {
			verb = kv.value;
			verb.trim();
			return true;
		}
	}
	return false;
}

Common::String NeuroData::roomDescription(int room) const {
	return roomGet(room, "description");
}

Common::String NeuroData::objectNote(int room, int object) const {
	return roomGet(room, Common::String::format("object.%d", object));
}

bool NeuroData::objectName(int room, int object, Common::String &name) const {
	return roomKey(room, Common::String::format("name.%d", object), name);
}

bool NeuroData::lineSpeaker(const Common::String &text, Common::String &speaker) const {
	if (!_lineSpeakers.contains(text))
		return false;
	speaker = _lineSpeakers[text];
	return true;
}

Common::String NeuroData::voiceName(int room, int color, int actor) const {
	if (actor)
		return roomGet(room, Common::String::format("voice.%d.%d", actor, color));
	return roomGet(room, Common::String::format("voice.%d", color));
}

bool NeuroData::actorName(int room, int actor, Common::String &name) const {
	if (roomKey(room, Common::String::format("actor.%d", actor), name))
		return true;
	if (_ini.hasSection("actors") && _ini.getKey(Common::String::format("%d", actor), "actors", name)) {
		name.trim();
		return true;
	}
	return false;
}

const NeuroData::Places &NeuroData::places(int room) const {
	if (!_ini.hasSection(roomSection(room)))
		room = _roomResources.getValOrDefault(room, 0);
	if (_places.room == room)
		return _places;

	_places = Places();
	_places.room = room;
	if (!_ini.hasSection(roomSection(room)))
		return _places;

	for (const auto &kv : _ini.getKeys(roomSection(room))) {
		const size_t bar = kv.value.findFirstOf('|');
		Common::String text;
		if (bar != Common::String::npos) {
			text = kv.value.substr(bar + 1);
			text.trim();
		}
		if (kv.key.hasPrefix("spot.")) {
			Spot spot;
			spot.name = kv.key.substr(5);
			if (sscanf(kv.value.c_str(), "%d,%d", &spot.x, &spot.y) != 2)
				continue;
			spot.effect = text;
			spot.twice = kv.value.substr(0, bar).contains("twice");
			_places.spots.push_back(spot);
		} else if (kv.key.hasPrefix("standing.") && bar != Common::String::npos) {
			Common::StringTokenizer boxes(kv.value.substr(0, bar), " ,");
			while (!boxes.empty())
				_places.standing.push_back({atoi(boxes.nextToken().c_str()), text});
		}
	}
	return _places;
}

const Common::Array<NeuroData::Spot> &NeuroData::spots(int room) const {
	return places(room).spots;
}

Common::String NeuroData::standing(int room, int box) const {
	for (const auto &place : places(room).standing) {
		if (place.box == box)
			return place.text;
	}
	return "";
}

Common::Array<int> NeuroData::passages(int room) const {
	Common::Array<int> result;
	Common::StringTokenizer ids(roomGet(room, "passages"), " ,");
	while (!ids.empty())
		result.push_back(atoi(ids.nextToken().c_str()));
	return result;
}

Common::String NeuroData::stateName(const Common::String &objectName, int state) const {
	// "door 2" uses the entry for "door"
	Common::String base = objectName;
	while (!base.empty() && Common::isDigit(base.lastChar()))
		base.deleteLastChar();
	base.trim();

	Common::StringTokenizer labels(get("states", base), ",");
	for (int i = 0; !labels.empty(); i++) {
		Common::String label = labels.nextToken();
		label.trim();
		if (i == state)
			return label;
	}
	return Common::String();
}

} // End of namespace Scumm
