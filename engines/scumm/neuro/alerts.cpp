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

// Moments the description file says she must not miss, and game variables put into words.

#include "scumm/neuro/neuro.h"

#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "scumm/scumm.h"

namespace Scumm {

/** An alert that could not go out within this long of its moment is dropped. */
static const uint32 kAlertLateMs = 1500;

static bool containsName(const NeuroThingList &things, const Common::String &name) {
	for (const auto &thing : things) {
		Common::String candidate = thing.name;
		candidate.toLowercase();
		// "=rock" is the rock itself, not the heap o' rocks beside it
		if (name.hasPrefix("=") ? candidate == name.substr(1) : candidate.contains(name))
			return true;
	}
	return false;
}

/** Whether the situation an alert is about holds in the given world. */
static bool alertHolds(const NeuroData::Alert &alert, const NeuroWorld &world) {
	if (alert.kind == "arrived" || alert.kind == "left")
		return containsName(world.actors, alert.name) == (alert.kind == "arrived");
	if (alert.kind == "appeared" || alert.kind == "disappeared")
		return containsName(world.objects, alert.name) == (alert.kind == "appeared");
	if (alert.kind == "entered")
		return true;
	if (alert.kind == "gained" || alert.kind == "lost")
		return containsName(world.inventory, alert.name) == (alert.kind == "gained");
	return false;
}

/** Fills in {exit}; false if the alert needs it and nobody was seen leaving. */
static bool fillExit(Common::String &text, const Common::String &exit) {
	const size_t at = text.find("{exit}");
	if (at == Common::String::npos)
		return true;
	if (exit.empty())
		return false;
	text.replace(at, 6, exit);
	return true;
}

void NeuroBridge::fireAlert(const NeuroData::Alert &original, const NeuroWorld &world) {
	NeuroData::Alert alert = original;
	alert.say = expandText(alert.say);
	Common::String exit;
	for (const auto &left : _leftAt) {
		if (alert.kind == "left" && left._key.contains(alert.name))
			exit = left._value;
	}
	if (!fillExit(alert.say, exit) || !fillExit(alert.command, exit))
		return;

	if (alert.ask.empty()) {
		sendContext(alert.say, false);
		return;
	}
	// Only one force may be outstanding; otherwise say it as plainly as context allows
	if (_forceActive || world.inDialogue) {
		sendContext(alert.say + " " + alert.ask, false);
	} else if (!alert.command.empty()) {
		ask(alert.say, alert.ask, alert.command, alert.then, alert.holdVar, alert.priority);
	} else {
		sendForce(alert.say, alert.ask, "interact", alert.priority);
		_lastActivity = g_system->getMillis();
	}
}

/** Asks a yes/no question; answering yes carries out the command, and then the second one. */
void NeuroBridge::ask(const Common::String &state, const Common::String &query, const Common::String &command,
		const Common::String &then, int holdVar, const Common::String &priority) {
	_questionCommand = command;
	_questionThen = then;
	_questionAskedAt = g_system->getMillis();
	_questionHoldVar = holdVar;
	if (holdVar)
		_questionHoldValue = readDataVar(holdVar);
	sendForce(state, query, "answer", priority);
	_lastActivity = _questionAskedAt;
}

void NeuroBridge::checkAlerts(const NeuroWorld &before, const NeuroWorld &after) {
	const uint32 now = g_system->getMillis();
	const Common::Array<NeuroData::Alert> &alerts = _data.alerts();

	// An alert waiting for its moment
	if (_pendingAlert >= 0) {
		const NeuroData::Alert &alert = alerts[_pendingAlert];
		if ((alert.room && alert.room != after.room) || !alertHolds(alert, after)) {
			_pendingAlert = -1;
		} else if (now > _pendingAlertDue + kAlertLateMs) {
			// She was busy when the moment came; a late prompt would only mislead
			_pendingAlert = -1;
		} else if (now >= _pendingAlertDue) {
			_pendingAlert = -1;
			fireAlert(alert, after);
		}
		return;
	}

	for (uint i = 0; i < alerts.size(); i++) {
		const NeuroData::Alert &alert = alerts[i];
		if (alert.room && alert.room != after.room)
			continue;
		if (alert.whileScript && !_vm->isScriptRunning(alert.whileScript))
			continue;
		// It has to have just become true
		if (alert.kind == "entered") {
			if (before.room == after.room)
				continue;
		} else if (alert.kind == "left") {
			bool left = false;
			for (const auto &who : _leftAt)
				left |= who._key.contains(alert.name);
			if (!left)
				continue;
		} else if (before.room != after.room || alertHolds(alert, before) || !alertHolds(alert, after)) {
			continue;
		}

		if (alert.leadMs > _reactionMs) {
			_pendingAlert = i;
			_pendingAlertDue = now + alert.leadMs - _reactionMs;
		} else {
			fireAlert(alert, after);
		}
		return;
	}
}

int NeuroBridge::readDataVar(uint var) const {
	const bool isBit = var & 0x8000;
	const uint number = var & 0x7FFF;
	// The engine stops the game on a number out of range, and reads other kinds of
	// variable from the running script
	if (number >= (uint)(isBit ? _vm->_numBitVariables : _vm->_numVariables)) {
		if (!_missingVars.contains(var))
			warning("Neuro: The description file names %s %u, which this game does not have", isBit ? "bit variable" : "variable", number);
		_missingVars[var] = true;
		return 0;
	}
	return _vm->readVar(var);
}

Common::String NeuroBridge::expandText(const Common::String &text) const {
	Common::String result = text;
	for (;;) {
		// {bitsA-B}: how many of the game's flags A to B are set
		const size_t bits = result.find("{bits");
		if (bits != Common::String::npos && result.find('}', bits) != Common::String::npos) {
			const size_t end = result.find('}', bits);
			int from = 0, to = 0, count = 0;
			sscanf(result.c_str() + bits + 5, "%d-%d", &from, &to);
			for (int bit = from; bit <= to && bit - from < 256; bit++)
				count += readDataVar(0x8000 | bit) ? 1 : 0;
			result.replace(bits, end - bits + 1, Common::String::format("%d", count));
			continue;
		}
		size_t open = result.find("{var");
		const bool pick = open == Common::String::npos;
		if (pick)
			open = result.find("{pick");
		const size_t close = open == Common::String::npos ? open : result.find('}', open);
		if (close == Common::String::npos)
			return result;
		const int value = readDataVar(atoi(result.c_str() + open + (pick ? 5 : 4)));
		Common::String replacement = Common::String::format("%d", value);
		if (pick) {
			replacement.clear();
			const size_t colon = result.find(':', open);
			if (colon != Common::String::npos && colon < close) {
				Common::StringTokenizer options(result.substr(colon + 1, close - colon - 1), "|");
				for (int i = 1; !options.empty(); i++) {
					Common::String option = options.nextToken();
					option.trim();
					if (i == value)
						replacement = option;
				}
			}
		}
		result.replace(open, close - open + 1, replacement);
	}
}

void NeuroBridge::checkWatches() {
	Common::String text;
	for (const auto &watch : _data.watches()) {
		const int value = readDataVar(watch.var);
		// The first look only sets the starting point
		const bool changed = _watched.contains(watch.var) && (watch.anyChange ? value != _watched[watch.var] : value > _watched[watch.var]);
		if (changed) {
			Common::String say = expandText(watch.say);
			const size_t at = say.find("{value}");
			if (at != Common::String::npos)
				say.replace(at, 7, Common::String::format("%d", value));
			text += say + "\n";
		}
		_watched[watch.var] = value;
	}
	if (text.empty())
		return;
	if (_dialogueAsContext) {
		if (_pendingDialogue.empty())
			_pendingSince = g_system->getMillis();
		_pendingDialogue += text;
		_lastLineText.clear();
	} else {
		sendContext(text, true);
	}
}

} // End of namespace Scumm
