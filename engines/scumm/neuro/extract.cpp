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

// Development tool: walks through every room of the game and writes, for
// each one, a picture of the whole room and a JSON file listing what is in
// it. The output is what descriptions are written and checked against.
//
//   NEURO_EXTRACT_DIR    where to write (turns the tool on)
//   NEURO_EXTRACT_FROM   first room to do (default 1)
//   NEURO_EXTRACT_SKIP   comma-separated rooms to leave out (ones that crash)
//   NEURO_DUMP_SCRIPTS   instead of all that: write every room's raw data and
//                        every global script to this directory, then quit.
//                        Nothing is entered or run, so no room can crash it.
//                        neuro/tools/transcribe/lines.py reads the result.
//
// Start the game from a savegame so that it is in a normal state. The room
// being worked on is written to "progress" first, so a crash names its room.

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "scumm/neuro/neuro.h"

#include "common/file.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include "scumm/actor.h"
#include "scumm/gfx.h"
#include "scumm/object.h"
#include "scumm/resource.h"
#include "scumm/scumm.h"

namespace Scumm {

static const int kSettleFrames = 8;
static const int kPanFrames = 3;

static void writeFile(const Common::String &path, const byte *data, uint32 size) {
	Common::DumpFile file;
	if (!file.open(Common::Path(path, Common::Path::kNativeSeparator))) {
		warning("Neuro: Could not write %s", path.c_str());
		return;
	}
	file.write(data, size);
	file.close();
}

static void writeText(const Common::String &path, const Common::String &text) {
	writeFile(path, (const byte *)text.c_str(), text.size());
}

void NeuroBridge::dumpScripts(const Common::String &dir) {
	Common::String index;
	for (int room = 1; room < _vm->_numRooms && room < (int)_vm->_res->_types[rtRoom].size(); room++) {
		// Rooms the index lists without a disk are numbers the game does not use
		if (_vm->_res->_types[rtRoom][room]._roomoffs == RES_INVALID_OFFSET || _vm->_res->_types[rtRoom][room]._roomno == 0)
			continue;
		_vm->ensureResourceLoaded(rtRoom, room);
		const byte *data = _vm->getResourceAddress(rtRoom, room);
		if (!data)
			continue;
		writeFile(Common::String::format("%s/room%03d.room", dir.c_str(), room), data, _vm->getResourceSize(rtRoom, room));
		index += Common::String::format("room %d\n", room);
	}
	for (int script = 1; script < _vm->_numGlobalScripts && script < (int)_vm->_res->_types[rtScript].size(); script++) {
		if (_vm->_res->_types[rtScript][script]._roomoffs == RES_INVALID_OFFSET || _vm->_res->_types[rtScript][script]._roomno == 0)
			continue;
		_vm->ensureResourceLoaded(rtScript, script);
		const byte *data = _vm->getResourceAddress(rtScript, script);
		if (!data)
			continue;
		writeFile(Common::String::format("%s/script%03d.scrp", dir.c_str(), script), data, _vm->getResourceSize(rtScript, script));
		index += Common::String::format("script %d room %d\n", script, _vm->_res->_types[rtScript][script]._roomno);
	}
	index += Common::String::format("game %s version %d\n", _vm->_game.gameid, _vm->_game.version);
	writeText(dir + "/index.txt", index);
}

void NeuroBridge::startExtraction() {
	const char *scripts = getenv("NEURO_DUMP_SCRIPTS");
	if (scripts && *scripts) {
		_extract.dir = scripts;
		_extract.phase = kExtractDumpScripts;
		return;
	}

	const char *dir = getenv("NEURO_EXTRACT_DIR");
	if (!dir || !*dir)
		return;

	_extract.dir = dir;
	const char *from = getenv("NEURO_EXTRACT_FROM");
	_extract.room = (from && *from) ? atoi(from) - 1 : 0;
	const char *skip = getenv("NEURO_EXTRACT_SKIP");
	if (skip) {
		Common::StringTokenizer rooms(skip, ", ");
		while (!rooms.empty())
			_extract.skip.push_back(atoi(rooms.nextToken().c_str()));
	}
	_extract.phase = kExtractWaitForGame;
}

void NeuroBridge::extractCopyScreen() {
	const VirtScreen &vs = _vm->_virtscr[kMainVirtScreen];
	const int width = _extract.width;
	for (int y = 0; y < _extract.height && y < vs.h; y++) {
		const byte *src = vs.getPixels(0, y);
		for (int x = 0; x < _vm->_screenWidth; x++) {
			const int roomX = vs.xstart + x;
			if (roomX < 0 || roomX >= width)
				continue;
			const byte *color = &_vm->_currentPalette[src[x] * 3];
			memcpy(&_extract.pixels[(y * width + roomX) * 3], color, 3);
		}
	}
}

void NeuroBridge::extractWriteRoom() {
	const int room = _extract.room;
	const Common::String base = Common::String::format("%s/room%03d", _extract.dir.c_str(), room);

	// The picture, as a PPM: trivial to write and any tool converts it
	Common::String header = Common::String::format("P6\n%d %d\n255\n", _extract.width, _extract.height);
	Common::Array<byte> image;
	image.resize(header.size() + _extract.pixels.size());
	memcpy(image.data(), header.c_str(), header.size());
	memcpy(image.data() + header.size(), _extract.pixels.data(), _extract.pixels.size());
	writeFile(base + ".ppm", image.data(), image.size());

	Common::JSONArray objects;
	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr < 1)
			continue;
		Common::JSONObject object;
		object.setVal("id", new Common::JSONValue((long long int)od.obj_nr));
		object.setVal("name", new Common::JSONValue(cleanText(_vm->getObjOrActorName(od.obj_nr), true)));
		object.setVal("x", new Common::JSONValue((long long int)od.x_pos));
		object.setVal("y", new Common::JSONValue((long long int)od.y_pos));
		object.setVal("w", new Common::JSONValue((long long int)od.width));
		object.setVal("h", new Common::JSONValue((long long int)od.height));
		object.setVal("state", new Common::JSONValue((long long int)od.state));
		object.setVal("untouchable", new Common::JSONValue(_vm->getClass(od.obj_nr, kObjectClassUntouchable)));
		object.setVal("parent", new Common::JSONValue((long long int)od.parent));
		objects.push_back(new Common::JSONValue(object));
	}

	Common::JSONArray actors;
	for (int i = 1; i < _vm->_numActors; i++) {
		Actor *actor = _vm->_actors[i];
		if (!actor->isInCurrentRoom() || i == _vm->VAR(_vm->VAR_EGO))
			continue;
		Common::JSONObject entry;
		entry.setVal("id", new Common::JSONValue((long long int)i));
		entry.setVal("name", new Common::JSONValue(cleanText(actor->getActorName(), true)));
		entry.setVal("x", new Common::JSONValue((long long int)actor->getPos().x));
		entry.setVal("y", new Common::JSONValue((long long int)actor->getPos().y));
		entry.setVal("costume", new Common::JSONValue((long long int)actor->_costume));
		entry.setVal("visible", new Common::JSONValue((bool)actor->_visible));
		actors.push_back(new Common::JSONValue(entry));
	}

	Common::JSONObject root;
	root.setVal("room", new Common::JSONValue((long long int)room));
	root.setVal("game", new Common::JSONValue(_vm->_game.gameid));
	root.setVal("width", new Common::JSONValue((long long int)_extract.width));
	root.setVal("height", new Common::JSONValue((long long int)_extract.height));
	// The room's own scripts may have moved the game elsewhere while it was being drawn
	root.setVal("stayed", new Common::JSONValue(_vm->_currentRoom == room));
	// A room with its own mouse handler (a script numbered past the global
	// ones) only understands real clicks while that handler is in place
	root.setVal("verb_script", new Common::JSONValue((long long int)_vm->VAR(_vm->VAR_VERB_SCRIPT)));
	root.setVal("own_mouse_handler", new Common::JSONValue(_vm->VAR(_vm->VAR_VERB_SCRIPT) >= _vm->_numGlobalScripts));
	root.setVal("objects", new Common::JSONValue(objects));
	root.setVal("actors", new Common::JSONValue(actors));
	writeText(base + ".json", Common::JSONValue(root).stringify(true));
}

bool NeuroBridge::extractNextRoom() {
	for (;;) {
		_extract.room++;
		if (_extract.room >= _vm->_numRooms)
			return false;
		bool skipped = false;
		for (const int room : _extract.skip)
			skipped |= room == _extract.room;
		if (!skipped && _vm->_res->_types[rtRoom][_extract.room]._roomoffs != RES_INVALID_OFFSET
				&& _vm->_res->_types[rtRoom][_extract.room]._roomno != 0)
			return true;
	}
}

void NeuroBridge::updateExtraction() {
	switch (_extract.phase) {
	case kExtractDumpScripts:
		dumpScripts(_extract.dir);
		_extract.phase = kExtractOff;
		_vm->quitGame();
		break;

	case kExtractWaitForGame:
		// Let the savegame finish loading
		if (!playerHasControl())
			return;
		_vm->_fastMode |= 2;
		_extract.phase = kExtractEnterRoom;
		break;

	case kExtractEnterRoom: {
		if (!extractNextRoom()) {
			writeText(_extract.dir + "/progress", "done\n");
			_extract.phase = kExtractOff;
			_vm->quitGame();
			return;
		}
		writeText(_extract.dir + "/progress", Common::String::format("%d\n", _extract.room));

		ego()->_room = _extract.room;
		_vm->startScene(_extract.room, nullptr, 0);
		_vm->_fullRedraw = true;
		_extract.frames = 0;
		_extract.phase = kExtractSettle;
		break;
	}

	case kExtractSettle:
		if (++_extract.frames < kSettleFrames)
			return;
		// The player's character was moved here only to enter the room, and is not part of it.
		// Hiding him can leave his last image on screen where nothing redraws,
		// so start again from the clean background and let the others redraw.
		ego()->hideActor();
		// A full redraw marks every actor in the room for drawing, hidden or not;
		// only one without a costume is really left out
		ego()->_costume = 0;
		{
			VirtScreen &vs = _vm->_virtscr[kMainVirtScreen];
			if (vs.hasTwoBuffers) {
				for (int y = 0; y < vs.h; y++)
					memcpy(vs.getPixels(0, y), vs.getBackPixels(0, y), vs.pitch);
			}
			for (int i = 1; i < _vm->_numActors; i++) {
				// A hidden actor that is marked for redrawing is drawn anyway
				if (_vm->_actors[i]->_visible)
					_vm->_actors[i]->_needRedraw = true;
			}
		}
		_extract.width = MAX<int>(_vm->_roomWidth, _vm->_screenWidth);
		_extract.height = _vm->_virtscr[kMainVirtScreen].h;
		_extract.pixels.clear();
		_extract.pixels.resize(_extract.width * _extract.height * 3);
		_extract.panX = _vm->_screenWidth / 2;
		_extract.frames = 0;
		_vm->setCameraAtEx(_extract.panX);
		_vm->_fullRedraw = true;
		_extract.phase = kExtractPan;
		break;

	case kExtractPan:
		if (++_extract.frames < kPanFrames)
			return;
		extractCopyScreen();
		if (_extract.panX + _vm->_screenWidth / 2 >= _extract.width || _vm->_currentRoom != _extract.room) {
			extractWriteRoom();
			_extract.phase = kExtractEnterRoom;
			return;
		}
		// Step by a screen, ending flush with the right edge
		_extract.panX = MIN<int>(_extract.panX + _vm->_screenWidth, _extract.width - _vm->_screenWidth / 2);
		_extract.frames = 0;
		_vm->setCameraAtEx(_extract.panX);
		_vm->_fullRedraw = true;
		break;

	default:
		break;
	}
}

} // End of namespace Scumm
