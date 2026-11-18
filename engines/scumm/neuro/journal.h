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
 *   panel   "no" keeps it out of the stream column (a goal that only holds notes)
 * A trigger is one of:
 *   heard: WORDS   a spoken line contains the words
 *   have: NAME     an inventory item's name contains the name
 *   room: N        she is in room N
 *   goal: ID       another goal was completed
 * Several triggers separated by " | " mean any of them. A name starting
 * with '=' must match exactly rather than be contained.
 * A goal without a start trigger is there from the beginning.
 *
 * [clue.ID] sections are things she learns that help with a goal:
 *   goal    the id of the goal it belongs to
 *   when    a trigger, as above (usually heard: with the line's words)
 *   when_bit   also note the clue when this game bit is set
 *   superseded_by   hide this clue once the named clue has been found
 *   text    the clue as a short note in plain words
 * A clue is noted once, announced at the next pause, and listed under its
 * goal for as long as that goal is unfinished. A goal's optional "title" is
 * the few words she uses to ask for its clues.
 *
 * [nudge.ID] sections suggest writing something down, once:
 *   when    a trigger, as above
 *   say     optional wording; the default just says it may be worth a note
 * A nudge never says what to write: the note is hers.
 */
class NeuroJournal {
public:
	void loadGoals(const Common::INIFile &ini);
	void reset();

	// What she hears and sees can start or finish a goal, or be a clue
	void heard(const Common::String &line);
	void observed(int room, const Common::StringArray &inventory);
	template<class BitSet>
	void observeBits(BitSet bitSet) {
		for (auto &clue : _clues) {
			if (!clue.found && clue.whenBit >= 0 && bitSet(clue.whenBit))
				noteClue(clue);
		}
	}

	/** Announcements for goals that changed since the last call. */
	Common::String takeNews();

	void addNote(const Common::String &text);

	/**
	 * A clue can quote the game's variables ({varN} and the other forms the
	 * description file allows), for what was only shown as animation.
	 */
	typedef Common::String (*Expander)(void *context, const Common::String &text);
	void setExpander(Expander expander, void *context) {
		_expand = expander;
		_expandContext = context;
	}

	Common::String describe(bool withCompleted = false) const;

	/** The clues noted for one unfinished goal, named by its title, id or opening words. */
	Common::String describeClues(const Common::String &which) const;

	/** The unfinished goals to show viewers, each as its text. */
	Common::StringArray panelGoals() const;

	/** Bridge state that has to survive a save and load along with the goals. */
	Common::StringMap memos;

	void save(const Common::String &fileName) const;
	void load(const Common::String &fileName);

private:
	Expander _expand = nullptr;
	void *_expandContext = nullptr;
	Common::String expand(const Common::String &text) const;

	enum State {
		kHidden,
		kActive,
		kDone
	};
	struct Goal {
		Common::String id, text, start, done, title;
		State state;
		bool nudge;
		bool onPanel;
	};
	struct Clue {
		Common::String id, goal, when, text, supersededBy;
		int whenBit = -1;
		bool found;
	};
	Common::Array<Clue> _clues;
	bool listed(const Clue &clue) const;
	void noteClue(Clue &clue);
	Common::String goalTitle(const Common::String &id) const;

	void fire(const Common::String &kind, const Common::String &argument);
	void setState(Goal &goal, State state);

	Common::Array<Goal> _goals;
	Common::StringArray _notes;
	Common::String _news;
};

} // End of namespace Scumm

#endif
