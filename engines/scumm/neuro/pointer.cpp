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

// Simulated mouse clicks, and the pointer travelling to them.

#include "scumm/neuro/neuro.h"

#include "common/system.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/scumm.h"
#include "scumm/verbs.h"

namespace Scumm {

// How long the pointer takes to travel: a fixed part, 2 ms a pixel, and a limit
static const uint32 kPointerStartMs = 140;
static const uint32 kPointerMaxMs = 600;
/** The pointer rests this long where it arrives before the button goes down. */
static const uint32 kPointerRestMs = 100;
/** Frames the pointer is held over a target before the click, for the game's hover script to see it. */
static const int kHoverFrames = 3;

// Mirrors the button state flags private to input.cpp
enum {
	kMouseDown = 1,
	kMouseClicked = 2
};

bool NeuroBridge::findClickPoint(int thing, int &x, int &y) const {
	if (_vm->objIsActor(thing)) {
		const Actor *actor = _vm->_actors[_vm->objToActor(thing)];
		x = actor->getPos().x;
		y = (actor->_top + actor->_bottom) / 2;
		return true;
	}

	for (int i = 1; i < _vm->_numLocalObjects; i++) {
		const ObjectData &od = _vm->_objs[i];
		if (od.obj_nr != thing)
			continue;
		// Another object may overlap this one, so probe a few points
		const int fractions[] = {2, 1, 3};
		for (int row = 0; row < 3; row++) {
			for (int col = 0; col < 3; col++) {
				x = od.x_pos + (od.width - 1) * fractions[col] / 4;
				y = od.y_pos + (od.height - 1) * fractions[row] / 4;
				if (_vm->findObject(x, y) == thing)
					return true;
			}
		}
	}
	return false;
}

void NeuroBridge::clickInRoom(int x, int y) {
	NeuroClick click;
	click.x = x;
	click.y = y;
	clickAt(click);
}

void NeuroBridge::clickAt(const NeuroClick &click) {
	_clickX = click.x;
	_clickY = click.y;
	_clickScreen = click.screen;
	_clickFollow = click.follow;
	_clickPress = click.press;
	_clickFrames = 0;
	if (!_pointerGlide || _fastMode == kFastAlways) {
		_clickPhase = kClickHover;
		return;
	}

	if (!_pointerKnown) {
		_pointerX = _vm->_mouse.x;
		_pointerY = _vm->_mouse.y;
		_pointerKnown = true;
	}
	_moveFromX = _pointerX;
	_moveFromY = _pointerY;
	int x, y;
	clickTarget(x, y);
	const int distance = (int)sqrt((double)((x - _moveFromX) * (x - _moveFromX) + (y - _moveFromY) * (y - _moveFromY)));
	// A short hop is quick, a trip across the screen takes about half a second
	_moveMs = MIN<uint32>(kPointerStartMs + distance * 2, kPointerMaxMs);
	_moveStartedAt = g_system->getMillis();
	_clickPhase = kClickMove;
}

/** Where on the screen the current click is aimed. */
void NeuroBridge::clickTarget(int &x, int &y) const {
	const VirtScreen &vs = _vm->_virtscr[kMainVirtScreen];
	if (_clickScreen) {
		x = _clickX;
		y = _clickY;
		return;
	}
	x = CLIP<int>(_clickX - vs.xstart, 0, _vm->_screenWidth - 1);
	y = CLIP<int>(_clickY + vs.topline, vs.topline, vs.topline + vs.h - 1);
}

/** Where the travelling pointer is at this moment. */
void NeuroBridge::pointerNow(int &x, int &y) const {
	clickTarget(x, y);
	if (_clickPhase != kClickMove || !_moveMs)
		return;
	const float t = MIN(1.0f, (float)(g_system->getMillis() - _moveStartedAt) / _moveMs);
	// Starts and stops gently, as a hand does
	const float eased = t * t * (3 - 2 * t);
	x = _moveFromX + (int)((x - _moveFromX) * eased);
	y = _moveFromY + (int)((y - _moveFromY) * eased);
}

void NeuroBridge::movePointer() {
	if (_clickPhase == kClickIdle || !_pointerGlide)
		return;
	if (_clickFollow)
		findClickPoint(_clickFollow, _clickX, _clickY);
	int x, y;
	pointerNow(x, y);
	if (x != _pointerX || y != _pointerY || !_pointerKnown) {
		_pointerX = x;
		_pointerY = y;
		_pointerKnown = true;
		g_system->warpMouse(x, y);
	}
}

bool NeuroBridge::needsClicks() const {
	// Scripts numbered past the global ones belong to the current room
	return _vm->VAR(_vm->VAR_VERB_SCRIPT) >= _vm->_numGlobalScripts;
}

bool NeuroBridge::verbButton(int verbId, NeuroClick &click) const {
	for (int slot = 1; slot < _vm->_numVerbs; slot++) {
		const VerbSlot &vs = _vm->_verbs[slot];
		if (vs.verbid != verbId || vs.curmode != 1 || vs.saveid != 0 || vs.curRect.isEmpty())
			continue;
		click.x = (vs.curRect.left + vs.curRect.right) / 2;
		click.y = (vs.curRect.top + vs.curRect.bottom) / 2;
		click.screen = true;
		return true;
	}
	return false;
}

/**
 * Turns a sentence into the clicks a player would make: the verb, then each
 * thing, in the inventory or in the room.
 */
bool NeuroBridge::planClicks(const NeuroWorld &world, NeuroCommand &command) {
	// Monkey Island's inventory: picture buttons 200 and up, filled in order
	static const int kFirstInventoryButton = 200;

	command.clicks.clear();
	NeuroClick click;
	if (!verbButton(command.verb, click))
		return false;
	command.clicks.push_back(click);

	for (const int thing : command.objects) {
		if (!thing)
			break;
		int index = -1;
		for (uint i = 0; i < world.inventory.size(); i++) {
			if (world.inventory[i].id == thing)
				index = i;
		}
		if (index >= 0) {
			// Eight things carried are shown at a time, the newest by default. The
			// game keeps which object each button stands for in variables 133 to
			// 140, and buttons 208 and 209 scroll a row up and down.
			static const int kShownVar = 133, kShown = 8, kScrollUp = 208, kScrollDown = 209;
			int button = -1;
			for (int pass = 0; pass < 24 && button < 0; pass++) {
				for (int i = 0; i < kShown; i++) {
					if (_vm->readVar(kShownVar + i) == thing)
						button = i;
				}
				// First all the way up, then down a row at a time
				if (button < 0)
					_vm->runInputScript(kVerbClickArea, pass < 8 ? kScrollUp : kScrollDown, 1);
			}
			if (button < 0 || !verbButton(kFirstInventoryButton + button, click))
				return false;
		} else {
			int x, y;
			if (!findClickPoint(thing, x, y))
				return false;
			// Only what is on screen can be clicked
			const int screenX = x - _vm->_virtscr[kMainVirtScreen].xstart;
			if (screenX < 0 || screenX >= _vm->_screenWidth)
				return false;
			click.x = x;
			click.y = y;
			click.screen = false;
			click.follow = _vm->objIsActor(thing) ? thing : 0;
		}
		command.clicks.push_back(click);
		click.follow = 0;
	}
	return true;
}

void NeuroBridge::updateClick() {
	movePointer();
	switch (_clickPhase) {
	case kClickMove:
		// A moment's rest on arrival, then the button, or on to the next word
		if (g_system->getMillis() - _moveStartedAt < _moveMs + kPointerRestMs)
			break;
		if (_clickPress) {
			_clickPhase = kClickHover;
			break;
		}
		_clickPhase = kClickIdle;
		if (!_clickQueue.empty()) {
			clickAt(_clickQueue.remove_at(0));
		} else if (_shownSentence) {
			_shownSentence = false;
			_commandStartedAt = g_system->getMillis();
			_vm->doSentence(_shownVerb, _shownObjects[0], _shownObjects[1]);
		}
		break;
	case kClickHover:
		if (++_clickFrames >= kHoverFrames)
			_clickPhase = kClickPress;
		break;
	case kClickPress:
		_vm->_leftBtnPressed |= kMouseClicked | kMouseDown;
		_clickPhase = kClickRelease;
		break;
	case kClickRelease:
		_vm->_leftBtnPressed &= ~kMouseDown;
		_clickPhase = kClickIdle;
		_lastClickAt = g_system->getMillis();
		// The next click of the same command, once the game has taken this one in
		if (!_clickQueue.empty()) {
			clickAt(_clickQueue.remove_at(0));
			_clickFrames = -kHoverFrames;
		}
		break;
	default:
		break;
	}
}

void NeuroBridge::overridePointer() {
	if (_clickPhase == kClickIdle)
		return;

	// The target may be scrolled out of view, so only the room position is
	// exact; the screen position just has to be somewhere over the room.
	const VirtScreen &vs = _vm->_virtscr[kMainVirtScreen];
	if (_clickFollow)
		findClickPoint(_clickFollow, _clickX, _clickY);
	if (_clickPhase == kClickMove) {
		// On its way: the game sees the pointer where it is drawn
		int x, y;
		pointerNow(x, y);
		_vm->_mouse.x = x;
		_vm->_mouse.y = y;
		_vm->_virtualMouse.x = x + vs.xstart;
		_vm->_virtualMouse.y = (y >= vs.topline && y < vs.topline + vs.h) ? y - vs.topline : -1;
		return;
	}
	if (_clickScreen) {
		// On the verbs or the inventory, below the room
		_vm->_mouse.x = _clickX;
		_vm->_mouse.y = _clickY;
		_vm->_virtualMouse.x = _clickX + vs.xstart;
		_vm->_virtualMouse.y = (_clickY >= vs.topline && _clickY < vs.topline + vs.h) ? _clickY - vs.topline : -1;
		return;
	}
	_vm->_virtualMouse.x = _clickX;
	_vm->_virtualMouse.y = _clickY;
	_vm->_mouse.x = CLIP<int>(_clickX - vs.xstart, 0, _vm->_screenWidth - 1);
	_vm->_mouse.y = CLIP<int>(_clickY + vs.topline, vs.topline, vs.topline + vs.h - 1);
}

} // End of namespace Scumm
