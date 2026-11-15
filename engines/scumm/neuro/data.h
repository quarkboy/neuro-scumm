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
#include "common/hash-str.h"
#include "common/str.h"

namespace Scumm {

/**
 * Hand-written text for what the game only shows as pictures: room names
 * and descriptions, what an object is, who an unnamed character is.
 *
 * The file is an INI file with one [roomN] section per room:
 *   name, description      the room itself
 *   object.ID              a note shown after the object's name
 *   name.ID                a name for an object in place of the game's own;
 *                          an empty value hides the object
 *   actor.ID               a name for an actor; overrides the game's own, which
 *                          can be left over from another room. A leading '-'
 *                          names the speaker but keeps it out of the room list,
 *                          and an empty value hides the actor.
 *   voice.COLOR            who is speaking when text of that colour appears
 *                          without belonging to any actor (a dog that is
 *                          part of the scenery, someone off screen); "-"
 *                          drops such text (labels drawn under the pointer)
 *   voice.ACTOR.COLOR      who is speaking when that actor's text has that
 *                          colour (one actor standing in for a group)
 *   comings = quiet        characters wander in and out of sight all the time here
 *                          (dots on the island map); do not report each one
 *   spot.NAME = X,Y | TEXT a place with no object that she can walk to; the
 *                          optional text says what she sees when she does
 * A room that the game draws from another room's data (the forest screens)
 * uses that room's section for whatever its own section does not say.
 * [alert.ID] sections turn a moment she must not miss into a prompt:
 *   room      room number it applies to (omit for any room)
 *   when      arrived: / left: (a character), appeared: / disappeared: (a
 *             thing), gained: / lost: (an inventory item), then part of a name;
 *             or just "entered", for walking into the room
 *   while     optional "script N": only while that game script is running
 *   lead      seconds after that moment at which her action should land;
 *             the prompt goes out early by her expected reaction time
 *   priority  low, medium, high or critical; how hard to interrupt her
 *   say       what a player watching the screen would have just seen
 *   ask       what she is asked to do about it
 *   do        optional "VERB | TARGET" (or "VERB | TARGET | SECOND TARGET").
 *             With it, ask must be a yes/no question, and answering yes
 *             carries out that command at once. For a character who left,
 *             {exit} in say and do stands for the thing they were last seen at.
 *   hold      optional number of a game variable (a timer) that is kept from
 *             changing while the question is open
 *   then      optional second command in the same form, run as soon as the
 *             first one has played out
 * [line.ID] sections name who says one particular line, where neither the
 * actor nor the text colour settles it (sound effects shown as text, say):
 *   text      the line, exactly as it is sent
 *   speaker   the name to use; "-" drops the line altogether
 * A [game] section holds switches:
 *   door_question = no     do not ask "Go through it?" after she opens a door
 * and two global sections:
 *   [actors]  ID = name    fallback names for unnamed actors
 *   [states]  object name = label for state 0, label for state 1, ...
 */
class NeuroData {
public:
	bool load(const Common::String &path);

	/** Says which room's data the game is drawing the given room from. */
	void setRoomResource(int room, int resource);

	/** The room whose section supplies the description: the room itself or the one it is drawn from. */
	int descriptionRoom(int room) const;

	Common::String roomName(int room) const;
	bool quietComings(int room) const;
	bool doorQuestion() const;
	Common::String roomDescription(int room) const;
	Common::String objectNote(int room, int object) const;
	/** Returns false if the file does not rename this object; an empty name hides it. */
	bool objectName(int room, int object, Common::String &name) const;
	/** actor: the actor number, or 0 for text that belongs to no actor. */
	Common::String voiceName(int room, int color, int actor = 0) const;

	/** The speaker set for exactly this line; false if there is none. */
	bool lineSpeaker(const Common::String &text, Common::String &speaker) const;

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
		int holdVar = 0;
		int whileScript = 0;
		Common::String kind, name, priority, say, ask, command, then;
	};
	const Common::Array<Alert> &alerts() const { return _alerts; }

private:
	Common::String get(const Common::String &section, const Common::String &key) const;
	/** Looks in the room's own section, then in that of the room it is drawn from. */
	bool roomKey(int room, const Common::String &key, Common::String &value) const;
	Common::String roomGet(int room, const Common::String &key) const;

	Common::HashMap<int, int> _roomResources;

	Common::INIFile _ini;
	Common::Array<Alert> _alerts;
	Common::StringMap _lineSpeakers;
};

} // End of namespace Scumm

#endif
