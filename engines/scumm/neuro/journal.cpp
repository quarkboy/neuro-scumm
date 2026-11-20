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
#include "common/tokenizer.h"
#include "common/system.h"

namespace Scumm {

static const uint kNotesPerPage = 10;
static const uint kMaxNoteLength = 300;

static int readBit(const Common::INIFile::Section &section, const char *key) {
	const Common::INIFile::KeyValue *kv = section.getKey(key);
	if (!kv)
		return -1;
	char *end;
	long bit = strtol(kv->value.c_str(), &end, 10);
	return end != kv->value.c_str() && !*end && bit >= 0 && bit <= 0x7fff ? bit : -1;
}

void NeuroJournal::trackStates(const Common::String &triggers) {
	Common::StringTokenizer parts(triggers, "|");
	while (!parts.empty()) {
		Common::String part = parts.nextToken();
		part.trim();
		part.toLowercase();
		int object, state;
		char extra;
		if (sscanf(part.c_str(), "state: %d %d %c", &object, &state, &extra) == 2 && object > 0 && state >= 0)
			_objectStates[object] = -1;
	}
}

void NeuroJournal::loadGoals(const Common::INIFile &ini) {
	_objectStates.clear();
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
			if (const Common::INIFile::KeyValue *kv = section.getKey("superseded_by"))
				clue.supersededBy = kv->value;
			clue.supersededBy.trim();
			if (const Common::INIFile::KeyValue *kv = section.getKey("hide_once"))
				clue.hideOnce = kv->value;
			clue.whenBit = readBit(section, "when_bit");
			clue.hideBit = readBit(section, "hide_bit");
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
		const Common::INIFile::KeyValue *panel = section.getKey("panel");
		goal.onPanel = !(panel && panel->value.equalsIgnoreCase("no"));
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
	for (const auto &goal : _goals) {
		trackStates(goal.start);
		trackStates(goal.done);
	}
	for (const auto &clue : _clues) {
		trackStates(clue.when);
		trackStates(clue.hideOnce);
	}
	reset();
}

void NeuroJournal::reset() {
	for (auto &entry : _objectStates)
		entry._value = -1;
	for (auto &goal : _goals)
		goal.state = (goal.start.empty() && !goal.nudge) ? kActive : kHidden;
	for (auto &clue : _clues) {
		clue.found = false;
		clue.retired = false;
	}
	_notes.clear();
	_lastNoteId = 0;
	_news.clear();
	memos.clear();
}

void NeuroJournal::setState(Goal &goal, State state) {
	if (goal.state == state)
		return;
	goal.state = state;
	if (goal.nudge)
		_news.push_back({goal.text, nullptr});
	else
		_news.push_back({(state == kActive ? "New goal: " : "Goal completed: ") + goal.text, nullptr});
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

	if (kind == "room" || kind == "goal" || kind == "seen" || kind == "state")
		return wantArgument.equalsIgnoreCase(argument);
	if (wantArgument.hasPrefix("="))
		return argument.equalsIgnoreCase(wantArgument.substr(1));

	Common::String haystack = argument;
	haystack.toLowercase();
	wantArgument.toLowercase();
	return haystack.contains(wantArgument);
}

Common::String NeuroJournal::expand(const Common::String &text) const {
	return _expand ? _expand(_expandContext, text) : text;
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

bool NeuroJournal::listed(const Clue &clue) const {
	if (clue.retired)
		return false;
	if (clue.found && !clue.supersededBy.empty()) {
		for (const auto &other : _clues) {
			if (other.found && other.id == clue.supersededBy)
				return false;
		}
	}
	return clue.found;
}

void NeuroJournal::noteClue(Clue &clue) {
	clue.found = true;
	if (listed(clue))
		_news.push_back({"Clue noted (" + goalTitle(clue.goal) + "): " + expand(clue.text), &clue});
}

void NeuroJournal::fire(const Common::String &kind, const Common::String &argument) {
	for (auto &clue : _clues) {
		if (!clue.retired && !clue.hideOnce.empty() && matches(clue.hideOnce, kind, argument))
			clue.retired = true;
		if (!clue.found && matches(clue.when, kind, argument))
			noteClue(clue);
	}
	for (uint i = 0; i < _goals.size(); i++) {
		Goal &goal = _goals[i];
		if (goal.state == kHidden && matches(goal.start, kind, argument))
			setState(goal, kActive);
		// A goal can be completed without ever having been announced
		if (goal.state != kDone && matches(goal.done, kind, argument)) {
			if (goal.state == kActive)
				setState(goal, kDone);
			else
				goal.state = kDone;
			// Goals waiting on this one; _goals is not resized, so recursion is safe
			fire("goal", goal.id);
		}
	}
}

void NeuroJournal::heard(const Common::String &line) {
	fire("heard", line);
}

void NeuroJournal::observed(int room, const Common::StringArray &inventory) {
	fire("room", Common::String::format("%d", room));
	for (const auto &item : inventory)
		fire("have", item);
}

void NeuroJournal::sawObject(int room, int object) {
	fire("seen", Common::String::format("%d %d", room, object));
}

Common::String NeuroJournal::takeNews() {
	Common::String news;
	for (const auto &entry : _news) {
		if (entry.clue) {
			if (!listed(*entry.clue))
				continue;
			bool finished = false;
			for (const auto &goal : _goals) {
				if (goal.id == entry.clue->goal) {
					finished = goal.state == kDone;
					break;
				}
			}
			if (finished)
				continue;
		}
		news += entry.text + "\n";
	}
	_news.clear();
	return news;
}

static Common::String noteText(const Common::String &text) {
	Common::String note = text;
	note.trim();
	if (note.size() > kMaxNoteLength)
		note = note.substr(0, kMaxNoteLength);
	for (uint i = 0; i < note.size(); i++) {
		if (note[i] == '\n' || note[i] == '\r')
			note.setChar(' ', i);
	}
	return note;
}

void NeuroJournal::addNote(const Common::String &text) {
	_notes.push_back({++_lastNoteId, noteText(text)});
}

bool NeuroJournal::editNote(uint id, const Common::String &text) {
	for (auto &note : _notes) {
		if (note.id == id) {
			note.text = noteText(text);
			return true;
		}
	}
	return false;
}

bool NeuroJournal::deleteNote(uint id) {
	for (uint i = 0; i < _notes.size(); i++) {
		if (_notes[i].id == id) {
			_notes.remove_at(i);
			return true;
		}
	}
	return false;
}

uint NeuroJournal::notesPages() const {
	return _notes.empty() ? 1 : 1 + (_notes.size() - 1) / kNotesPerPage;
}

Common::StringArray NeuroJournal::panelGoals() const {
	Common::StringArray result;
	for (const auto &goal : _goals) {
		if (goal.state == kActive && !goal.nudge && goal.onPanel)
			result.push_back(goal.text);
	}
	return result;
}

Common::String NeuroJournal::describeClues(const Common::String &which) const {
	Common::String wanted = which;
	wanted.toLowercase();
	wanted.trim();

	// Completed goals can be requested by id or title. Keep suggestions and partial matches current.
	const Goal *match = nullptr;
	Common::String titles;
	for (int pass = 0; pass < 2 && !match; pass++) {
		for (const auto &goal : _goals) {
			if (goal.nudge || goal.state == kHidden || (pass != 0 && goal.state != kActive))
				continue;
			Common::String title = goalTitle(goal.id), text = goal.text;
			if (pass == 0 && goal.state == kActive)
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
		if (clue.goal == match->id && listed(clue)) {
			text += "- " + expand(clue.text) + "\n";
			any = true;
		}
	}
	return any ? text : text + "None noted yet.\n";
}

Common::String NeuroJournal::describe(bool withCompleted, uint notesPage) const {
	Common::String active, done;
	uint hiddenClues = 0;
	for (const auto &goal : _goals) {
		if (goal.nudge)
			continue;
		if (goal.state == kActive) {
			uint clues = 0;
			for (const auto &clue : _clues)
				clues += (clue.goal == goal.id && listed(clue)) ? 1 : 0;
			active += "- " + goal.text;
			if (clues)
				active += Common::String::format(" (%u clue%s)", clues, clues == 1 ? "" : "s");
			active += "\n";
			hiddenClues += clues;
		} else if (goal.state == kDone && withCompleted)
			done += "- " + goalTitle(goal.id) + ": " + goal.text + "\n";
	}

	Common::String text = "## Your journal\n\n";
	text += "Current goals:\n" + (active.empty() ? Common::String("- none yet\n") : active);
	if (hiddenClues)
		text += "(To read a goal's clues, set clues_for to the goal.)\n";
	// Finished goals are left out unless asked for: seeing them invites doing them again
	if (withCompleted)
		text += "\nCompleted goals:\n" + (done.empty() ? Common::String("- none yet\n") : done);
	if (!_notes.empty() && notesPage >= 1 && notesPage <= notesPages()) {
		text += "\nYour own notes (newest first). Plans may already be done. Use edit_note or delete_note with the note ID to keep them current.\n";
		const uint first = (notesPage - 1) * kNotesPerPage;
		const uint count = MIN<uint>(kNotesPerPage, _notes.size() - first);
		for (uint i = 0; i < count; i++) {
			const Note &note = _notes[_notes.size() - 1 - first - i];
			text += Common::String::format("- [#%u] %s\n", note.id, note.text.c_str());
		}
		text += Common::String::format("Showing %u-%u of %u notes.", first + 1, first + count, _notes.size());
		if (notesPage < notesPages())
			text += Common::String::format(" Use notes_page: %u for older notes.", notesPage + 1);
		text += "\n";
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
		if (clue.retired)
			file->writeString("retired " + clue.id + "\n");
	}
	file->writeString(Common::String::format("last_note_id %u\n", _lastNoteId));
	for (const auto &note : _notes)
		file->writeString(Common::String::format("note_id %u %s\n", note.id, note.text.c_str()));
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
		if (line.hasPrefix("clue ") || line.hasPrefix("retired ")) {
			const bool retired = line.hasPrefix("retired ");
			const Common::String id = line.substr(retired ? 8 : 5);
			for (auto &clue : _clues) {
				if (clue.id == id) {
					if (retired)
						clue.retired = true;
					else
						clue.found = true;
				}
			}
		} else if (line.hasPrefix("note ")) {
			// Older sidecars stored personal notes in creation order without IDs.
			_notes.push_back({++_lastNoteId, line.substr(5)});
		} else if (line.hasPrefix("last_note_id ")) {
			_lastNoteId = MAX(_lastNoteId, (uint)strtoul(line.c_str() + 13, nullptr, 10));
		} else if (line.hasPrefix("note_id ")) {
			char *end;
			const uint id = strtoul(line.c_str() + 8, &end, 10);
			if (id && *end == ' ') {
				_notes.push_back({id, end + 1});
				_lastNoteId = MAX(_lastNoteId, id);
			}
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
	// A newer annotation may depend on a goal already completed in this save.
	for (const auto &goal : _goals) {
		if (goal.state == kDone)
			fire("goal", goal.id);
	}
	_news.clear();
}

} // End of namespace Scumm
