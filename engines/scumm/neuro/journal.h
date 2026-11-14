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

#ifndef SCUMM_NEURO_JOURNAL_H
#define SCUMM_NEURO_JOURNAL_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/formats/ini-file.h"
#include "common/str.h"

namespace Scumm {

/**
 * What she is trying to do, kept outside her own memory.
 *
 * Goals come from [goal.ID] sections of the description file, in order:
 *   text    what a character told her to do, in plain words
 *   start   when the goal appears
 *   done    when it is ticked off
 * A trigger is one of:
 *   heard: WORDS   a spoken line contains the words
 *   have: NAME     an inventory item's name contains the name
 *   room: N        she is in room N
 *   goal: ID       another goal was completed
 * A goal without a start trigger is there from the beginning.
 */
class NeuroJournal {
public:
	void loadGoals(const Common::INIFile &ini);
	void reset();

	// Each returns true if a goal appeared or was completed
	bool heard(const Common::String &line);
	bool observed(int room, const Common::StringArray &inventory);

	/** Announcements for goals that changed since the last call. */
	Common::String takeNews();

	void addNote(const Common::String &text);

	Common::String describe(bool withNotes = true) const;
	bool isEmpty() const;

	/** Bridge state that has to survive a save and load along with the goals. */
	Common::StringMap memos;

	void save(const Common::String &fileName) const;
	void load(const Common::String &fileName);

private:
	enum State {
		kHidden,
		kActive,
		kDone
	};
	struct Goal {
		Common::String id, text, start, done;
		State state;
	};

	bool fire(const Common::String &kind, const Common::String &argument);
	void setState(Goal &goal, State state);

	Common::Array<Goal> _goals;
	Common::StringArray _notes;
	Common::String _news;
};

} // End of namespace Scumm

#endif
