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

#ifndef GRAPHICS_STREAMPANEL_H
#define GRAPHICS_STREAMPANEL_H

#include "graphics/surface.h"

namespace Graphics {

/**
 * A column beside the game picture, for a window made to be streamed: the
 * game keeps its 4:3 shape and the rest of a wider window shows whatever the
 * engine draws here.
 *
 * It is switched on by giving the window's size in the NEURO_STREAM_SIZE
 * environment variable or the neuro_stream_size config key ("1920x1080"):
 * whatever is wider than 4:3 becomes the column. NEURO_PANEL_SIDE (or
 * neuro_panel_side) puts it on the "left" or the "right". Only the SDL
 * software renderer honours this.
 */
struct StreamPanel {
	/** A window size was asked for. */
	bool sized = false;
	/** The window is wider than the game, and the rest is the column. */
	bool enabled = false;
	bool onLeft = false;

	/** The window size asked for. */
	int windowWidth = 0;
	int windowHeight = 0;

	/** What the column shows, in ARGB8888; stretched to fit. Empty until the engine draws. */
	Surface surface;

	/** Goes up every time the surface changes. */
	uint32 generation = 0;

	/** The column's width in a window of the size asked for. */
	int width() const;

	static PixelFormat format() { return PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24); }
};

StreamPanel &streamPanel();

} // End of namespace Graphics

#endif
