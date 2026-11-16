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

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "common/config-manager.h"
#include "common/str.h"
#include "common/util.h"

#include "graphics/streampanel.h"

namespace Graphics {

static Common::String panelSetting(const char *env, const char *key) {
	const char *value = getenv(env);
	if (value && *value)
		return value;
	if (ConfMan.hasKey(key))
		return ConfMan.get(key);
	return Common::String();
}

int StreamPanel::width() const {
	// The game is 4:3 and as tall as the window
	return MAX(0, windowWidth - windowHeight * 4 / 3);
}

StreamPanel &streamPanel() {
	static StreamPanel panel;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		int w = 0, h = 0;
		const Common::String size = panelSetting("NEURO_STREAM_SIZE", "neuro_stream_size");
		if (sscanf(size.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 200) {
			panel.windowWidth = w;
			panel.windowHeight = h;
			panel.sized = true;
			panel.enabled = panel.width() > 0;
		}
		panel.onLeft = panelSetting("NEURO_PANEL_SIDE", "neuro_panel_side").equalsIgnoreCase("left");
	}
	return panel;
}

} // End of namespace Graphics
