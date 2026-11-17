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

	_dialogueAsContext = neuroSetting("NEURO_DIALOGUE_MODE").equalsIgnoreCase("context");

	const Common::String descriptions = neuroSetting("NEURO_DESCRIPTIONS");
	if (!descriptions.empty())
		_data.load(descriptions);

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

bool NeuroBridge::playerHasControl() const {
	return _vm->_userPut > 0 && _vm->_cursor.state > 0;
}

bool NeuroBridge::sentenceRunning() const {
	return _vm->isScriptRunning(_vm->VAR(_vm->VAR_SENTENCE_SCRIPT));
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

	if (_questionHoldVar) {
		if (_questionCommand.empty() || g_system->getMillis() - _questionAskedAt > kQuestionOpenMs)
			_questionHoldVar = 0;
		else
			_vm->writeVar(_questionHoldVar, _questionHoldValue);
	}

	if (_heldSpeech.waiting && g_system->getMillis() >= _heldSpeech.dueAt)
		sendHeldSpeech();
	if (_speechEndDueAt && (g_system->getMillis() >= _speechEndDueAt || playerHasControl()))
		sendSpeechEnded();

	flushDrawnText();
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
	_speechEndDueAt = 0;
	_heldSpeech.waiting = false;
	_forceActive = false;
	_forcedDialogue.clear();
	_lastActivity = g_system->getMillis();
}

void NeuroBridge::sendIntro() {
	sendContext(Common::String::format(
		"You are playing %s, a classic point-and-click adventure game. "
		"You control Guybrush Threepwood, who wants to become a pirate. "
		"You cannot see the screen, so each location and what is in it will be described to you. "
		"Things that share a name are numbered from left to right. "
		"Your goals are kept in a journal that you can read at any time with read_journal.",
		_gameName.c_str()), true);
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

void NeuroBridge::resetAfterLoad() {
	_described = NeuroWorld();
	_queue.clear();
	_clickPhase = kClickIdle;
	_clickQueue.clear();
	_shownSentence = false;
	_pendingDialogue.clear();
	_speechOpen = false;
	_heldSpeech.waiting = false;
	if (_speechEndDueAt)
		sendSpeechEnded();
	_namedCostume.clear();
	_recentLines.clear();
	_lastActivity = g_system->getMillis();
	_seenRooms.clear();
	_watched.clear();
	_pagerState = kPagerIdle;
	_pagerLines.clear();
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

void NeuroBridge::sendForce(const Common::String &state, const Common::String &query, const char *actionName, const Common::String &priority) {
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

	_forceActive = true;
	_forceAction = actionName;
	_forceSentAt = g_system->getMillis();
}

void NeuroBridge::sendActionResult(const Common::String &id, bool success, const Common::String &message) {
	// Keep what she is told in the order it happened
	flushDialogue();

	// A force is over once the forced action succeeds; the server retries a
	// failed one itself. She may answer with a different action than the one
	// named, and anything that did something settles it. Reading, noting and
	// waiting do nothing, and may have been sent before the prompt arrived.
	const bool passive = _currentAction == "read_journal" || _currentAction == "add_note" || _currentAction == "wait";
	if (success && _forceActive && (_currentAction == _forceAction || !passive))
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
