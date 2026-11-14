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

#ifndef SCUMM_NEURO_DATA_H
#define SCUMM_NEURO_DATA_H

#include "common/array.h"
#include "common/formats/ini-file.h"
#include "common/str.h"

namespace Scumm {

/**
 * Hand-written text for what the game only shows as pictures: room names
 * and descriptions, what an object is, who an unnamed character is.
 *
 * The file is an INI file with one [roomN] section per room:
 *   name, description      the room itself
 *   object.ID              a note shown after the object's name
 *   actor.ID               a name for an actor; overrides the game's own, which
 *                          can be left over from another room. A leading '-'
 *                          names the speaker but keeps it out of the room list,
 *                          and an empty value hides the actor.
 *   spot.NAME = X,Y | TEXT a place with no object that she can walk to; the
 *                          optional text says what she sees when she does
 * [alert.ID] sections turn a moment she must not miss into a prompt:
 *   room      room number it applies to (omit for any room)
 *   when      arrived: / left: (a character), appeared: / disappeared: (a
 *             thing), gained: / lost: (an inventory item), then part of a name
 *   lead      seconds after that moment at which her action should land;
 *             the prompt goes out early by her expected reaction time
 *   priority  low, medium, high or critical; how hard to interrupt her
 *   say       what a player watching the screen would have just seen
 *   ask       what she is asked to do about it
 *   do        optional "VERB | TARGET" (or "VERB | TARGET | SECOND TARGET").
 *             With it, ask must be a yes/no question, and answering yes
 *             carries out that command at once.
 * and two global sections:
 *   [actors]  ID = name    fallback names for unnamed actors
 *   [states]  object name = label for state 0, label for state 1, ...
 */
class NeuroData {
public:
	bool load(const Common::String &path);

	Common::String roomName(int room) const;
	Common::String roomDescription(int room) const;
	Common::String objectNote(int room, int object) const;
	/** Returns false if the file says nothing about this actor. */
	bool actorName(int room, int actor, Common::String &name) const;

	struct Spot {
		Common::String name;
		int x, y;

		/** What stepping there looks like. */
		Common::String effect;
	};
	Common::Array<Spot> spots(int room) const;

	/** A word for the state an object is in, e.g. "open"; empty if unknown. */
	Common::String stateName(const Common::String &objectName, int state) const;

	const Common::INIFile &ini() const { return _ini; }

	struct Alert {
		int room = 0;
		uint32 leadMs = 0;
		Common::String kind, name, priority, say, ask, command;
	};
	const Common::Array<Alert> &alerts() const { return _alerts; }

private:
	Common::String get(const Common::String &section, const Common::String &key) const;

	Common::INIFile _ini;
	Common::Array<Alert> _alerts;
};

} // End of namespace Scumm

#endif
