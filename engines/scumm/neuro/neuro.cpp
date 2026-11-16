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
#include "scumm/object.h"
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
	_journal.setVarReader([](void *vm, int var) { return ((ScummEngine *)vm)->readVar(var); }, _vm);
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

	// NEURO_POINTER=off makes clicks land at once, without the pointer travelling
	const char *pointer = getenv("NEURO_POINTER");
	if (pointer && *pointer)
		_pointerGlide = !Common::String(pointer).equalsIgnoreCase("off");
	else if (ConfMan.hasKey("neuro_pointer"))
		_pointerGlide = !ConfMan.get("neuro_pointer").equalsIgnoreCase("off");

	const char *contextTokens = getenv("NEURO_CONTEXT_TOKENS");
	if (contextTokens && *contextTokens)
		_contextTokens = atoi(contextTokens);
	else if (ConfMan.hasKey("neuro_context_tokens"))
		_contextTokens = ConfMan.getInt("neuro_context_tokens");

	const char *reaction = getenv("NEURO_REACTION_MS");
	if (reaction && *reaction)
		_reactionMs = atoi(reaction);
	else if (ConfMan.hasKey("neuro_reaction_ms"))
		_reactionMs = ConfMan.getInt("neuro_reaction_ms");

	const char *idle = getenv("NEURO_IDLE_FORCE_SECONDS");
	int idleSeconds = 45;
	if (idle && *idle)
		idleSeconds = atoi(idle);
	else if (ConfMan.hasKey("neuro_idle_force_seconds"))
		idleSeconds = ConfMan.getInt("neuro_idle_force_seconds");
	_idleForceMs = MAX(0, idleSeconds) * 1000;

	startExtraction();

	// Line ids must stay unique across restarts of the game
	Common::RandomSource rnd("neuro_speech");
	_speechIdPrefix = Common::String::format("%08x", rnd.getRandomNumber(0x7FFFFFFF));
}

void NeuroBridge::update() {
	if (_extract.phase != kExtractOff) {
		updateExtraction();
		return;
	}

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

	flushDrawnText();
	updateSpeed();
	updateClick();
	updatePager();
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
	_clickQueue.clear();
	_shownSentence = false;
	_pendingDialogue.clear();
	_speechOpen = false;
	_namedCostume.clear();
	_recentLines.clear();
	_lastActivity = g_system->getMillis();
	_seenRooms.clear();
	_watched.clear();
	_pagerState = kPagerIdle;
	_pagerLines.clear();
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
	_outcomeTold |= cue;
}

void NeuroBridge::onConnected() {
	debug(1, "Neuro: Connected to %s as '%s'", _url.c_str(), _gameName.c_str());
	send("startup");
	registerActions();

	sendIntro();

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
	wait.setVal("description", new Common::JSONValue("Let the game continue for a moment."));

	Common::JSONObject skip;
	skip.setVal("name", new Common::JSONValue("skip_cutscene"));
	skip.setVal("description", new Common::JSONValue("Skip the current cutscene."));

	// Once the game's verbs are known they are offered as an enum
	Common::JSONValue *verbSchema = stringSchema();
	if (!_offeredVerbs.empty()) {
		Common::JSONArray names;
		for (const auto &verb : _offeredVerbs)
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
		"Use a verb on something here or in your inventory. \"Walk to\" moves you. "
		"second_target: what an item is used on or given to. "
		"then_verb and then_target: an optional second step, done the instant the first ends."));
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
		"In a conversation, say the line with this number."));
	choose.setVal("schema", new Common::JSONValue(optionSchema));

	Common::JSONArray actions;
	actions.push_back(new Common::JSONValue(interact));
	Common::JSONObject readJournal;
	readJournal.setVal("name", new Common::JSONValue("read_journal"));
	readJournal.setVal("description", new Common::JSONValue(
		"Read your goals and notes. clues_for: name a goal to read its clues instead. show_completed adds finished goals."));
	Common::JSONObject completedType;
	completedType.setVal("type", new Common::JSONValue("boolean"));
	Common::JSONObject journalProperties;
	journalProperties.setVal("show_completed", new Common::JSONValue(completedType));
	journalProperties.setVal("clues_for", stringSchema());
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
		"Add a short note to your journal."));
	addNote.setVal("schema", new Common::JSONValue(noteSchema));

	Common::JSONArray yesNo;
	yesNo.push_back(new Common::JSONValue("yes"));
	yesNo.push_back(new Common::JSONValue("no"));
	Common::JSONObject answerType;
	answerType.setVal("type", new Common::JSONValue("string"));
	answerType.setVal("enum", new Common::JSONValue(yesNo));
	Common::JSONObject answerProperties;
	answerProperties.setVal("answer", new Common::JSONValue(answerType));
	Common::JSONArray answerRequired;
	answerRequired.push_back(new Common::JSONValue("answer"));
	Common::JSONObject answerSchema;
	answerSchema.setVal("type", new Common::JSONValue("object"));
	answerSchema.setVal("properties", new Common::JSONValue(answerProperties));
	answerSchema.setVal("required", new Common::JSONValue(answerRequired));
	Common::JSONObject answer;
	answer.setVal("name", new Common::JSONValue("answer"));
	answer.setVal("description", new Common::JSONValue(
		"Answer a yes/no question from the game. Yes does what it offered, at once."));
	answer.setVal("schema", new Common::JSONValue(answerSchema));

	Common::JSONValue *others[] = {new Common::JSONValue(choose), new Common::JSONValue(wait), new Common::JSONValue(skip),
		new Common::JSONValue(readJournal), new Common::JSONValue(addNote), new Common::JSONValue(answer)};
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

	// A force is over once the forced action succeeds; the server retries a failed one itself
	// She may answer a prompt with a different action than the one it named
	// (leaving a close-up by skipping it); anything that did something settles it
	// Waiting changes nothing either, and may have been sent before the prompt arrived
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
			if (g_system->getMillis() - _registeredAt > 3000)
				registerActions();
		}
	}

	delete root;
}

void NeuroBridge::handleDebugAction(const Common::String &id, const Common::String &name, const Common::String &data) {
	// The dev driver sends these with the id "dbg", straight to the game: the
	// server never issued them and would only warn about an unexpected result
	const bool quiet = id == "dbg";
	// Development aid, deliberately not registered: saves a screenshot
	// to ScummVM's screenshot directory.
	if (name == "debug_screenshot") {
		g_system->saveScreenshot();
		if (!quiet) sendActionResult(id, true);
		return;
	}

	if (name == "debug_dump") {
		debugDump();
		if (!quiet) sendActionResult(id, true);
		return;
	}
	if (name == "debug_save" || name == "debug_load") {
		int slot = 0;
		char label[64] = "";
		sscanf(data.c_str(), "%d %63[^\n]", &slot, label);
		if (!quiet) sendActionResult(id, true);
		if (name == "debug_save")
			_vm->requestSave(slot, label[0] ? label : "neuro");
		else
			_vm->requestLoad(slot);
		return;
	}
	if (name == "debug_speed") {
		_fastMode = kFastOff;
		_vm->_fastMode = atoi(data.c_str());
		if (!quiet) sendActionResult(id, true);
		return;
	}
	if (name == "debug_sentence") {
		int verb = 0, a = 0, b = 0;
		sscanf(data.c_str(), "%d %d %d", &verb, &a, &b);
		if (!quiet) sendActionResult(id, true);
		_vm->doSentence(verb, a, b);
		return;
	}
	if (name == "debug_entry") {
		// "OBJ" or "OBJ VERB": where an object is, who owns it, which verbs it handles
		int obj = 0, verb = -1;
		sscanf(data.c_str(), "%d %d", &obj, &verb);
		Common::String text = Common::String::format("object %d: where %d, owner %d, class bits %x; handles verbs:",
			obj, _vm->whereIsObject(obj), _vm->objIsActor(obj) ? -1 : _vm->getOwner(obj), _vm->objIsActor(obj) ? 0 : _vm->_classData[obj]);
		for (int v = 1; v < 256; v++) {
			if ((verb < 0 || v == verb) && _vm->getVerbEntrypoint(obj, v))
				text += Common::String::format(" %d", v);
		}
		debug("NEURODUMP %s", text.c_str());
		if (!quiet) sendActionResult(id, true, text);
		return;
	}
	if (name == "debug_give") {
		// "OBJECT ROOM": put an object from that room into the inventory, as picking it up does
		int object = 0, room = 0;
		if (sscanf(data.c_str(), "%d %d", &object, &room) == 2) {
			_vm->addObjectToInventory(object, room);
			_vm->putOwner(object, _vm->VAR(_vm->VAR_EGO));
			_vm->putClass(object, kObjectClassUntouchable, 1);
			_vm->putState(object, 1);
			_vm->runInventoryScript(1);
		}
		if (!quiet) sendActionResult(id, true);
		return;
	}
	if (name == "debug_var" || name == "debug_bit") {
		// "N" reads a game variable (or bit variable), "N VALUE" writes it
		int number = 0, value = 0;
		const int given = sscanf(data.c_str(), "%d %d", &number, &value);
		const uint var = name == "debug_bit" ? (0x8000 | number) : number;
		if (given == 2)
			_vm->writeVar(var, value);
		const Common::String text = Common::String::format("%s %d = %d", name.c_str() + 6, number, _vm->readVar(var));
		debug("NEURODUMP %s", text.c_str());
		if (!quiet) sendActionResult(id, true, text);
		return;
	}
	if (name == "debug_room") {
		const int room = atoi(data.c_str());
		if (!quiet) sendActionResult(id, true);
		_vm->_actors[_vm->VAR(_vm->VAR_EGO)]->_room = room;
		_vm->startScene(room, nullptr, 0);
		_vm->_fullRedraw = true;
		return;
	}

	if (!quiet) sendActionResult(id, false, "Unknown debug action.");
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

	// Development aids are not her actions
	if (name.hasPrefix("debug_") && name != "debug_click") {
		handleDebugAction(id, name, data);
		return;
	}

	_currentAction = name;
	_lastActivity = g_system->getMillis();
	_lastActionAt = _lastActivity;

	const NeuroArgs args(data);

	if (name == "choose_dialogue") {
		actionChooseDialogue(id, args);
		return;
	}

	if (name == "debug_click") {
		int x = 0, y = 0;
		sscanf(data.c_str(), "%d,%d", &x, &y);
		sendActionResult(id, true);
		clickInRoom(x, y);
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
