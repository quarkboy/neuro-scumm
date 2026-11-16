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


// The column beside the game in a window made to be streamed: where she is,
// what she is doing, her goals and what she carries. See graphics/streampanel.h.

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "scumm/neuro/neuro.h"

#include "common/config-manager.h"
#include "common/system.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/streampanel.h"

#include "scumm/scumm.h"

namespace Scumm {

namespace {

const uint kPanelCheckMs = 250;
const uint kPanelRecent = 6;

Common::String panelSetting(const char *env, const char *key, const char *fallback) {
	const char *value = getenv(env);
	if (value && *value)
		return value;
	if (ConfMan.hasKey(key))
		return ConfMan.get(key);
	return fallback;
}

// A goal in the journal is a full sentence of instructions; the column shows its title
Common::String goalTitle(const Common::String &goal) {
	const size_t colon = goal.findFirstOf(':');
	if (colon != Common::String::npos && colon > 0 && colon < 40) {
		Common::String rest = goal.substr(colon + 1);
		rest.trim();
		// The catch-all goal that only holds notes
		if (rest.hasPrefix("what you learn"))
			return Common::String();
		return goal.substr(0, colon);
	}
	size_t cut = goal.size();
	for (uint i = 0; i < goal.size(); i++) {
		if (goal[i] == ',' || goal[i] == '.' || goal[i] == '(') {
			cut = i;
			break;
		}
	}
	Common::String title = goal.substr(0, cut);
	title.trim();
	return title;
}

/** Draws text top to bottom in a column of a surface, and keeps track of where it is. */
struct PanelWriter {
	Graphics::Surface &surface;
	int left, right, y;

	uint32 color(byte r, byte g, byte b) const {
		return surface.format.ARGBToColor(255, r, g, b);
	}

	void rect(int x1, int y1, int x2, int y2, uint32 c) {
		surface.fillRect(Common::Rect(MAX(x1, 0), MAX(y1, 0), MIN<int>(x2, surface.w), MIN<int>(y2, surface.h)), c);
	}

	/** Wrapped text; returns false once the bottom of the surface is reached. */
	bool text(const Graphics::Font *font, const Common::String &utf8, uint32 c, uint maxLines = 0,
			Graphics::TextAlign align = Graphics::kTextAlignLeft, int indent = 0) {
		Common::Array<Common::U32String> lines;
		font->wordWrapText(utf8.decode(Common::kUtf8), right - left - indent, lines);
		for (uint i = 0; i < lines.size(); i++) {
			if (maxLines && i >= maxLines)
				break;
			if (y + font->getFontHeight() > surface.h)
				return false;
			Common::U32String line = lines[i];
			if (maxLines && i == maxLines - 1 && lines.size() > maxLines)
				line += Common::U32String("...");
			font->drawString(&surface, line, left + indent, y, right - left - indent, c, align, 0, true);
			y += font->getFontHeight() + 1;
		}
		return true;
	}
};

} // End of anonymous namespace

Common::String NeuroBridge::describeForPanel(const Common::String &name, const NeuroArgs &args) {
	if (name == "interact") {
		const Common::String verb = args.get("verb|action|command");
		const Common::String target = args.get("target|object|thing|item|name|first_target|target1");
		const Common::String second = args.get("second_target|second|with|on|to|target2|recipient");
		const Common::String thenVerb = args.get("then_verb|then|next_verb|second_verb");
		const Common::String thenTarget = args.get("then_target|next_target");
		if (verb.empty())
			return args.get("text|sentence|input", true);
		Common::String text = verb + " " + target;
		if (!second.empty())
			text += (verb.equalsIgnoreCase("give") ? " to " : " with ") + second;
		if (!thenVerb.empty())
			text += ", then " + thenVerb + " " + thenTarget;
		return text;
	}
	if (name == "choose_dialogue") {
		Common::String choice = args.get("option|choice|line|number|index|id|text", true);
		const int number = atoi(choice.c_str());
		if (number > 0) {
			NeuroWorld world;
			collectWorld(world);
			if (world.inDialogue && number <= (int)world.verbs.size())
				choice = world.verbs[number - 1].name;
		}
		return "Says: \"" + choice + "\"";
	}
	if (name == "answer")
		return "Answers " + args.get("answer|response|reply|value|choice", true);
	if (name == "read_journal")
		return "Reads her journal";
	if (name == "add_note")
		return "Writes a note";
	if (name == "wait")
		return "Waits";
	if (name == "skip_cutscene")
		return "Skips the scene";
	return name;
}

void NeuroBridge::notePanelAction(const Common::String &name, const NeuroArgs &args) {
	if (!Graphics::streamPanel().enabled)
		return;
	_panelNow = describeForPanel(name, args);
	_panelRecent.insert_at(0, _panelNow);
	while (_panelRecent.size() > kPanelRecent)
		_panelRecent.pop_back();
}

void NeuroBridge::updatePanel() {
	Graphics::StreamPanel &panel = Graphics::streamPanel();
	if (!panel.enabled)
		return;

	const uint32 now = g_system->getMillis();
	if (panel.surface.getPixels() && now - _panelCheckedAt < kPanelCheckMs)
		return;
	_panelCheckedAt = now;

	if (_panelTitle.empty()) {
		_panelTitle = panelSetting("NEURO_PANEL_TITLE", "neuro_panel_title", "Neuro plays|The Secret of Monkey Island");
		_panelChat = CLIP(atoi(panelSetting("NEURO_PANEL_CHAT", "neuro_panel_chat", "30").c_str()), 0, 80);
		if (_panelHide.empty())
			_panelHide = "," + panelSetting("NEURO_PANEL_HIDE", "neuro_panel_hide", "") + ",";
	}

	// Parts of the column can be left out: title, chat, location, now, goals, inventory, recent
	auto shown = [&](const char *part) {
		return !_panelHide.contains(Common::String(",") + part + ",");
	};

	// What there is to show
	Common::String location, state;
	Common::StringArray goals, inventory;
	if (_vm->_currentRoom != 0) {
		NeuroWorld world;
		collectWorld(world);
		location = _data.roomName(world.room);
		for (const auto &item : world.inventory)
			inventory.push_back(item.name);

		Common::String journal = _journal.describe(false);
		while (!journal.empty()) {
			const size_t end = journal.findFirstOf('\n');
			const Common::String line = journal.substr(0, end);
			journal = end == Common::String::npos ? Common::String() : journal.substr(end + 1);
			// Her own notes come after the goals and are not goals
			if (line.hasPrefix("Your notes"))
				break;
			if (line.hasPrefix("- ")) {
				const Common::String title = goalTitle(line.substr(2));
				if (!title.empty())
					goals.push_back(title);
			}
		}
	}

	bool thinking = false;
	if (_panelHeld) {
		state = "Taking a short break";
	} else if (!_ws.isConnected()) {
		state = "Waiting for Neuro";
	} else if (_forceActive) {
		thinking = true;
		state = "Thinking";
		for (uint i = 0; i < (now / 400) % 4; i++)
			state += '.';
	} else if (!_panelNow.empty()) {
		state = _panelNow;
	} else {
		state = "Watching";
	}

	// Only draw when something changed
	Common::String key = location + "\n" + state + "\n" + _panelTitle + "\n" + _panelHide + "\n";
	for (const auto &goal : goals)
		key += goal + "|";
	for (const auto &item : inventory)
		key += item + "|";
	for (const auto &recent : _panelRecent)
		key += recent + "|";
	if (key == _panelKey && panel.surface.getPixels())
		return;
	_panelKey = key;

	// The column is drawn small and shown enlarged by a whole number, so that
	// the built-in bitmap fonts come out as crisp, chunky pixels
	// (1080 and 900 by two, 1440 by three). Where no whole number gives
	// text of about the same size (720), it is drawn 540 tall and smoothed.
	const int zoom = MAX(1, (panel.windowHeight + 270) / 540);
	int height = panel.windowHeight / zoom;
	int width = panel.width() / zoom;
	if (height < 440 || height > 620) {
		height = 540;
		width = panel.width() * 540 / panel.windowHeight;
	}
	width = MAX(80, width);
	Graphics::Surface &surface = panel.surface;
	if (surface.w != width || surface.h != height || !surface.getPixels()) {
		surface.free();
		surface.create(width, height, Graphics::StreamPanel::format());
	}

	const Graphics::Font *big = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *small = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!big || !small)
		return;

	PanelWriter w = { surface, 8, width - 8, 0 };
	const uint32 gold = w.color(240, 195, 106);
	const uint32 white = w.color(238, 240, 250);
	const uint32 dim = w.color(164, 171, 208);
	const uint32 pink = w.color(255, 156, 203);
	const uint32 card = w.color(28, 34, 68);
	const uint32 line = w.color(92, 80, 60);

	// A night sky, darker towards the bottom
	for (int y = 0; y < height; y++) {
		const int t = y * 255 / height;
		w.rect(0, y, width, y + 1, w.color(22 - t * 11 / 255, 28 - t * 12 / 255, 66 - t * 28 / 255));
	}

	// Title
	w.y = 8;
	Common::String title = shown("title") ? _panelTitle : Common::String();
	while (!title.empty()) {
		const size_t bar = title.findFirstOf('|');
		w.text(big, title.substr(0, bar), gold, 2, Graphics::kTextAlignCenter);
		title = bar == Common::String::npos ? Common::String() : title.substr(bar + 1);
	}
	w.y += 5;

	// An empty box to lay the stream's chat over
	if (_panelChat > 0 && shown("chat")) {
		const int boxHeight = height * _panelChat / 100;
		w.rect(w.left - 1, w.y - 1, w.right + 1, w.y + boxHeight + 1, line);
		w.rect(w.left, w.y, w.right, w.y + boxHeight, w.color(8, 10, 24));
		w.y += boxHeight + 8;
	}

	// Each section: a small heading, then its text on a slightly lighter card
	auto section = [&](const char *heading, int lines) {
		const int cardHeight = small->getFontHeight() + 4 + lines + 4;
		w.rect(w.left - 4, w.y, w.right + 4, MIN(w.y + cardHeight, height - 4), card);
		w.y += 3;
		w.text(small, heading, gold, 1);
		w.y += 1;
	};

	Common::Array<Common::U32String> wrapped;

	if (shown("location")) {
		big->wordWrapText(location.decode(Common::kUtf8), w.right - w.left, wrapped);
		section("WHERE SHE IS", MIN<int>(MAX<int>(wrapped.size(), 1), 2) * (big->getFontHeight() + 1));
		w.text(big, location.empty() ? Common::String("...") : location, white, 2);
		w.y += 9;
	}

	if (shown("now")) {
		wrapped.clear();
		big->wordWrapText(state.decode(Common::kUtf8), w.right - w.left, wrapped);
		section("RIGHT NOW", MIN<int>(MAX<int>(wrapped.size(), 2), 3) * (big->getFontHeight() + 1));
		const int stateTop = w.y;
		w.text(big, state, thinking ? pink : white, 3);
		w.y = MAX(w.y, stateTop + 2 * (big->getFontHeight() + 1)) + 9;
	}

	if (shown("goals") && !goals.empty() && w.y < height - 40) {
		section("HER GOALS", goals.size() * (small->getFontHeight() + 1));
		for (const auto &goal : goals) {
			small->drawString(&surface, Common::U32String("*"), w.left, w.y, 8, gold);
			if (!w.text(small, goal, white, 1, Graphics::kTextAlignLeft, 9))
				break;
		}
		w.y += 9;
	}

	if (shown("inventory") && !inventory.empty() && w.y < height - 40) {
		Common::String carrying;
		for (uint i = 0; i < inventory.size(); i++)
			carrying += (i ? ", " : "") + inventory[i];
		wrapped.clear();
		small->wordWrapText(carrying.decode(Common::kUtf8), w.right - w.left, wrapped);
		section("CARRYING", wrapped.size() * (small->getFontHeight() + 1));
		w.text(small, carrying, white);
		w.y += 9;
	}

	if (shown("recent") && !_panelRecent.empty() && w.y < height - 40) {
		section("LATELY", _panelRecent.size() * (small->getFontHeight() + 1));
		for (uint i = 0; i < _panelRecent.size(); i++) {
			if (!w.text(small, _panelRecent[i], i == 0 ? white : dim, 1))
				break;
		}
	}

	panel.generation++;
}

} // End of namespace Scumm
