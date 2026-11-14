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

#include "scumm/neuro/args.h"

#include "common/formats/json.h"
#include "common/tokenizer.h"
#include "common/util.h"

namespace Scumm {

/** Fixes what a hand-typed "JSON" object most often gets wrong. */
static Common::String repairJson(const Common::String &text) {
	// Single quotes as string quotes, but only if there are no real ones to confuse them with
	const bool singleQuoted = !text.contains('"');

	Common::String result;
	bool inString = false;
	for (uint i = 0; i < text.size(); i++) {
		char c = text[i];
		if (singleQuoted && c == '\'')
			c = '"';

		if (inString) {
			result += c;
			if (c == '\\' && i + 1 < text.size())
				result += text[++i];
			else if (c == '"')
				inString = false;
			continue;
		}

		if (c == '"') {
			inString = true;
			result += c;
		} else if (c == ',') {
			// Drop a comma that is followed by the end of the object or list
			uint next = i + 1;
			while (next < text.size() && Common::isSpace(text[next]))
				next++;
			if (next >= text.size() || (text[next] != '}' && text[next] != ']'))
				result += c;
		} else if (Common::isAlpha(c) || c == '_') {
			// A bare word: quote it if it is a key, leave true/false/null alone
			uint end = i;
			while (end < text.size() && (Common::isAlnum(text[end]) || text[end] == '_' || text[end] == '-'))
				end++;
			uint next = end;
			while (next < text.size() && Common::isSpace(text[next]))
				next++;
			const Common::String word = text.substr(i, end - i);
			if (next < text.size() && text[next] == ':')
				result += "\"" + word + "\"";
			else
				result += word;
			i = end - 1;
		} else {
			result += c;
		}
	}
	return result;
}

static Common::JSONValue *parseLeniently(Common::String text) {
	text.trim();
	if (text.empty())
		return nullptr;

	// ```json ... ```
	if (text.hasPrefix("```")) {
		const size_t lineEnd = text.findFirstOf('\n');
		text = lineEnd == Common::String::npos ? text.substr(3) : text.substr(lineEnd + 1);
		const size_t fence = text.rfind("```");
		if (fence != Common::String::npos)
			text = text.substr(0, fence);
		text.trim();
	}

	Common::JSONValue *value = Common::JSON::parse(text);
	if (value && value->isString()) {
		// JSON wrapped in a JSON string, or simply a bare string
		const Common::String inner = value->asString();
		Common::JSONValue *unwrapped = (inner.contains('{')) ? Common::JSON::parse(inner) : nullptr;
		if (unwrapped) {
			delete value;
			value = unwrapped;
		}
	}
	if (value)
		return value;

	// Words around the object: keep what is between the outermost braces
	const size_t open = text.findFirstOf('{');
	const size_t close = text.findLastOf('}');
	if (open == Common::String::npos || close == Common::String::npos || close < open)
		return new Common::JSONValue(text);
	text = text.substr(open, close - open + 1);

	value = Common::JSON::parse(text);
	if (!value)
		value = Common::JSON::parse(repairJson(text));
	return value;
}

/** verb, Verb, "second target" and second-target are all the same key. */
static Common::String normalizeKey(const Common::String &key) {
	Common::String result;
	for (uint i = 0; i < key.size(); i++) {
		const char c = key[i];
		if (Common::isAlnum(c))
			result += (char)tolower(c);
		else if (c == ' ' || c == '-' || c == '_')
			result += '_';
	}
	return result;
}

static Common::String valueText(const Common::JSONValue *value) {
	if (!value || value->isNull())
		return Common::String();
	if (value->isString())
		return value->asString();
	if (value->isBool())
		return value->asBool() ? "true" : "false";
	if (value->isIntegerNumber())
		return Common::String::format("%lld", value->asIntegerNumber());
	if (value->isNumber())
		return Common::String::format("%g", value->asNumber());
	return value->stringify();
}

NeuroArgs::NeuroArgs(const Common::String &data) {
	Common::JSONValue *root = parseLeniently(data);
	if (!root)
		return;

	const Common::JSONValue *object = root;
	// {"data": {...}} and the like: the real fields are one level down
	while (object->isObject() && object->asObject().size() == 1) {
		const Common::JSONValue *only = object->asObject().begin()->_value;
		if (!only || !only->isObject())
			break;
		object = only;
	}

	if (object->isObject()) {
		for (const auto &field : object->asObject())
			_fields[normalizeKey(field._key)] = valueText(field._value);
	} else if (object->isArray()) {
		if (!object->asArray().empty())
			_bareValue = valueText(object->asArray()[0]);
	} else {
		_bareValue = valueText(object);
	}
	_bareValue.trim();
	delete root;
}

Common::String NeuroArgs::get(const char *names, bool bareValueToo) const {
	Common::StringTokenizer alternatives(names, "|");
	while (!alternatives.empty()) {
		const Common::String name = alternatives.nextToken();
		if (_fields.contains(name)) {
			Common::String value = _fields[name];
			value.trim();
			if (!value.empty())
				return value;
		}
	}
	// One field given, under a name nobody expected: it can only be this one
	if (bareValueToo && _bareValue.empty() && _fields.size() == 1)
		return _fields.begin()->_value;
	return bareValueToo ? _bareValue : Common::String();
}

} // End of namespace Scumm
