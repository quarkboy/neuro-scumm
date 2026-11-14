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

#ifndef SCUMM_NEURO_ARGS_H
#define SCUMM_NEURO_ARGS_H

#include "common/hash-str.h"
#include "common/str.h"

namespace Scumm {

/**
 * The data that came with an action, read as forgivingly as possible.
 *
 * It is supposed to be a JSON object matching the action's schema, but a
 * language model wrote it. Where the meaning is plain, small mistakes are
 * put right: a code fence or chatter around the JSON, JSON inside a JSON
 * string, single quotes, bare keys, trailing commas, a wrapper object such
 * as {"data": {...}}, differently spelled keys, or a bare value where an
 * object with one field was expected.
 */
class NeuroArgs {
public:
	explicit NeuroArgs(const Common::String &data);

	/**
	 * The value of a field as text, trying each of the |-separated names in
	 * turn; empty if there is none. If the data was a bare value rather
	 * than an object and bareValueToo is set, that value is returned.
	 */
	Common::String get(const char *names, bool bareValueToo = false) const;

private:
	Common::StringMap _fields;
	Common::String _bareValue;
};

} // End of namespace Scumm

#endif
