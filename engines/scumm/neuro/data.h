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
 *   prompt                 what she is asked when picking a line to say here, in
 *                          place of "Pick a line to say."
 *   layout = shifting      the room is redrawn as a new place without being left
 *                          (the catacombs); list it afresh when things change
 *   relative = yes         say whether each thing lies to the left or the right of
 *                          Guybrush (the navigator's head points one way or the other)
 *   comings = quiet        characters wander in and out of sight all the time here
 *                          (dots on the island map); do not report each one
 *   spot.NAME = X,Y | TEXT a place with no object that she can walk to; the
 *                          optional text says what she sees when she does
 *   standing.NAME = BOX BOX ... | TEXT
 *                          where Guybrush is, for a room with parts that do not
 *                          connect on foot (two sides of a stretch of water):
 *                          the text is given while he stands in one of those
 *                          walk boxes, and again when he gets to another part
 *   passages = ID ID ...   things that, walked to, bring another part of a wide
 *                          room into view where the picture does not follow
 *                          Guybrush there by itself. A command aimed at something
 *                          out of view goes by way of the one in view, as a
 *                          player, who cannot click outside the picture, would.
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
 * [watch.ID] sections put into words something the game only shows as animation
 * and keeps as a number (who won an exchange of a sword fight):
 *   var       the game variable
 *   change    "any" to speak whenever it changes, not only when it goes up
 *   say       what she is told; {value} is the new value
 * In what an alert or a watch says, {varN} is the value of game variable N and
 * {pickN: one | two | three} is the text for its value, counting from 1.
 * An alert with no "ask" only tells her what happened.
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
	bool shiftingLayout(int room) const;
	bool relativePlaces(int room) const;
	Common::String roomPrompt(int room) const;
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
	const Common::Array<Spot> &spots(int room) const;

	/** Where in the room Guybrush is when he stands in that walk box; empty if the file does not say. */
	Common::String standing(int room, int box) const;

	/** Things that bring another part of the room into view when walked to. */
	Common::Array<int> passages(int room) const;

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

	struct Watch {
		int var = 0;
		bool anyChange = false;
		Common::String say;
	};
	const Common::Array<Watch> &watches() const { return _watches; }

	/**
	 * A remark on lines she can say, from what the game keeps about them. It is
	 * given once, above the list, with the numbers of the lines it goes for. While
	 * the script runs, the line on verb V stands for entry VAR[slotVar + V - firstVerb];
	 * entries from min to max get the note unless bit (unlessBit + entry) is set
	 * (with no unlessBit, always).
	 */
	struct LineNote {
		int script = 0;
		int firstVerb = 0;
		int slotVar = 0;
		int min = 0, max = 0;
		int unlessBit = 0;
		Common::String note;
	};
	const Common::Array<LineNote> &lineNotes() const { return _lineNotes; }

private:
	Common::String get(const Common::String &section, const Common::String &key) const;
	/** Looks in the room's own section, then in that of the room it is drawn from. */
	bool roomKey(int room, const Common::String &key, Common::String &value) const;
	Common::String roomGet(int room, const Common::String &key) const;

	Common::HashMap<int, int> _roomResources;

	// The places of the room last asked about, kept so the file's text is not parsed on every look
	struct Standing {
		int box;
		Common::String text;
	};
	struct Places {
		int room = -1;
		Common::Array<Spot> spots;
		Common::Array<Standing> standing;
	};
	mutable Places _places;
	const Places &places(int room) const;

	Common::INIFile _ini;
	Common::Array<Alert> _alerts;
	Common::Array<Watch> _watches;
	Common::Array<LineNote> _lineNotes;
	Common::StringMap _lineSpeakers;
};

} // End of namespace Scumm

#endif
