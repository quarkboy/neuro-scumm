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

// Conversations: the lines she can say, scrolling through a long list of them, and prompting her to pick.

#include "scumm/neuro/neuro.h"

#include "common/system.h"
#include "common/textconsole.h"

#include "scumm/resource.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

/** A force that was never answered stops blocking the next one after this long. */
static const uint32 kForceTimeoutMs = 90000;
/** The list moves within this long of an arrow being pressed. */
static const uint32 kScrollTakesMs = 1000;
/** The line she picked can stay on screen for this long. */
static const uint32 kChoiceLingersMs = 1000;
static const uint32 kSameLinesAgainMs = 3000;
/** How long the last line said to her is still what her choice of line answers. */
static const uint32 kLastHeardMs = 30000;

static Common::String listDialogue(const NeuroThingList &lines) {
	// What is noted about lines comes first, once per note, with the numbers
	// it goes for ("4, 5, 8, 10-14"): shorter than repeating it on every line
	Common::String text;
	Common::StringArray notes;
	for (const auto &line : lines) {
		if (!line.note.empty() && Common::find(notes.begin(), notes.end(), line.note) == notes.end())
			notes.push_back(line.note);
	}
	for (const auto &note : notes) {
		Common::String numbers;
		for (uint i = 0; i < lines.size(); i++) {
			if (lines[i].note != note)
				continue;
			uint last = i;
			while (last + 1 < lines.size() && lines[last + 1].note == note)
				last++;
			if (!numbers.empty())
				numbers += ", ";
			numbers += last > i + 1 ? Common::String::format("%u-%u", i + 1, last + 1) : Common::String::format("%u", i + 1);
			if (last == i + 1)
				numbers += Common::String::format(", %u", last + 1);
			i = last;
		}
		text += note + ": " + numbers + "\n";
	}
	if (!notes.empty())
		text += "\n";
	for (uint i = 0; i < lines.size(); i++)
		text += Common::String::format("%u. %s\n", i + 1, lines[i].name.c_str());
	return text;
}

void NeuroBridge::scrollArrows(const NeuroWorld &world, int &up, int &down) {
	up = down = 0;
	if (!world.inDialogue || world.verbs.empty())
		return;
	int firstLine = 0x7FFF;
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (vs.verbid == world.verbs[0].id)
			firstLine = vs.curRect.top;
	}
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (!vs.verbid || vs.saveid != 0 || vs.curmode != 1 || vs.type != kTextVerbType)
			continue;
		if (!cleanText(_vm->getResourceAddress(rtVerb, slot), true).empty())
			continue;
		// An arrow has no text; the one level with the first line scrolls up
		if (vs.curRect.top <= firstLine + 4)
			up = vs.verbid;
		else
			down = vs.verbid;
	}
}

bool NeuroBridge::pagerCovers(const NeuroWorld &world) const {
	if (_pagerLines.empty() || world.verbs.empty())
		return false;
	for (const auto &verb : world.verbs) {
		bool listed = false;
		for (const auto &line : _pagerLines)
			listed |= line.name == verb.name;
		if (!listed)
			return false;
	}
	return true;
}

void NeuroBridge::pressArrow(int verb, const NeuroWorld &world) {
	_pagerSaw = world.verbs[0].name;
	_pagerPressedAt = g_system->getMillis();
	_vm->runInputScript(kVerbClickArea, verb, 1);
}

void NeuroBridge::resetPager() {
	_pagerState = kPagerIdle;
	_pagerLines.clear();
}

void NeuroBridge::updatePager() {
	if (_pagerState == kPagerIdle)
		return;

	NeuroWorld world;
	collectWorld(world);
	if (!playerHasControl() || !world.inDialogue || world.verbs.empty()) {
		resetPager();
		return;
	}
	// The list moves a frame or so after an arrow is pressed
	const uint32 sincePress = g_system->getMillis() - _pagerPressedAt;
	if (_pagerPressedAt && world.verbs[0].name == _pagerSaw && sincePress < kScrollTakesMs)
		return;
	_pagerPressedAt = 0;

	int up, down;
	scrollArrows(world, up, down);

	if (_pagerState == kPagerToTop) {
		if (up) {
			pressArrow(up, world);
			return;
		}
		_pagerLines.clear();
		_pagerState = kPagerDown;
	}
	if (_pagerState == kPagerDown) {
		for (const auto &verb : world.verbs) {
			bool listed = false;
			for (const auto &line : _pagerLines)
				listed |= line.name == verb.name;
			if (!listed)
				_pagerLines.push_back(verb);
		}
		if (down)
			pressArrow(down, world);
		else
			_pagerState = kPagerIdle;
		return;
	}

	// Seeking the line she chose
	for (const auto &verb : world.verbs) {
		if (verb.name == _pagerTarget) {
			resetPager();
			_commandStartedAt = g_system->getMillis();
			pickLine(verb.id);
			return;
		}
	}
	uint target = 0, first = 0;
	for (uint i = 0; i < _pagerLines.size(); i++) {
		if (_pagerLines[i].name == _pagerTarget)
			target = i;
		if (_pagerLines[i].name == world.verbs[0].name)
			first = i;
	}
	const int arrow = target < first ? up : down;
	if (!arrow) {
		warning("Neuro: Could not scroll to the line '%s'", _pagerTarget.c_str());
		resetPager();
		return;
	}
	pressArrow(arrow, world);
}

void NeuroBridge::actionChooseDialogue(const Common::String &id, const NeuroArgs &args) {
	NeuroWorld world;
	collectWorld(world);
	if (!playerHasControl() || !world.inDialogue || _pagerState != kPagerIdle) {
		sendActionResult(id, false, "Nothing to choose now.");
		return;
	}
	// The whole list, where only part of it is on screen
	int up, down;
	scrollArrows(world, up, down);
	const bool paged = (up || down) && pagerCovers(world);
	if (paged)
		world.verbs = _pagerLines;

	// A number, possibly dressed up ("option 3", "3."), or the line itself
	const Common::String choice = args.get("option|choice|line|number|index|id|text", true);
	// A line that has a number in it ("I have 2 questions") is still a line
	Common::String words;
	for (uint i = 0; i < choice.size(); i++) {
		if (Common::isAlpha(choice[i]))
			words += tolower(choice[i]);
	}
	const bool plainNumber = words.empty() || words == "option" || words == "choice" || words == "line" || words == "number" || words == "no";
	int option = 0;
	if (!plainNumber) {
		const NeuroThing *line = findThing(world.verbs, choice);
		for (uint i = 0; line && i < world.verbs.size(); i++) {
			if (&world.verbs[i] == line)
				option = i + 1;
		}
	}
	for (uint i = 0; i < choice.size() && !option; i++) {
		if (Common::isDigit(choice[i]))
			option = atoi(choice.c_str() + i);
	}
	if (option < 1 || option > (int)world.verbs.size()) {
		sendActionResult(id, false, "Pick a line by number:\n" + listDialogue(world.verbs));
		return;
	}

	sendActionResult(id, true);
	// What is said next is the reply to her line, not something to react to separately
	beginAction();
	// Answered; it is not what her next choice is about
	_lastHeard.clear();
	if (paged) {
		_pagerTarget = world.verbs[option - 1].name;
		_pagerState = kPagerSeek;
		return;
	}
	_pagerLines.clear();
	pickLine(world.verbs[option - 1].id);
}

void NeuroBridge::pickLine(int verb) {
	// A room can work out the chosen line from where the pointer is, so a
	// real click on the line, as a player makes, is the way that always works
	NeuroClick click;
	if (verbButton(verb, click)) {
		clickAt(click);
		return;
	}
	_vm->runInputScript(kVerbClickArea, verb, 1);
}

void NeuroBridge::updateForces(const NeuroWorld &world) {
	const uint32 now = g_system->getMillis();

	if (_forceActive && now - _forceSentAt > kForceTimeoutMs)
		_forceActive = false;

	if (world.inDialogue) {
		if (_pagerState != kPagerIdle)
			return;
		int up, down;
		scrollArrows(world, up, down);
		const bool covered = (up || down) && pagerCovers(world);
		if ((up || down) && !covered) {
			// Not while her last choice is still on screen, or the pointer is on its way to it
			if (!_forceActive && now - _lastActionAt > kChoiceLingersMs && _clickPhase == kClickIdle && now - _lastClickAt > kChoiceLingersMs) {
				_pagerPressedAt = 0;
				_pagerState = kPagerToTop;
			}
			return;
		}
		if (!covered)
			_pagerLines.clear();
		const Common::String lines = listDialogue(covered ? _pagerLines : world.verbs);
		// Not a second force while one is outstanding, and not the same lines
		// again while the one she picked is still on screen
		const bool fresh = lines != _forcedDialogue || now - _lastActionAt > kSameLinesAgainMs;
		if (!_forceActive && fresh) {
			_forcedDialogue = lines;
			// The line she is answering goes with the choice, so it cannot be missed
			Common::String state = lines;
			if (!_lastHeard.empty() && now - _lastHeardAt < kLastHeardMs)
				state = "Last said to you: " + _lastHeard + "\n\n" + lines;
			Common::String query = expandText(_data.roomPrompt(world.room));
			if (query.empty())
				query = "Pick a line to say.";
			sendForce(state, query, "choose_dialogue");
		}
		return;
	}
	_forcedDialogue.clear();
	_pagerLines.clear();

	if (_idleForceMs && !_forceActive && now - _lastActivity > _idleForceMs) {
		_lastActivity = now;
		sendForce(describeRoom(world, kRoomStatus), "Your move.", "interact");
	}
}

} // End of namespace Scumm
