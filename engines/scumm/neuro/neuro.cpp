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
#include "common/random.h"
#include "common/system.h"
#include "common/util.h"
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

	const char *mode = getenv("NEURO_DIALOGUE_MODE");
	Common::String dialogueMode;
	if (mode && *mode)
		dialogueMode = mode;
	else if (ConfMan.hasKey("neuro_dialogue_mode"))
		dialogueMode = ConfMan.get("neuro_dialogue_mode");
	_dialogueAsContext = dialogueMode.equalsIgnoreCase("context");

	const char *idle = getenv("NEURO_IDLE_FORCE_SECONDS");
	int idleSeconds = 45;
	if (idle && *idle)
		idleSeconds = atoi(idle);
	else if (ConfMan.hasKey("neuro_idle_force_seconds"))
		idleSeconds = ConfMan.getInt("neuro_idle_force_seconds");
	_idleForceMs = MAX(0, idleSeconds) * 1000;

	// Line ids must stay unique across restarts of the game
	Common::RandomSource rnd("neuro_speech");
	_speechIdPrefix = Common::String::format("%08x", rnd.getRandomNumber(0x7FFFFFFF));
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

	updateClick();

	if (!_pendingDialogue.empty() && !_speechOpen && g_system->getMillis() - _lastTalkTime > 1000)
		flushDialogue();

	pollWorld();
}

void NeuroBridge::flushDialogue() {
	if (_pendingDialogue.empty())
		return;
	Common::String text = _pendingDialogue;
	_pendingDialogue.clear();
	_lastLineText.clear();
	_lastLineOffset = 0;
	sendContext(text, false);
}

void NeuroBridge::onConnected() {
	debug(1, "Neuro: Connected to %s as '%s'", _url.c_str(), _gameName.c_str());
	send("startup");
	registerActions();

	sendContext(Common::String::format(
		"You are playing %s, a classic point-and-click adventure game. "
		"You control Guybrush Threepwood, who wants to become a pirate. "
		"You cannot see the screen, so each location and what is in it will be described to you.",
		_gameName.c_str()), true);

	// Describe the current room again from scratch
	_described = NeuroWorld();
	_speechOpen = false;
	_forceActive = false;
	_forcedDialogue.clear();
	_lastActivity = g_system->getMillis();
}

// JSON values own their children, so every use needs a fresh copy
static Common::JSONValue *stringSchema() {
	Common::JSONObject schema;
	schema.setVal("type", new Common::JSONValue("string"));
	return new Common::JSONValue(schema);
}

void NeuroBridge::registerActions() {
	Common::JSONObject wait;
	wait.setVal("name", new Common::JSONValue("wait"));
	wait.setVal("description", new Common::JSONValue("Do nothing for a moment and let the game continue."));

	Common::JSONObject skip;
	skip.setVal("name", new Common::JSONValue("skip_cutscene"));
	skip.setVal("description", new Common::JSONValue("Skip the cutscene that is currently playing."));

	// Once the game's verbs are known they are offered as an enum
	Common::JSONValue *verbSchema = stringSchema();
	if (!_standardVerbs.empty()) {
		Common::JSONArray names;
		for (const auto &verb : _standardVerbs)
			names.push_back(new Common::JSONValue(verb.name));
		names.push_back(new Common::JSONValue("Walk to"));
		Common::JSONObject withEnum;
		withEnum.setVal("type", new Common::JSONValue("string"));
		withEnum.setVal("enum", new Common::JSONValue(names));
		delete verbSchema;
		verbSchema = new Common::JSONValue(withEnum);
	}

	Common::JSONObject properties;
	properties.setVal("verb", verbSchema);
	properties.setVal("target", stringSchema());
	properties.setVal("second_target", stringSchema());
	Common::JSONArray required;
	required.push_back(new Common::JSONValue("verb"));
	required.push_back(new Common::JSONValue("target"));
	Common::JSONObject schema;
	schema.setVal("type", new Common::JSONValue("object"));
	schema.setVal("properties", new Common::JSONValue(properties));
	schema.setVal("required", new Common::JSONValue(required));

	Common::JSONObject interact;
	interact.setVal("name", new Common::JSONValue("interact"));
	interact.setVal("description", new Common::JSONValue(
		"Use one of the game's verbs on something in the room or in your inventory, e.g. verb \"Look at\", target \"poster\". "
		"Walk somewhere with the verb \"Walk to\". "
		"second_target is only for using or giving an inventory item: \"Use\" target \"key\" second_target \"door\"."));
	interact.setVal("schema", new Common::JSONValue(schema));

	Common::JSONObject optionType;
	optionType.setVal("type", new Common::JSONValue("integer"));
	optionType.setVal("minimum", new Common::JSONValue((long long int)1));
	Common::JSONObject optionProperties;
	optionProperties.setVal("option", new Common::JSONValue(optionType));
	Common::JSONArray optionRequired;
	optionRequired.push_back(new Common::JSONValue("option"));
	Common::JSONObject optionSchema;
	optionSchema.setVal("type", new Common::JSONValue("object"));
	optionSchema.setVal("properties", new Common::JSONValue(optionProperties));
	optionSchema.setVal("required", new Common::JSONValue(optionRequired));

	Common::JSONObject choose;
	choose.setVal("name", new Common::JSONValue("choose_dialogue"));
	choose.setVal("description", new Common::JSONValue(
		"During a conversation, pick what Guybrush says next by the number of the line."));
	choose.setVal("schema", new Common::JSONValue(optionSchema));

	Common::JSONArray actions;
	actions.push_back(new Common::JSONValue(interact));
	actions.push_back(new Common::JSONValue(choose));
	actions.push_back(new Common::JSONValue(wait));
	actions.push_back(new Common::JSONValue(skip));

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

void NeuroBridge::sendForce(const Common::String &state, const Common::String &query, const char *actionName) {
	Common::JSONArray names;
	names.push_back(new Common::JSONValue(actionName));

	Common::JSONObject data;
	data.setVal("state", new Common::JSONValue(state));
	data.setVal("query", new Common::JSONValue(query));
	data.setVal("ephemeral_context", new Common::JSONValue(true));
	data.setVal("action_names", new Common::JSONValue(names));
	send("actions/force", &data);

	_forceActive = true;
	_forceSentAt = g_system->getMillis();
}

void NeuroBridge::sendActionResult(const Common::String &id, bool success, const Common::String &message) {
	// Keep what she is told in the order it happened
	flushDialogue();
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
	// Any action answers an outstanding force
	_forceActive = false;
	_lastActivity = g_system->getMillis();

	if (name == "choose_dialogue") {
		Common::JSONValue *args = data.empty() ? nullptr : Common::JSON::parse(data);
		actionChooseDialogue(id, args);
		delete args;
		return;
	}

	// Development aid, deliberately not registered: saves a screenshot
	// to ScummVM's screenshot directory.
	if (name == "debug_screenshot") {
		g_system->saveScreenshot();
		sendActionResult(id, true);
		return;
	}

	if (name == "wait") {
		sendActionResult(id, true);
		return;
	}

	if (name == "skip_cutscene") {
		actionSkipCutscene(id);
		return;
	}

	if (name == "interact") {
		Common::JSONValue *args = data.empty() ? nullptr : Common::JSON::parse(data);
		actionInteract(id, args);
		delete args;
		return;
	}

	sendActionResult(id, false, Common::String::format("Unknown action '%s'.", name.c_str()));
}

} // End of namespace Scumm
