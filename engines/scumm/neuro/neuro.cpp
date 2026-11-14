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

#include "scumm/neuro/neuro.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "scumm/scumm.h"

#ifndef USE_BASIC_NET
#error "The Neuro-sama integration needs libcurl. Install it or configure with --disable-engine=neuro"
#endif

namespace Scumm {

static const uint32 kReconnectDelayMs = 5000;

NeuroBridge *NeuroBridge::create(ScummEngine *vm) {
	Common::String url;
	const char *env = getenv("NEURO_SDK_WS_URL");
	if (env && *env)
		url = env;
	else if (ConfMan.hasKey("neuro_ws_url"))
		url = ConfMan.get("neuro_ws_url");

	if (url.empty())
		return nullptr;
	return new NeuroBridge(vm, url);
}

NeuroBridge::NeuroBridge(ScummEngine *vm, const Common::String &url) : _vm(vm), _url(url) {
	// The API identifies a game by its display name, which must never change
	switch (_vm->_game.id) {
	case GID_MONKEY:
	case GID_MONKEY_EGA:
	case GID_MONKEY_VGA:
		_gameName = "The Secret of Monkey Island";
		break;
	case GID_MONKEY2:
		_gameName = "Monkey Island 2: LeChuck's Revenge";
		break;
	default:
		_gameName = _vm->_game.gameid;
		break;
	}
}

void NeuroBridge::update() {
	if (!_ws.isConnected()) {
		if (_wasConnected) {
			warning("Neuro: Connection to %s lost", _url.c_str());
			_wasConnected = false;
		}

		const uint32 now = g_system->getMillis();
		if (now < _nextConnectAttempt)
			return;
		_nextConnectAttempt = now + kReconnectDelayMs;

		if (!_ws.connect(_url))
			return;
		_wasConnected = true;
		onConnected();
	}

	Common::String message;
	while (_ws.receive(message))
		handleMessage(message);
}

void NeuroBridge::onConnected() {
	debug(1, "Neuro: Connected to %s as '%s'", _url.c_str(), _gameName.c_str());
	send("startup");
	registerActions();
}

void NeuroBridge::registerActions() {
	Common::JSONObject wait;
	wait.setVal("name", new Common::JSONValue("wait"));
	wait.setVal("description", new Common::JSONValue("Do nothing for a moment and let the game continue."));

	Common::JSONArray actions;
	actions.push_back(new Common::JSONValue(wait));

	Common::JSONObject data;
	data.setVal("actions", new Common::JSONValue(actions));
	send("actions/register", &data);
}

void NeuroBridge::send(const char *command, const Common::JSONObject *data) {
	Common::JSONObject root;
	root.setVal("command", new Common::JSONValue(command));
	root.setVal("game", new Common::JSONValue(_gameName));
	if (data)
		root.setVal("data", new Common::JSONValue(*data));

	const Common::String text = Common::JSONValue(root).stringify();
	debug(2, "Neuro: > %s", text.c_str());
	_ws.sendText(text);
}

void NeuroBridge::sendContext(const Common::String &message, bool silent) {
	Common::JSONObject data;
	data.setVal("message", new Common::JSONValue(message));
	data.setVal("silent", new Common::JSONValue(silent));
	send("context", &data);
}

void NeuroBridge::sendActionResult(const Common::String &id, bool success, const Common::String &message) {
	Common::JSONObject data;
	data.setVal("id", new Common::JSONValue(id));
	data.setVal("success", new Common::JSONValue(success));
	if (!message.empty())
		data.setVal("message", new Common::JSONValue(message));
	send("action/result", &data);
}

void NeuroBridge::handleMessage(const Common::String &text) {
	debug(2, "Neuro: < %s", text.c_str());

	Common::JSONValue *root = Common::JSON::parse(text);
	if (!root)
		return;

	if (root->isObject() && root->hasChild("command") && root->child("command")->isString()) {
		const Common::String &command = root->child("command")->asString();
		Common::JSONValue *data = root->hasChild("data") ? root->child("data") : nullptr;

		if (command == "action" && data && data->isObject()) {
			Common::String id, name, args;
			if (data->hasChild("id") && data->child("id")->isString())
				id = data->child("id")->asString();
			if (data->hasChild("name") && data->child("name")->isString())
				name = data->child("name")->asString();
			if (data->hasChild("data") && data->child("data")->isString())
				args = data->child("data")->asString();
			if (!id.empty())
				handleAction(id, name, args);
		} else if (command == "actions/reregister_all") {
			registerActions();
		}
	}

	delete root;
}

void NeuroBridge::handleAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	if (name == "wait") {
		sendActionResult(id, true);
		return;
	}

	sendActionResult(id, false, Common::String::format("Unknown action '%s'.", name.c_str()));
}

} // End of namespace Scumm
