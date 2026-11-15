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

#include "scumm/neuro/journal.h"

#include "common/savefile.h"
#include "common/system.h"

namespace Scumm {

static const uint kMaxNotes = 12;
static const uint kMaxNoteLength = 300;

void NeuroJournal::loadGoals(const Common::INIFile &ini) {
	_goals.clear();
	_clues.clear();
	for (const auto &section : ini.getSections()) {
		if (section.name.hasPrefix("clue.")) {
			Clue clue;
			clue.id = section.name.substr(5);
			if (const Common::INIFile::KeyValue *kv = section.getKey("goal"))
				clue.goal = kv->value;
			if (const Common::INIFile::KeyValue *kv = section.getKey("when"))
				clue.when = kv->value;
			if (const Common::INIFile::KeyValue *kv = section.getKey("text"))
				clue.text = kv->value;
			clue.goal.trim();
			clue.when.trim();
			clue.text.trim();
			clue.found = false;
			if (!clue.text.empty())
				_clues.push_back(clue);
			continue;
		}
		const bool nudge = section.name.hasPrefix("nudge.");
		if (!nudge && !section.name.hasPrefix("goal."))
			continue;
		Goal goal;
		goal.nudge = nudge;
		// Nudges keep their prefix so that they cannot collide with a goal's id
		goal.id = nudge ? section.name : section.name.substr(5);
		if (const Common::INIFile::KeyValue *kv = section.getKey(nudge ? "say" : "text"))
			goal.text = kv->value;
		if (const Common::INIFile::KeyValue *kv = section.getKey(nudge ? "when" : "start"))
			goal.start = kv->value;
		if (const Common::INIFile::KeyValue *kv = section.getKey("title"))
			goal.title = kv->value;
		goal.title.trim();
		if (nudge && goal.text.empty())
			goal.text = "That sounded worth remembering. You can write it down with add_note.";
		if (const Common::INIFile::KeyValue *kv = section.getKey("done"))
			goal.done = kv->value;
		goal.text.trim();
		goal.start.trim();
		goal.done.trim();
		goal.state = kHidden;
		_goals.push_back(goal);
	}
	reset();
}

void NeuroJournal::reset() {
	for (auto &goal : _goals)
		goal.state = (goal.start.empty() && !goal.nudge) ? kActive : kHidden;
	for (auto &clue : _clues)
		clue.found = false;
	_notes.clear();
	_news.clear();
	memos.clear();
}

void NeuroJournal::setState(Goal &goal, State state) {
	if (goal.state == state)
		return;
	goal.state = state;
	if (goal.nudge)
		_news += goal.text + "\n";
	else
		_news += (state == kActive ? "New goal: " : "Goal completed: ") + goal.text + "\n";
}

static bool matchesOne(const Common::String &trigger, const Common::String &kind, const Common::String &argument);

static bool matches(const Common::String &trigger, const Common::String &kind, const Common::String &argument) {
	size_t start = 0;
	for (;;) {
		const size_t bar = trigger.find(" | ", start);
		if (matchesOne(trigger.substr(start, bar == Common::String::npos ? Common::String::npos : bar - start), kind, argument))
			return true;
		if (bar == Common::String::npos)
			return false;
		start = bar + 3;
	}
}

static bool matchesOne(const Common::String &trigger, const Common::String &kind, const Common::String &argument) {
	const size_t colon = trigger.findFirstOf(':');
	if (colon == Common::String::npos)
		return false;
	Common::String wantKind = trigger.substr(0, colon);
	Common::String wantArgument = trigger.substr(colon + 1);
	wantKind.trim();
	wantArgument.trim();
	if (!wantKind.equalsIgnoreCase(kind) || wantArgument.empty())
		return false;

	if (kind == "room" || kind == "goal")
		return wantArgument.equalsIgnoreCase(argument);
	if (wantArgument.hasPrefix("="))
		return argument.equalsIgnoreCase(wantArgument.substr(1));

	Common::String haystack = argument;
	haystack.toLowercase();
	wantArgument.toLowercase();
	return haystack.contains(wantArgument);
}

Common::String NeuroJournal::goalTitle(const Common::String &id) const {
	for (const auto &goal : _goals) {
		if (goal.id != id)
			continue;
		if (!goal.title.empty())
			return goal.title;
		// The first few words of the goal stand in for a title
		Common::String title;
		int words = 0;
		for (uint i = 0; i < goal.text.size() && words < 3; i++) {
			if (goal.text[i] == ' ' || goal.text[i] == ':')
				words++;
			if (goal.text[i] == ':')
				break;
			title += goal.text[i];
		}
		title.trim();
		return title;
	}
	return id;
}

bool NeuroJournal::fire(const Common::String &kind, const Common::String &argument) {
	bool changed = false;
	for (auto &clue : _clues) {
		if (!clue.found && matches(clue.when, kind, argument)) {
			clue.found = true;
			changed = true;
			_news += "Clue noted (" + goalTitle(clue.goal) + "): " + clue.text + "\n";
		}
	}
	for (uint i = 0; i < _goals.size(); i++) {
		Goal &goal = _goals[i];
		if (goal.state == kHidden && matches(goal.start, kind, argument)) {
			setState(goal, kActive);
			changed = true;
		}
		// A goal can be completed without ever having been announced
		if (goal.state != kDone && matches(goal.done, kind, argument)) {
			const bool announced = goal.state == kActive;
			if (announced)
				setState(goal, kDone);
			else
				goal.state = kDone;
			changed = true;
			// Goals waiting on this one; _goals is not resized, so recursion is safe
			fire("goal", goal.id);
		}
	}
	return changed;
}

bool NeuroJournal::heard(const Common::String &line) {
	return fire("heard", line);
}

bool NeuroJournal::observed(int room, const Common::StringArray &inventory) {
	bool changed = fire("room", Common::String::format("%d", room));
	for (const auto &item : inventory)
		changed |= fire("have", item);
	return changed;
}

Common::String NeuroJournal::takeNews() {
	Common::String news = _news;
	_news.clear();
	return news;
}

void NeuroJournal::addNote(const Common::String &text) {
	Common::String note = text;
	note.trim();
	if (note.size() > kMaxNoteLength)
		note = note.substr(0, kMaxNoteLength);
	for (uint i = 0; i < note.size(); i++) {
		if (note[i] == '\n' || note[i] == '\r')
			note.setChar(' ', i);
	}
	if (note.empty())
		return;
	_notes.push_back(note);
	while (_notes.size() > kMaxNotes)
		_notes.remove_at(0);
}

bool NeuroJournal::isEmpty() const {
	for (const auto &goal : _goals) {
		if (goal.state != kHidden && !goal.nudge)
			return false;
	}
	return _notes.empty();
}

Common::String NeuroJournal::describeClues(const Common::String &which) const {
	Common::String wanted = which;
	wanted.toLowercase();
	wanted.trim();

	// By id, by title, or by any words of the goal itself; the first unfinished one that fits
	const Goal *match = nullptr;
	Common::String titles;
	for (int pass = 0; pass < 2 && !match; pass++) {
		for (const auto &goal : _goals) {
			if (goal.nudge || goal.state != kActive)
				continue;
			Common::String title = goalTitle(goal.id), text = goal.text;
			if (pass == 0)
				titles += (titles.empty() ? "" : ", ") + title;
			title.toLowercase();
			text.toLowercase();
			const bool fits = pass == 0 ? (wanted == goal.id || wanted == title) : (title.contains(wanted) || wanted.contains(title) || text.contains(wanted));
			if (fits && !wanted.empty()) {
				match = &goal;
				break;
			}
		}
	}
	if (!match)
		return titles.empty() ? Common::String("You have no goals yet.") : "Say which goal: " + titles + ".";

	Common::String text = "## Clues: " + goalTitle(match->id) + "\n\n";
	bool any = false;
	for (const auto &clue : _clues) {
		if (clue.found && clue.goal == match->id) {
			text += "- " + clue.text + "\n";
			any = true;
		}
	}
	return any ? text : text + "None noted yet.\n";
}

Common::String NeuroJournal::describe(bool withCompleted) const {
	Common::String active, done;
	uint hiddenClues = 0;
	for (const auto &goal : _goals) {
		if (goal.nudge)
			continue;
		if (goal.state == kActive) {
			uint clues = 0;
			for (const auto &clue : _clues)
				clues += (clue.found && clue.goal == goal.id) ? 1 : 0;
			active += "- " + goal.text;
			if (clues)
				active += Common::String::format(" (%u clue%s)", clues, clues == 1 ? "" : "s");
			active += "\n";
			hiddenClues += clues;
		} else if (goal.state == kDone)
			done += "- " + goal.text + "\n";
	}

	Common::String text = "## Your journal\n\n";
	text += "Current goals:\n" + (active.empty() ? Common::String("- none yet\n") : active);
	if (hiddenClues)
		text += "(To read a goal's clues, set clues_for to the goal.)\n";
	// Finished goals are left out unless asked for: seeing them invites doing them again
	if (withCompleted)
		text += "\nCompleted goals:\n" + (done.empty() ? Common::String("- none yet\n") : done);
	if (!_notes.empty()) {
		text += "\nYour notes:\n";
		for (const auto &note : _notes)
			text += "- " + note + "\n";
	}
	return text;
}

void NeuroJournal::save(const Common::String &fileName) const {
	Common::OutSaveFile *file = g_system->getSavefileManager()->openForSaving(fileName, false);
	if (!file)
		return;
	for (const auto &goal : _goals) {
		if (goal.state != kHidden)
			file->writeString(Common::String::format("goal %s %d\n", goal.id.c_str(), (int)goal.state));
	}
	for (const auto &clue : _clues) {
		if (clue.found)
			file->writeString("clue " + clue.id + "\n");
	}
	for (const auto &note : _notes)
		file->writeString("note " + note + "\n");
	for (const auto &memo : memos)
		file->writeString("memo " + memo._key + " " + memo._value + "\n");
	file->finalize();
	delete file;
}

void NeuroJournal::load(const Common::String &fileName) {
	reset();
	Common::InSaveFile *file = g_system->getSavefileManager()->openForLoading(fileName);
	if (!file)
		return;

	// Goals that are active from the start may have been completed in the save
	while (!file->eos()) {
		Common::String line = file->readLine();
		if (line.hasPrefix("clue ")) {
			for (auto &clue : _clues) {
				if (clue.id == line.substr(5))
					clue.found = true;
			}
		} else if (line.hasPrefix("note ")) {
			_notes.push_back(line.substr(5));
		} else if (line.hasPrefix("memo ")) {
			const size_t space = line.findFirstOf(' ', 5);
			if (space != Common::String::npos)
				memos[line.substr(5, space - 5)] = line.substr(space + 1);
		} else if (line.hasPrefix("goal ")) {
			const size_t space = line.findLastOf(' ');
			const Common::String id = line.substr(5, space - 5);
			const int state = atoi(line.c_str() + space + 1);
			for (auto &goal : _goals) {
				if (goal.id == id && state >= kHidden && state <= kDone)
					goal.state = (State)state;
			}
		}
	}
	delete file;
}

} // End of namespace Scumm
