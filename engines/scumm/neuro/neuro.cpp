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
#include "common/textconsole.h"

#include "scumm/actor.h"
#include "scumm/object.h"
#include "scumm/scumm.h"

#ifndef USE_BASIC_NET
#error "The Neuro-sama integration needs libcurl. Install it, or configure without --enable-engine=neuro"
#endif

namespace Scumm {

static const uint32 kReconnectDelayMs = 5000;
static const int kIdleForceSeconds = 45;
/** Dialogue sent as context goes out after a pause this long, or once it has waited this long. */
static const uint32 kBatchPauseMs = 1000;
static const uint32 kBatchMaxMs = 5000;
/** A request to register everything again this soon after doing so is not acted on. */
static const uint32 kJustRegisteredMs = 3000;

Common::String neuroSetting(const char *env, const char *key) {
	const char *value = getenv(env);
	if (value && *value)
		return value;
	Common::String configKey(key ? key : env);
	if (!key)
		configKey.toLowercase();
	return ConfMan.hasKey(configKey) ? ConfMan.get(configKey) : Common::String();
}

static int numberSetting(const char *env, int fallback) {
	const Common::String value = neuroSetting(env);
	return value.empty() ? fallback : atoi(value.c_str());
}

NeuroBridge *NeuroBridge::create(ScummEngine *vm) {
	const Common::String url = neuroSetting("NEURO_SDK_WS_URL", "neuro_ws_url");

	// The room extraction tool runs without a server
	const char *extract = getenv("NEURO_EXTRACT_DIR");
	if (!extract || !*extract)
		extract = getenv("NEURO_DUMP_SCRIPTS");
	if (url.empty() && !(extract && *extract))
		return nullptr;

	// Versions 1 to 6 with a verb bar. Version 0, the Humongous games and
	// versions 7 and 8 keep their state differently, and the games without
	// verbs need another way to act.
	const bool known = vm->_game.version >= 1 && vm->_game.version <= 6 && vm->_game.heversion == 0
		&& vm->_game.platform != Common::kPlatformNES;
	const bool hasVerbs = vm->_game.id != GID_SAMNMAX && vm->_game.id != GID_LOOM;
	if (!url.empty() && !(known && hasVerbs)) {
		warning("Neuro: The integration does not support this game (%s, SCUMM version %d) and stays off", vm->_game.gameid, vm->_game.version);
		return nullptr;
	}
	return new NeuroBridge(vm, url);
}

NeuroBridge::NeuroBridge(ScummEngine *vm, const Common::String &url) : _vm(vm), _url(url) {
	_dialogueAsContext = neuroSetting("NEURO_DIALOGUE_MODE").equalsIgnoreCase("context");

	const Common::String descriptions = neuroSetting("NEURO_DESCRIPTIONS");
	if (!descriptions.empty())
		_data.load(descriptions);
	else
		warning("Neuro: No description file (NEURO_DESCRIPTIONS): she gets the game's own names and nothing else");

	// The API identifies a game by its display name, which must never change
	const NeuroData::Game &game = _data.game();
	_gameName = game.name.empty() ? Common::String(_vm->_game.gameid) : game.name;
	_walkVerb = game.walkVerb.empty() ? Common::String("Walk to") : game.walkVerb;

	_journal.loadGoals(_data.ini());
	_journal.setExpander([](void *bridge, const Common::String &text) { return ((NeuroBridge *)bridge)->expandText(text); }, this);
	// Starting straight from a savegame (-x) loads it before the first frame
	if (ConfMan.hasKey("save_slot") && ConfMan.getInt("save_slot") >= 0)
		loadJournal(ConfMan.getInt("save_slot"));

	const Common::String fastMode = neuroSetting("NEURO_FAST_MODE");
	if (fastMode.equalsIgnoreCase("always"))
		_fastMode = kFastAlways;
	else if (fastMode.equalsIgnoreCase("scenes"))
		_fastMode = kFastScenes;

	const Common::String debugActions = neuroSetting("NEURO_DEBUG_ACTIONS");
	if (debugActions.equalsIgnoreCase("all"))
		_debugActions = kDebugAll;
	else if (debugActions.equalsIgnoreCase("operator"))
		_debugActions = kDebugOperator;

	_pointerGlide = !neuroSetting("NEURO_POINTER").equalsIgnoreCase("off");
	_allowSkip = !neuroSetting("NEURO_SKIP_CUTSCENES").equalsIgnoreCase("off");
	_contextTokens = numberSetting("NEURO_CONTEXT_TOKENS", _contextTokens);
	_reactionMs = numberSetting("NEURO_REACTION_MS", _reactionMs);
	_idleForceMs = MAX(0, numberSetting("NEURO_IDLE_FORCE_SECONDS", kIdleForceSeconds)) * 1000;

	startExtraction();

	// Line ids must stay unique across restarts of the game
	Common::RandomSource rnd("neuro_speech");
	_speechIdPrefix = Common::String::format("%08x", rnd.getRandomNumber(0x7FFFFFFF));
}

Actor *NeuroBridge::ego() const {
	return _vm->_actors[_vm->VAR(_vm->VAR_EGO)];
}

Common::String NeuroBridge::egoName() {
	// A game with one hero names it in the description file; where the player
	// switches between characters, the game's own name for the current one serves
	Common::String name = _data.game().egoName;
	if (name.empty())
		name = actorName(_vm->VAR(_vm->VAR_EGO));
	return name.empty() ? Common::String("your character") : name;
}

Common::String NeuroBridge::egoNameCapital() {
	Common::String name = egoName();
	name.setChar(toupper(name[0]), 0);
	return name;
}

bool NeuroBridge::untouchable(int thing) const {
	if (_vm->getClass(thing, kObjectClassUntouchable))
		return true;
	// Versions 1 and 2 keep it in the object's state instead
	if (_vm->_game.version > 2 || _vm->objIsActor(thing))
		return false;
	const int index = _vm->getObjectIndex(thing);
	return index > 0 && (_vm->_objs[index].state & kObjectStateUntouchable);
}

bool NeuroBridge::playerHasControl() const {
	return _vm->_userPut > 0 && _vm->_cursor.state > 0;
}

bool NeuroBridge::sentenceRunning() const {
	// Versions 1 and 2 have no variable for it: there it is always this script
	static const int kOldSentenceScript = 2;
	return _vm->isScriptRunning(_vm->VAR_SENTENCE_SCRIPT == 0xFF ? kOldSentenceScript : _vm->VAR(_vm->VAR_SENTENCE_SCRIPT));
}

void NeuroBridge::update() {
	if (_extract.phase != kExtractOff) {
		updateExtraction();
		return;
	}

	updatePanel();

	if (!_ws.isConnected()) {
		if (_wasConnected) {
			warning("Neuro: Connection to %s lost", _url.c_str());
			_wasConnected = false;
			resetInteraction();
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
	while (_ws.receive(message)) {
		updateInteraction();
		handleMessage(message);
	}
	if (!_ws.isConnected())
		return;
	updateInteraction();

	if (_interaction.prompt.question().holdVar)
		_vm->writeVar(_interaction.prompt.question().holdVar, _interaction.prompt.question().holdValue);

	if (_heldSpeech.waiting && g_system->getMillis() >= _heldSpeech.dueAt)
		sendHeldSpeech();

	flushDrawnText();
	pressKeys();
	updateSpeed();
	updateClick();
	updatePager();
	updateQueuedCommand();

	if (!_pendingDialogue.empty()) {
		const uint32 now = g_system->getMillis();
		// Normally wait for a pause, but do not let endless chatter hold lines back
		if ((!_speechOpen && now - _lastTalkTime > kBatchPauseMs) || now - _pendingSince > kBatchMaxMs)
			flushDialogue();
	}

	pollWorld();
}

void NeuroBridge::onConnected() {
	debug(1, "Neuro: Connected to %s as '%s'", _url.c_str(), _gameName.c_str());
	send("startup");
	registerActions();

	sendIntro();

	// Describe the current room again from scratch
	_described = NeuroWorld();
	_speechOpen = false;
	_heldSpeech.waiting = false;
	_lastActivity = g_system->getMillis();
}

void NeuroBridge::sendIntro() {
	sendContext(Common::String::format(
		"You are playing %s, a classic point-and-click adventure game. %s"
		"You cannot see the screen, so each location and what is in it will be described to you. "
		"Things that share a name are numbered from left to right. "
		"Your goals are kept in a journal that you can read at any time with read_journal.",
		_gameName.c_str(), _data.game().intro.empty() ? "" : (_data.game().intro + " ").c_str()), true);
	_introSentAt = _tokensSent;
}

void NeuroBridge::onSaveLoad(bool saved, int slot) {
	// The journal travels with each savegame in a file of its own
	if (saved) {
		saveJournal(slot);
	} else {
		resetAfterLoad();
		loadJournal(slot);
	}
}

void NeuroBridge::updateInteraction() {
	const int room = _vm->_currentRoom;
	if (_interaction.prompt.expired(room, g_system->getMillis()))
		endPrompt();

	if (_interaction.outcome.leftRoom(room)) {
		// Coordinates and in-flight clicks belonged to the old room. A named
		// follow-up is resolved against the new room when control returns.
		cancelClicks();
		resetPager();
		if (!_interaction.queue.empty() && _interaction.queue[0].preset.empty()) {
			_interaction.queue.clear();
			sendContext("The room changed before the follow-up movement could start.", false);
			_interaction.outcome.report();
		}
		_interaction.outcome.enteredRoom(room);
	}
}

void NeuroBridge::endPrompt(bool notify) {
	if (!_interaction.prompt.active())
		return;
	const Common::String action = _interaction.prompt.action();
	_interaction.prompt = NeuroInteraction::Prompt();
	if (notify && _ws.isConnected())
		registerActions(action.c_str());
}

void NeuroBridge::cancelQueuedInput() {
	cancelClicks();
	_interaction.queue.clear();
}

void NeuroBridge::resetInteraction() {
	endPrompt();
	_interaction = NeuroInteraction();
	_forcedDialogue.clear();
	_pendingAlert = -1;
	_openedDoor.clear();
	cancelClicks();
	resetPager();
	_walkTarget = 0;
	_lastTargets[0] = _lastTargets[1] = 0;
	_lastHeard.clear();
}

void NeuroBridge::resetAfterLoad() {
	resetInteraction();
	_controlSince = _speechBlockSince = 0;
	_actorSeen.clear();
	_leftAt.clear();
	_lastLine.clear();
	_lastLineText.clear();
	_drawnText.clear();
	_lastDrawnText.clear();
	_strangers.clear();
	_voiceColors.clear();
	_described = NeuroWorld();
	_pendingDialogue.clear();
	if (_speechOpen && !_heldSpeech.waiting && !_dialogueAsContext)
		sendSpeechEnded();
	_speechOpen = false;
	_heldSpeech.waiting = false;
	_namedCostume.clear();
	_recentLines.clear();
	_lastActivity = g_system->getMillis();
	_seenRooms.clear();
	_watched.clear();
	sendContext("A saved game was loaded. Forget where you were; your situation is described again below.", true);
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
	if (fast && !(_vm->_fastMode & 1)) {
		_vm->_fastMode |= 1;
		_speedSet = true;
	} else if (!fast && _speedSet) {
		_vm->_fastMode &= ~1;
		_speedSet = false;
	}
}

void NeuroBridge::send(const char *command, const Common::JSONObject *data) {
	Common::JSONObject root;
	root.setVal("command", new Common::JSONValue(command));
	root.setVal("game", new Common::JSONValue(_gameName));
	if (data)
		root.setVal("data", new Common::JSONValue(*data));

	const Common::String text = Common::JSONValue(root).stringify();
	// Close enough for deciding when she will have forgotten something
	_tokensSent += text.size() / 4;
	debug(2, "Neuro: > %s", text.c_str());
	_ws.sendText(text);
}

void NeuroBridge::sendContext(const Common::String &message, bool silent) {
	Common::JSONObject data;
	data.setVal("message", new Common::JSONValue(message));
	data.setVal("silent", new Common::JSONValue(silent));
	send("context", &data);
}

void NeuroBridge::sendForce(const Common::String &state, const Common::String &query, const char *actionName, const Common::String &priority,
		const NeuroInteraction::Question &question) {
	Common::JSONArray names;
	names.push_back(new Common::JSONValue(actionName));

	Common::JSONObject data;
	data.setVal("state", new Common::JSONValue(state));
	data.setVal("query", new Common::JSONValue(query));
	data.setVal("ephemeral_context", new Common::JSONValue(true));
	if (priority == "medium" || priority == "high" || priority == "critical")
		data.setVal("priority", new Common::JSONValue(priority));
	data.setVal("action_names", new Common::JSONValue(names));
	send("actions/force", &data);

	_interaction.prompt.open(actionName, _vm->_currentRoom, g_system->getMillis(), question);
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
	if (success && _interaction.prompt.answeredBy(_currentAction))
		endPrompt();
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
			if (g_system->getMillis() - _registeredAt > kJustRegisteredMs)
				registerActions();
		}
	}

	delete root;
}

void NeuroBridge::handleAction(const Common::String &id, const Common::String &rawName, const Common::String &data) {
	// "Choose-Dialogue" is still choose_dialogue
	Common::String name = rawName;
	name.trim();
	name.toLowercase();
	for (uint i = 0; i < name.size(); i++) {
		if (name[i] == '-' || name[i] == ' ')
			name.setChar('_', i);
	}

	// The operator's and the developer's aids are not her actions, and only work where they were asked for
	if (name.hasPrefix("debug_")) {
		_currentAction.clear();
		handleDebugAction(id, name, data);
		return;
	}

	_currentAction = name;
	_lastActivity = g_system->getMillis();
	_lastActionAt = _lastActivity;

	const NeuroArgs args(data);
	notePanelAction(name, args);

	if (name == "choose_dialogue") {
		actionChooseDialogue(id, args);
		return;
	}

	if (name == "answer") {
		actionAnswer(id, args);
		return;
	}

	if (name == "check_inventory") {
		NeuroWorld world;
		collectInventory(world);
		sendActionResult(id, true, "Inventory: " + neuroListNames(world.inventory));
		return;
	}

	if (name == "read_journal") {
		Common::String completed = args.get("show_completed|completed|show_done|all", true);
		completed.toLowercase();
		// One goal's clues at a time: all of them together would be a wall of text
		const Common::String cluesFor = args.get("clues_for|clues|goal|show_clues");
		if (!cluesFor.empty())
			sendActionResult(id, true, _journal.describeClues(cluesFor));
		else
			sendActionResult(id, true, _journal.describe(completed == "true" || completed == "yes" || completed == "1"));
		return;
	}
	if (name == "add_note") {
		const Common::String text = args.get("text|note|content|message", true);
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

	if (name == "skip_cutscene" && !_allowSkip) {
		sendActionResult(id, false, "Scenes cannot be skipped.");
		return;
	}
	if (name == "skip_cutscene") {
		actionSkipCutscene(id);
		return;
	}

	if (name == "interact") {
		actionInteract(id, args);
		return;
	}

	sendActionResult(id, false, Common::String::format("Unknown action '%s'.", name.c_str()));
}

} // End of namespace Scumm
