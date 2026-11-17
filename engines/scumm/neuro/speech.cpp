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

// What is said and shown as text in the game, passed on as speech or context.

#include "scumm/neuro/neuro.h"

#include "common/debug.h"
#include "common/system.h"

#include "scumm/actor.h"
#include "scumm/charset.h"
#include "scumm/scumm.h"

namespace Scumm {

/** Text that nobody speaks, up to this many bytes long, is held this long in case it is being typed out. */
static const uint kTypedOutMaxLength = 12;
static const uint32 kTypedOutHoldMs = 250;
/** The same line from the same speaker within this long is one line said by a crowd. */
static const uint32 kSameLineMs = 5000;
/** A line heard again within this long is background chatter... */
static const uint32 kChatterMemoryMs = 60000;
/** ...unless she did something this recently, which it may be a reaction to. */
static const uint32 kReactionWindowMs = 10000;

Common::String NeuroBridge::cleanText(const byte *text, bool resolveCodes) {
	if (!text)
		return Common::String();

	byte resolved[2048];
	if (resolveCodes) {
		_vm->convertMessageToString(text, resolved, sizeof(resolved));
		text = resolved;
	}

	Common::String raw;
	while (*text) {
		byte c = *text++;
		if (c == 0xFF || c == 0xFE) {
			byte code = *text;
			if (!code)
				break;
			text++;
			if (code == 1 || code == 3) {
				// Line break or new page
				raw += ' ';
			} else if (code != 2 && code != 8) {
				// Remaining codes carry a two byte argument
				for (int i = 0; i < 2 && *text; i++)
					text++;
			}
			continue;
		}
		// Names are padded with '@' to leave room for renaming
		if (c == '@' || c < 0x20)
			continue;
		if (c == '^') {
			// The game fonts draw this as an ellipsis
			raw += "...";
		} else if (c == 0xFA) {
			// Non-breaking space
			raw += ' ';
		} else {
			raw += (char)c;
		}
	}

	Common::String utf8 = _vm->_charset->convertText(raw, _vm->_language).encode();

	// Collapse runs of spaces left behind by line breaks
	Common::String result;
	for (uint i = 0; i < utf8.size(); i++) {
		if (utf8[i] == ' ' && (result.empty() || result.lastChar() == ' '))
			continue;
		result += utf8[i];
	}
	result.trim();
	return result;
}

void NeuroBridge::onTalkStarted(int actor, const byte *text) {
	_interaction.outcome.hearSpeech();

	onTalkStopped();

	const Common::String line = cleanText(text, false);
	if (line.empty() || !_ws.isConnected())
		return;

	Common::String speaker;
	if (!speakerOf(actor, line, speaker))
		return;

	// Any news waits until the conversation is over (see pollWorld)
	_journal.heard(line);
	if (actor != _vm->VAR(_vm->VAR_EGO)) {
		_lastHeard = speaker + ": \"" + line + "\"";
		_lastHeardAt = g_system->getMillis();
	}

	// Crowds shout the same line once per member; report it once
	const Common::String spoken = speaker + ": " + line;
	if (spoken == _lastLine && g_system->getMillis() - _lastTalkTime < kSameLineMs)
		return;
	_lastLine = spoken;

	if (isBackgroundChatter(spoken))
		return;

	if (_speechEndDueAt)
		sendSpeechEnded();
	_speechOpen = true;
	if (_dialogueAsContext) {
		// Text that is typed out a bit at a time ("Ps", "Pss", "Psssst.") is one line
		if (!_lastLineText.empty() && line.hasPrefix(_lastLineText) && _lastLineOffset <= _pendingDialogue.size()
				&& _pendingDialogue.substr(_lastLineOffset).hasPrefix(speaker + ": \"" + _lastLineText))
			_pendingDialogue = _pendingDialogue.substr(0, _lastLineOffset);
		_lastLineText = line;
		_lastLineOffset = _pendingDialogue.size();
		if (_pendingDialogue.empty())
			_pendingSince = g_system->getMillis();
		_pendingDialogue += Common::String::format("%s: \"%s\"\n", speaker.c_str(), line.c_str());
		return;
	}

	// A sound drawn as text can be typed out a bit at a time ("Ps", "Pss",
	// "Psssst."), each piece a line of its own. Short text that nobody speaks
	// is held for a moment, and a piece that is overtaken is never sent.
	if (_heldSpeech.waiting) {
		if (speaker == _heldSpeech.speaker && line.size() > _heldSpeech.text.size() && line.hasPrefix(_heldSpeech.text))
			_heldSpeech.waiting = false;
		else
			sendHeldSpeech();
	}

	// Lines she is only watching may get a reaction. Replies to her own action
	// must not prompt her one by one: she is prompted once, when it is her
	// turn, with the choice of lines or a cue.
	const bool noReply = _interaction.outcome.pending() || anotherLineFollows();
	const bool spokenByActor = actor > 0 && actor < 0x80;
	if (!spokenByActor && line.size() <= kTypedOutMaxLength) {
		_heldSpeech.waiting = true;
		_heldSpeech.ended = false;
		_heldSpeech.speaker = speaker;
		_heldSpeech.text = line;
		_heldSpeech.noReply = noReply;
		_heldSpeech.dueAt = g_system->getMillis() + kTypedOutHoldMs;
		return;
	}
	sendSpeechStarted(speaker, line, noReply);
}

void NeuroBridge::sendSpeechStarted(const Common::String &speaker, const Common::String &line, bool noReply) {
	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(Common::String::format("%s-%u", _speechIdPrefix.c_str(), ++_speechCounter)));
	data.setVal("speaker", new Common::JSONValue(speaker));
	data.setVal("text", new Common::JSONValue(line));
	if (noReply)
		data.setVal("reply", new Common::JSONValue("never"));
	// Only a safety net in case the end message is lost: generous reading time
	data.setVal("expected_duration_ms", new Common::JSONValue((long long int)(3000 + line.size() * 150)));
	send("speech/started", &data);
}

/** Sends the line that was held to see whether it would be typed out further. */
void NeuroBridge::sendHeldSpeech() {
	_heldSpeech.waiting = false;
	sendSpeechStarted(_heldSpeech.speaker, _heldSpeech.text, _heldSpeech.noReply);
	if (_heldSpeech.ended)
		reportSpeechEnd();
}

bool NeuroBridge::speakerOf(int actor, const Common::String &line, Common::String &speaker) {
	const bool isActor = actor > 0 && actor < 0x80;
	const bool isEgo = actor == _vm->VAR(_vm->VAR_EGO);
	const byte color = _vm->_charsetColor;

	speaker = actorName(actor);
	if (_strangerRoom != _vm->_currentRoom) {
		_strangerRoom = _vm->_currentRoom;
		_strangers.clear();
		_voiceColors.clear();
	}
	if (speaker.empty()) {
		if (isEgo) {
			speaker = egoNameCapital();
		} else if (isActor) {
			// Keep unnamed speakers apart so a conversation can be followed
			uint index = 0;
			while (index < _strangers.size() && _strangers[index] != actor)
				index++;
			if (index == _strangers.size()) {
				_strangers.push_back(actor);
				debug(1, "Neuro: Unnamed speaker: actor %d in room %d", actor, _vm->_currentRoom);
			}
			speaker = Common::String::format("Stranger %u", index + 1);
		} else {
			speaker = _data.voiceName(_vm->_currentRoom, color);
			if (speaker == "-")
				return false;
			if (speaker.empty()) {
				speaker = "Narrator";
				debug(1, "Neuro: Unattributed line in room %d, text colour %d: %s", _vm->_currentRoom, color, line.c_str());
			}
		}
	}

	// A reviewer's word on this very line beats everything worked out above
	Common::String chosen;
	if (_data.lineSpeaker(line, chosen)) {
		if (chosen == "-")
			return false;
		if (!chosen.empty())
			speaker = chosen;
		return true;
	}

	// A group that is one actor speaks in a different text colour per member.
	// Number them once a second colour shows up, unless the file names them.
	const Common::String voice = isActor ? _data.voiceName(_vm->_currentRoom, color, actor) : Common::String();
	if (!voice.empty()) {
		speaker = voice;
	} else if (isActor && !isEgo) {
		Common::Array<byte> &colors = _voiceColors[speaker];
		uint index = 0;
		while (index < colors.size() && colors[index] != color)
			index++;
		if (index == colors.size()) {
			colors.push_back(color);
			debug(1, "Neuro: Voice %u of '%s' (actor %d, room %d) uses text colour %d", index + 1, speaker.c_str(), actor, _vm->_currentRoom, color);
		}
		if (colors.size() > 1)
			speaker += Common::String::format(" %u", index + 1);
	}
	return true;
}

bool NeuroBridge::isBackgroundChatter(const Common::String &spoken) {
	const uint32 now = g_system->getMillis();
	bool heard = false;
	for (uint i = 0; i < _recentLines.size();) {
		if (now - _recentLines[i].time > kChatterMemoryMs) {
			_recentLines.remove_at(i);
			continue;
		}
		if (_recentLines[i].text == spoken) {
			heard = true;
			_recentLines[i].time = now;
		}
		i++;
	}
	if (!heard)
		_recentLines.push_back({spoken, now});
	return heard && now - _lastActionAt > kReactionWindowMs;
}

/**
 * Whether the script that is printing a line goes straight on to print another:
 * the line's text, then "wait for message", then a print that is spoken. Called
 * while the print instruction runs, when the script pointer is at its text.
 * An actor changing pose in between is allowed; anything else (a jump, a walk,
 * a pause) counts as no.
 */
bool NeuroBridge::anotherLineFollows() const {
	// The layout read here is that of the version 4 and 5 scripts
	if (_vm->_game.version < 4 || _vm->_game.version > 5 || _vm->_currentScript == 0xFF || !_vm->_scriptPointer)
		return false;

	const byte *p = _vm->_scriptPointer;
	p += _vm->resStrLen(p) + 1;
	if (p[0] != 0xAE || (p[1] & 0x1F) != 2)	// wait for message
		return false;
	p += 2;

	// A parameter is a variable (two bytes) when its bit in the opcode is set.
	// An actor may be given a pose first, which takes no time
	for (int i = 0; i < 4 && (*p & 0x3F) == 0x11; i++) {
		const byte pose = *p++;
		for (int n = 0; n < 2; n++) {
			const bool isVar = pose & (0x80 >> n);
			if (isVar && (p[1] & 0x20))
				return false;
			p += isVar ? 2 : 1;
		}
	}
	const byte op = *p++;
	if (op == 0x14) {	// print, for an actor given by number
		if (*p >= 252 && *p <= 254)	// text that nobody speaks
			return false;
		p++;
	} else if (op == 0x94) {	// print, for an actor held in a variable
		if (p[1] & 0x20)	// an indexed variable takes more bytes
			return false;
		p += 2;
	} else if (op != 0xD8) {	// print for the player's character
		return false;
	}

	// Skip the settings in front of the text
	for (int i = 0; i < 8; i++) {
		const byte sub = *p++;
		if (sub == 0xFF)
			return false;
		int words = 0, bytes = 0;
		switch (sub & 0x0F) {
		case 0:	// position
			words = 2;
			break;
		case 1:	// colour
			bytes = 1;
			break;
		case 2:	// right edge
			words = 1;
			break;
		case 4:	// centred
		case 6:	// left
		case 7:	// overhead
			break;
		case 15:	// the text
			while (*p == ' ')
				p++;
			return *p != 0;
		default:
			return false;
		}
		// Variables are two bytes whatever the size of the value
		for (int n = 0; n < words + bytes; n++) {
			const bool isVar = sub & (0x80 >> n);
			if (isVar && (p[1] & 0x20))
				return false;
			p += (isVar || n < words) ? 2 : 1;
		}
	}
	return false;
}

void NeuroBridge::onTalkStopped() {
	if (!_speechOpen)
		return;
	_speechOpen = false;
	_lastTalkTime = g_system->getMillis();
	if (_dialogueAsContext)
		return;

	// Not sent yet, so there is no end to report yet either
	if (_heldSpeech.waiting) {
		_heldSpeech.ended = true;
		return;
	}
	reportSpeechEnd();
}

void NeuroBridge::reportSpeechEnd() {
	// Lines of one scene follow each other with short pauses. Reporting each
	// end at once would leave her those pauses to start talking in, so the
	// end is held back: the next line reports it, or update() once the pause
	// has lasted or the player is back in control.
	if (playerHasControl())
		sendSpeechEnded();
	else
		_speechEndDueAt = _lastTalkTime + kSpeechGapMs;
}

void NeuroBridge::sendSpeechEnded() {
	_speechEndDueAt = 0;
	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(Common::String::format("%s-%u", _speechIdPrefix.c_str(), _speechCounter)));
	send("speech/ended", &data);
}

void NeuroBridge::flushDialogue() {
	if (_pendingDialogue.empty())
		return;
	Common::String text = _pendingDialogue;
	_pendingDialogue.clear();
	_lastLineText.clear();
	_lastLineOffset = 0;
	// Mid-scene she is only listening; once she can act again a line may
	// be her cue. In a conversation the choice of lines prompts her instead.
	bool cue = playerHasControl();
	if (cue) {
		NeuroWorld world;
		collectWorld(world);
		cue = !world.inDialogue;
	}
	sendContext(text, !cue);
	if (cue)
		_interaction.outcome.report();
}

void NeuroBridge::onTextDrawn(const byte *text, int color) {
	const Common::String line = cleanText(text, true);
	if (line.empty() || !_ws.isConnected())
		return;
	Common::String source = _data.voiceName(_vm->_currentRoom, color);
	if (source == "-")
		return;
	if (source.empty())
		source = "Text on the screen";
	_journal.heard(line);
	_drawnText += source + ": \"" + line + "\"\n";
}

void NeuroBridge::flushDrawnText() {
	if (_drawnText.empty())
		return;
	// A screen that redraws its text every frame says it once
	if (_drawnText != _lastDrawnText)
		sendContext(_drawnText, true);
	_lastDrawnText = _drawnText;
	_drawnText.clear();
}

} // End of namespace Scumm
