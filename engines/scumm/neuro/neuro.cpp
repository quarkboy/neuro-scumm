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

#include "scumm/actor.h"
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

	const char *descriptions = getenv("NEURO_DESCRIPTIONS");
	if (descriptions && *descriptions)
		_data.load(descriptions);
	else if (ConfMan.hasKey("neuro_descriptions"))
		_data.load(ConfMan.get("neuro_descriptions"));

	_journal.loadGoals(_data.ini());
	// Starting straight from a savegame (-x) loads it before the first frame
	if (ConfMan.hasKey("save_slot") && ConfMan.getInt("save_slot") >= 0)
		loadJournal(ConfMan.getInt("save_slot"));

	const char *fast = getenv("NEURO_FAST_MODE");
	Common::String fastMode;
	if (fast && *fast)
		fastMode = fast;
	else if (ConfMan.hasKey("neuro_fast_mode"))
		fastMode = ConfMan.get("neuro_fast_mode");
	if (fastMode.equalsIgnoreCase("always"))
		_fastMode = kFastAlways;
	else if (fastMode.equalsIgnoreCase("scenes"))
		_fastMode = kFastScenes;

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

	updateSpeed();
	updateClick();
	updateQueuedCommand();

	// Saving and loading happen between frames. The journal travels with
	// each savegame in a file of its own.
	if (_vm->_saveLoadFlag != 0) {
		_saveLoadSlot = _vm->_saveLoadSlot;
		_loadPending = _vm->_saveLoadFlag == 2;
		_savePending = !_loadPending;
	} else if (_loadPending) {
		_loadPending = false;
		resetAfterLoad();
		loadJournal(_saveLoadSlot);
	} else if (_savePending) {
		_savePending = false;
		saveJournal(_saveLoadSlot);
	}

	if (!_pendingDialogue.empty()) {
		const uint32 now = g_system->getMillis();
		// Normally wait for a pause, but do not let endless chatter hold lines back
		if ((!_speechOpen && now - _lastTalkTime > 1000) || now - _pendingSince > 5000)
			flushDialogue();
	}

	pollWorld();
}

Common::String NeuroBridge::journalFileName(int slot) const {
	return Common::String::format("%s-journal.%02d", _vm->_targetName.c_str(), slot);
}

void NeuroBridge::saveJournal(int slot) {
	_journal.memos.clear();
	for (const auto &named : _namedCostume)
		_journal.memos[Common::String::format("actor.%d", named._key)] = Common::String::format("%d", named._value);
	_journal.save(journalFileName(slot));
}

void NeuroBridge::loadJournal(int slot) {
	_journal.load(journalFileName(slot));
	_namedCostume.clear();
	for (const auto &memo : _journal.memos) {
		if (memo._key.hasPrefix("actor."))
			_namedCostume[atoi(memo._key.c_str() + 6)] = atoi(memo._value.c_str());
	}
}

void NeuroBridge::sendJournalNews() {
	const Common::String news = _journal.takeNews();
	if (!news.empty())
		sendContext(news, true);
}

void NeuroBridge::updateSpeed() {
	if (_fastMode == kFastOff)
		return;
	// Bit 0 is the engine's "fast mode" (Ctrl-F). Keep real speed while
	// she is deciding, so timing stays what it would be on stream.
	const bool fast = _fastMode == kFastAlways || !playerHasControl();
	if (fast)
		_vm->_fastMode |= 1;
	else
		_vm->_fastMode &= ~1;
}

void NeuroBridge::resetAfterLoad() {
	_described = NeuroWorld();
	_queue.clear();
	_clickPhase = kClickIdle;
	_pendingDialogue.clear();
	_speechOpen = false;
	_namedCostume.clear();
	_recentLines.clear();
	_lastActivity = g_system->getMillis();
	sendContext("A saved game was loaded. Forget where you were; your situation is described again below.", true);
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
	sendJournalNews();
}

void NeuroBridge::onConnected() {
	debug(1, "Neuro: Connected to %s as '%s'", _url.c_str(), _gameName.c_str());
	send("startup");
	registerActions();

	sendContext(Common::String::format(
		"You are playing %s, a classic point-and-click adventure game. "
		"You control Guybrush Threepwood, who wants to become a pirate. "
		"You cannot see the screen, so each location and what is in it will be described to you. "
		"Your goals are kept in a journal that you can read at any time with read_journal.",
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

void NeuroBridge::registerActions(bool interactOnly) {
	_registeredAt = g_system->getMillis();

	// Older servers keep the first definition of an action instead of
	// replacing it, so drop the old one before sending a new one.
	if (interactOnly) {
		Common::JSONArray names;
		names.push_back(new Common::JSONValue("interact"));
		Common::JSONObject data;
		data.setVal("action_names", new Common::JSONValue(names));
		send("actions/unregister", &data);
	}

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
	properties.setVal("then_verb", stringSchema());
	properties.setVal("then_target", stringSchema());
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
		"second_target is only for using or giving an inventory item: \"Use\" target \"key\" second_target \"door\". "
		"When timing matters, then_verb and then_target add a second step that is done the instant the first one finishes."));
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
	Common::JSONObject readJournal;
	readJournal.setVal("name", new Common::JSONValue("read_journal"));
	readJournal.setVal("description", new Common::JSONValue(
		"Read your journal: your current goals and your own notes. Set show_completed to also list goals you have already finished."));
	Common::JSONObject completedType;
	completedType.setVal("type", new Common::JSONValue("boolean"));
	Common::JSONObject journalProperties;
	journalProperties.setVal("show_completed", new Common::JSONValue(completedType));
	Common::JSONObject journalSchema;
	journalSchema.setVal("type", new Common::JSONValue("object"));
	journalSchema.setVal("properties", new Common::JSONValue(journalProperties));
	readJournal.setVal("schema", new Common::JSONValue(journalSchema));

	Common::JSONObject noteProperties;
	noteProperties.setVal("text", stringSchema());
	Common::JSONArray noteRequired;
	noteRequired.push_back(new Common::JSONValue("text"));
	Common::JSONObject noteSchema;
	noteSchema.setVal("type", new Common::JSONValue("object"));
	noteSchema.setVal("properties", new Common::JSONValue(noteProperties));
	noteSchema.setVal("required", new Common::JSONValue(noteRequired));
	Common::JSONObject addNote;
	addNote.setVal("name", new Common::JSONValue("add_note"));
	addNote.setVal("description", new Common::JSONValue(
		"Write a short note in your journal so you remember it later, such as a clue or a plan."));
	addNote.setVal("schema", new Common::JSONValue(noteSchema));

	Common::JSONValue *others[] = {new Common::JSONValue(choose), new Common::JSONValue(wait), new Common::JSONValue(skip),
		new Common::JSONValue(readJournal), new Common::JSONValue(addNote)};
	for (Common::JSONValue *other : others) {
		if (interactOnly)
			delete other;
		else
			actions.push_back(other);
	}

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
	_forceAction = actionName;
	_forceSentAt = g_system->getMillis();
}

void NeuroBridge::sendActionResult(const Common::String &id, bool success, const Common::String &message) {
	// Keep what she is told in the order it happened
	flushDialogue();

	// A force is over once the forced action succeeds; the server retries a failed one itself
	if (success && _forceActive && _currentAction == _forceAction)
		_forceActive = false;
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
			// Some servers ask for this on connect, when everything has just been sent
			if (g_system->getMillis() - _registeredAt > 3000)
				registerActions();
		}
	}

	delete root;
}

void NeuroBridge::handleDebugAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	// Development aid, deliberately not registered: saves a screenshot
	// to ScummVM's screenshot directory.
	if (name == "debug_screenshot") {
		g_system->saveScreenshot();
		sendActionResult(id, true);
		return;
	}

	if (name == "debug_dump") {
		debugDump();
		sendActionResult(id, true);
		return;
	}
	if (name == "debug_save" || name == "debug_load") {
		int slot = 0;
		char label[64] = "";
		sscanf(data.c_str(), "%d %63[^\n]", &slot, label);
		sendActionResult(id, true);
		if (name == "debug_save")
			_vm->requestSave(slot, label[0] ? label : "neuro");
		else
			_vm->requestLoad(slot);
		return;
	}
	if (name == "debug_speed") {
		_fastMode = kFastOff;
		_vm->_fastMode = atoi(data.c_str());
		sendActionResult(id, true);
		return;
	}
	if (name == "debug_room") {
		const int room = atoi(data.c_str());
		sendActionResult(id, true);
		_vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_room = room;
		_vm->startScene(room, nullptr, 0);
		_vm->_fullRedraw = true;
		return;
	}

	sendActionResult(id, false, "Unknown debug action.");
}

void NeuroBridge::handleAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	// Development aids are not her actions
	if (name.hasPrefix("debug_") && name != "debug_click") {
		handleDebugAction(id, name, data);
		return;
	}

	_currentAction = name;
	_lastActivity = g_system->getMillis();
	_lastActionAt = _lastActivity;

	if (name == "choose_dialogue") {
		Common::JSONValue *args = data.empty() ? nullptr : Common::JSON::parse(data);
		actionChooseDialogue(id, args);
		delete args;
		return;
	}

	if (name == "debug_click") {
		int x = 0, y = 0;
		sscanf(data.c_str(), "%d,%d", &x, &y);
		sendActionResult(id, true);
		clickInRoom(x, y);
		return;
	}
	if (name == "read_journal") {
		Common::JSONValue *args = data.empty() ? nullptr : Common::JSON::parse(data);
		bool completed = false;
		if (args && args->isObject() && args->hasChild("show_completed") && args->child("show_completed")->isBool())
			completed = args->child("show_completed")->asBool();
		delete args;
		sendActionResult(id, true, _journal.describe(completed));
		return;
	}
	if (name == "add_note") {
		Common::JSONValue *args = data.empty() ? nullptr : Common::JSON::parse(data);
		Common::String text;
		if (args && args->isObject() && args->hasChild("text") && args->child("text")->isString())
			text = args->child("text")->asString();
		delete args;
		text.trim();
		if (text.empty()) {
			sendActionResult(id, false, "add_note needs some text.");
		} else {
			_journal.addNote(text);
			sendActionResult(id, true, "Noted.");
		}
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
