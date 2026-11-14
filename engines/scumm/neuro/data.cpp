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

bool NeuroData::load(const Common::String &path) {
	Common::FSNode node(Common::Path(path, Common::Path::kNativeSeparator));
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream) {
		warning("Neuro: Could not open description file '%s'", path.c_str());
		return false;
	}

	_ini.allowNonEnglishCharacters();
	const bool ok = _ini.loadFromStream(*stream);
	delete stream;
	if (!ok) {
		warning("Neuro: Could not parse description file '%s'", path.c_str());
		return false;
	}

	for (const auto &section : _ini.getSections()) {
		if (!section.name.hasPrefix("alert."))
			continue;
		Alert alert;
		alert.room = atoi(get(section.name, "room").c_str());
		alert.leadMs = (uint32)(atof(get(section.name, "lead").c_str()) * 1000);
		alert.priority = get(section.name, "priority");
		alert.say = get(section.name, "say");
		alert.ask = get(section.name, "ask");
		alert.command = get(section.name, "do");
		alert.then = get(section.name, "then");

		const Common::String when = get(section.name, "when");
		const size_t colon = when.findFirstOf(':');
		if (colon == Common::String::npos) {
			warning("Neuro: Alert '%s' has no usable 'when'", section.name.c_str());
			continue;
		}
		alert.kind = when.substr(0, colon);
		alert.name = when.substr(colon + 1);
		alert.kind.trim();
		alert.name.trim();
		alert.kind.toLowercase();
		alert.name.toLowercase();
		_alerts.push_back(alert);
	}
	return true;
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

Common::String NeuroData::roomName(int room) const {
	return get(roomSection(room), "name");
}

Common::String NeuroData::roomDescription(int room) const {
	return get(roomSection(room), "description");
}

Common::String NeuroData::objectNote(int room, int object) const {
	return get(roomSection(room), Common::String::format("object.%d", object));
}

Common::String NeuroData::voiceName(int room, int color) const {
	return get(roomSection(room), Common::String::format("voice.%d", color));
}

bool NeuroData::actorName(int room, int actor, Common::String &name) const {
	const Common::String key = Common::String::format("actor.%d", actor);
	if (_ini.hasSection(roomSection(room)) && _ini.getKey(key, roomSection(room), name)) {
		name.trim();
		return true;
	}
	if (_ini.hasSection("actors") && _ini.getKey(Common::String::format("%d", actor), "actors", name)) {
		name.trim();
		return true;
	}
	return false;
}

Common::Array<NeuroData::Spot> NeuroData::spots(int room) const {
	Common::Array<Spot> result;
	if (!_ini.hasSection(roomSection(room)))
		return result;

	for (const auto &kv : _ini.getKeys(roomSection(room))) {
		if (!kv.key.hasPrefix("spot."))
			continue;
		Spot spot;
		spot.name = kv.key.substr(5);
		if (sscanf(kv.value.c_str(), "%d,%d", &spot.x, &spot.y) != 2)
			continue;
		const size_t bar = kv.value.findFirstOf('|');
		if (bar != Common::String::npos) {
			spot.effect = kv.value.substr(bar + 1);
			spot.effect.trim();
		}
		result.push_back(spot);
	}
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
