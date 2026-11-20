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

#ifndef SCUMM_NEURO_NEURO_H
#define SCUMM_NEURO_NEURO_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/rect.h"
#include "common/formats/json.h"
#include "common/str.h"

#include "scumm/neuro/args.h"
#include "scumm/neuro/data.h"
#include "scumm/neuro/journal.h"
#include "scumm/neuro/websocket.h"

namespace Scumm {

class Actor;
class ScummEngine;

/**
 * A setting, from the environment variable or else the config key of the same
 * name in lower case (or the key given). Empty if neither is set.
 */
Common::String neuroSetting(const char *env, const char *key = nullptr);

/** Something with a name: an object, an actor, an inventory item, a verb or a place to walk to. */
struct NeuroThing {
	int id = 0;

	/** Its middle in the room; for a verb, the left edge and top of its text on screen. */
	int x = 0, y = 0;

	int state = 0;
	Common::String name;
	// Inventory rules use the game's own name even when the displayed name differs.
	Common::String inventoryGameName;

	/** Extra words for the room listing; not part of the name. */
	Common::String note;
	bool stateNote = false;
};

typedef Common::Array<NeuroThing> NeuroThingList;

/** The names in both lists, separated by commas; "nothing" if there are none. */
Common::String neuroListNames(const NeuroThingList &a, const NeuroThingList &b = NeuroThingList());

/** A simulated mouse click, in room or screen coordinates. */
struct NeuroClick {
	int x = 0, y = 0;
	bool screen = false;

	/** A character to stay on: one that keeps moving is not where it was when the click was planned. */
	int follow = 0;

	/** False for a place the pointer only travels to: the command itself is given another way. */
	bool press = true;
};

/** A validated verb sentence or walk, ready to run. */
struct NeuroCommand {
	bool walk = false;
	int verb = 0;
	int objects[2] = {0, 0};

	// Where to click, for walking
	int x = 0, y = 0;
	bool isSpot = false;
	/** A place that only reacts to a second step, taken from right next to it. */
	bool stepTwice = false;

	/** As a follow-up: how long the player's character must have stood still before it starts. */
	uint32 settleMs = 0;

	/** As a follow-up: it comes after a walk across the room, which may take a while. */
	bool afterPassage = false;

	/** What she is told on success. */
	Common::String note;

	/** Not yet resolved: "VERB | TARGET | SECOND", worked out when it is due to run. */
	Common::String preset;

	/** When set, the command is carried out as these clicks, as a player would. */
	Common::Array<NeuroClick> clicks;

	bool sameAction(const NeuroCommand &other) const {
		return objects[0] && walk == other.walk && verb == other.verb &&
			objects[0] == other.objects[0] && objects[1] == other.objects[1] &&
			(!isSpot || (x == other.x && y == other.y));
	}
};

/** A prompt can arrive while the previous action's outcome is still settling. */
struct NeuroInteraction {
	struct Question {
		Common::String command, thenCommand;
		int holdVar = 0, holdValue = 0;
		int readyAlert = -1;
	};

	class Prompt {
	public:
		void open(const char *action, int room, uint32 now, const Question &question) {
			_action = action;
			_idle = false;
			_room = room;
			_startedAt = now;
			_question = question;
		}
		bool active() const { return !_action.empty(); }
		void markIdle() { _idle = true; }
		bool idle() const { return _idle; }
		const Common::String &action() const { return _action; }
		const Question &question() const { return _question; }
		uint32 age(uint32 now) const { return now - _startedAt; }
		bool expired(int room, uint32 now) const {
			const uint32 lifetime = _action == "answer" ? 20000 : 90000;
			return active() && (room != _room || age(now) > lifetime);
		}
		bool answeredBy(const Common::String &name) const {
			if (!active() || name.empty())
				return false;
			return name == _action || name == "interact" || name == "choose_dialogue" || name == "skip_cutscene";
		}

	private:
		Common::String _action;
		bool _idle = false;
		int _room = 0;
		uint32 _startedAt = 0;
		Question _question;
	} prompt;

	class Outcome {
	public:
		void begin(int room, uint32 now, const NeuroCommand &command) {
			_command = command;
			_room = room;
			_startedAt = now;
			_pending = true;
			_reported = _spoken = false;
		}
		bool pending() const { return _pending; }
		bool running(int room, const NeuroCommand &command) const {
			return _pending && _room == room && _command.sameAction(command);
		}
		bool reported() const { return _reported; }
		bool spoken() const { return _spoken; }
		void report() { _reported = _pending; }
		void hearSpeech() { _spoken = _pending; }
		void finish() { _pending = false; }
		void dispatchedAt(uint32 now) { _startedAt = now; }
		uint32 age(uint32 now) const { return now - _startedAt; }
		bool leftRoom(int room) const { return _pending && _room != room; }
		void enteredRoom(int room) { _room = room; _command = NeuroCommand(); }

	private:
		NeuroCommand _command;
		int _room = 0;
		uint32 _startedAt = 0;
		bool _pending = false, _reported = false, _spoken = false;
	} outcome;

	Common::Array<NeuroCommand> queue;
};

/** Everything the player could see or use at one moment. */
struct NeuroWorld {
	Common::HashMap<int, int> visibleCostumes;
	int room = 0;
	NeuroThingList objects;
	NeuroThingList actors;
	NeuroThingList inventory;
	NeuroThingList verbs;

	/** Places without an object that can still be walked to; id is -(index + 1). */
	NeuroThingList spots;
	uint authoredSpotCount = 0;
	Common::Array<Common::Point> spotPositions;
	Common::Array<bool> spotTwice;

	/** Which part of the room the player's character is in, where the description file tells them apart. */
	Common::String standing;
	Common::StringArray scenery;
	Common::HashMap<int, bool> readyAlerts;

	/** The verb area shows lines to say instead of verbs. */
	bool inDialogue = false;

	/** The verb area shows other verbs than the usual ones. */
	bool customVerbs = false;
};

/**
 * Lets Neuro-sama play the game through the Neuro API
 * (https://github.com/VedalAI/neuro-sdk).
 *
 * neuro.cpp     settings, the connection, messages in and out
 * actions.cpp   her actions and carrying out a command
 * dialogue.cpp  lines to say, and prompting her
 * pointer.cpp   simulated clicks and the travelling pointer
 * world.cpp     what is in the room, and what changed
 * speech.cpp    what is said and shown as text
 * alerts.cpp    alerts and watched variables from the description file
 * panel.cpp     the column beside the game in a stream window
 * debug.cpp     the operator's and the developer's unregistered actions
 * extract.cpp   the developer's room and script dump
 */
class NeuroBridge {
public:
	/**
	 * Returns nullptr unless a server URL is configured, either in the
	 * NEURO_SDK_WS_URL environment variable or the neuro_ws_url config key.
	 */
	static NeuroBridge *create(ScummEngine *vm);

	/** Called once per engine loop iteration. */
	void update();

	void sendContext(const Common::String &message, bool silent);

	/** A character (or the narrator, for actor 0xFF) started saying a line. */
	void onTalkStarted(int actor, const byte *text);
	void onTalkStopped();

	/** Text was drawn on the screen that nobody says (a sign in close-up, a title). */
	void onTextDrawn(const byte *text, int color);

	/** A script gave an actor a name. */
	void onActorNamed(int actor);
	void onActorAnimation(int actor, int costume, int animation);
	void onActorPlacedAtObject(int actor, int object);

	/** The engine has just written or read a savegame. */
	void onSaveLoad(bool saved, int slot);

	/** While a simulated click is in progress, points the mouse at its target. */
	void overridePointer();
	/** Draws the pointer on its way to what is being clicked; called for every frame shown. */
	void movePointer();

	/** What Neuro would be told on entering the current room. */
	Common::String describeWorld();

private:
	NeuroBridge(ScummEngine *vm, const Common::String &url);

	ScummEngine *_vm;
	Common::String _url;
	NeuroData _data;
	// From the description file's [game] section, with defaults
	Common::String _gameName;
	Common::String _walkVerb;
	/** What the player's character is called, and the same to start a sentence with. */
	Common::String egoName();
	Common::String egoNameCapital();
	/** The actor she was last told she is playing; games with several heroes switch. */
	int _egoTold = 0;
	/** Whether the game keeps an object or actor from being used. */
	bool untouchable(int thing) const;

	Actor *ego() const;
	bool playerHasControl() const;
	/** The game's script that carries out a verb sentence is running. */
	bool sentenceRunning() const;

	// --- The connection, and messages in and out (neuro.cpp)

	WebSocketClient _ws;
	bool _wasConnected = false;
	uint32 _nextConnectAttempt = 0;
	void onConnected();
	void handleMessage(const Common::String &text);
	void handleAction(const Common::String &id, const Common::String &name, const Common::String &data);

	/** Takes ownership of the values in data. */
	void send(const char *command, const Common::JSONObject *data = nullptr);
	void sendActionResult(const Common::String &id, bool success, const Common::String &message = Common::String(), bool completesPrompt = true);
	void sendForce(const Common::String &state, const Common::String &query, const char *actionName, const Common::String &priority = Common::String(),
		const NeuroInteraction::Question &question = NeuroInteraction::Question());
	void sendIntro();

	NeuroInteraction _interaction;
	void endPrompt(bool notify = true);
	void updateInteraction();
	void resetInteraction();
	void cancelClicks();
	void cancelQueuedInput();
	/** The action being handled, and when she last did anything or anything happened. */
	Common::String _currentAction;
	uint32 _lastActionAt = 0;
	uint32 _lastActivity = 0;

	// Things said once are forgotten as her context scrolls on, so they are
	// said again after roughly this many tokens of traffic.
	uint32 _contextTokens = 32000;
	uint64 _tokensSent = 0;
	uint64 _introSentAt = 0;

	// A loaded savegame can change everything at once
	void resetAfterLoad();

	// Run the game at ScummVM's fast speed, always or only while she cannot act
	enum FastMode {
		kFastOff,
		kFastAlways,
		kFastScenes
	};
	FastMode _fastMode = kFastOff;
	/** Whether the engine's fast mode is on because of us, not the player's Ctrl-F. */
	bool _speedSet = false;
	void updateSpeed();

	// --- Her actions (actions.cpp)

	void registerActions(const char *replace = nullptr);
	void unregisterAction(const char *name);
	uint32 _registeredAt = 0;
	/** Whether she is offered skip_cutscene at all. */
	bool _allowSkip = true;
	// The game's normal verbs, learned the first time they are on screen
	NeuroThingList _standardVerbs;
	// The verbs her interact action currently offers: the normal ones, or a scene's own
	NeuroThingList _offeredVerbs;

	void actionInteract(const Common::String &id, const NeuroArgs &args);
	void actionSkipCutscene(const Common::String &id);
	void actionAnswer(const Common::String &id, const NeuroArgs &args);

	/** Finds a thing by the name she used for it; nullptr if there is no single match. */
	const NeuroThing *findThing(const NeuroThingList &things, const Common::String &name) const;
	bool resolveCommand(const NeuroWorld &world, const Common::String &verbName, const Common::String &targetName,
		const Common::String &secondName, NeuroCommand &command, Common::String &error);
	bool resolvePreset(const NeuroWorld &world, const Common::String &preset, NeuroCommand &command);
	bool passageTo(int room, int target, NeuroCommand &passage) const;
	void runCommand(const NeuroCommand &command);
	bool canDispatchSentence() const;
	void dispatchSentence(int verb, int first, int second);

	// Follow-up steps, each run as soon as the one before has played out
	bool _queueSawBusy = false;
	uint32 _queueIdleSince = 0;
	uint32 _lastQueuedStepAt = 0;
	void updateQueuedCommand();

	// After each of her actions she is told how it turned out, so that
	// she can carry on at once instead of waiting to be prompted.
	/** The thing the last command walked to, if it was a walk. */
	int _walkTarget = 0;
	int _encounterScript = 0;
	int _encounterRetries = 0;
	// What her last action was aimed at; only these are worth a note when their look changes
	int _lastTargets[2] = {0, 0};
	void beginAction(const NeuroCommand &command = NeuroCommand());
	void finishAction(const NeuroWorld &world);

	// --- Conversations and prompts (dialogue.cpp)

	void actionChooseDialogue(const Common::String &id, const NeuroArgs &args);
	/** Picks a line on screen; with a real click where the room reads the pointer to see which. */
	void pickLine(int verb);
	Common::String dialogueMenu(const Common::String &lines, int room);

	// The game shows a long list of lines a few at a time, with arrows to
	// scroll it. She is given the whole list, read by scrolling through it,
	// and her choice is scrolled into view before it is clicked.
	enum PagerState {
		kPagerIdle,
		kPagerToTop,
		kPagerDown,
		kPagerSeek
	};
	PagerState _pagerState = kPagerIdle;
	NeuroThingList _pagerLines;
	Common::String _pagerTarget;
	Common::String _pagerSaw;
	uint32 _pagerPressedAt = 0;
	/** The arrows that scroll the lines on screen; 0 for one that cannot be used now. */
	void scrollArrows(const NeuroWorld &world, int &up, int &down);
	bool pagerCovers(const NeuroWorld &world) const;
	void pressArrow(int verb, const NeuroWorld &world);
	void resetPager();
	void updatePager();

	uint32 _idleForceMs = 0;
	/** The lines she was last asked to choose from. */
	Common::String _forcedDialogue;
	Common::String _menuLines;
	int _menuRoom = -1;
	uint32 _menuNumber = 0;
	Common::String _chosenDialogue;
	uint32 _chosenDialogueAt = 0;
	// What she is answering when asked to pick a line
	Common::String _lastHeard;
	uint32 _lastHeardAt = 0;
	void updateForces(const NeuroWorld &world);

	// --- Alerts and watched variables (alerts.cpp)

	void checkAlerts(const NeuroWorld &before, const NeuroWorld &after);
	void collectReadyAlerts(NeuroWorld &world) const;
	void fireAlert(int index, const NeuroWorld &world);
	int _pendingAlert = -1;
	uint32 _pendingAlertDue = 0;
	// How long she takes from prompt to action; alerts are sent that much early
	uint32 _reactionMs = 300;

	// A yes/no question, and what saying yes does
	void ask(const Common::String &state, const Common::String &query, const Common::String &command,
		const Common::String &then, int holdVar = 0, const Common::String &priority = Common::String(), int readyAlert = -1);

	// Game variables whose rise is worth a sentence, with the value last seen
	Common::HashMap<int, int> _watched;
	void checkWatches();
	uint32 _lastKeyPressAt = 0;
	void pressKeys();
	/** Fills in {varN}, {bitsA-B} and {pickN: a | b | c} from the game's variables. */
	Common::String expandText(const Common::String &text) const;
	/** Reads a variable (or, with 0x8000 set, a bit variable) named in the data file; 0 if the game has none by that number. */
	int readDataVar(uint var) const;
	mutable Common::HashMap<uint, bool> _missingVars;

	// --- Simulated clicks and the pointer (pointer.cpp)

	// A simulated click takes a few frames: the game's own hover script
	// has to see the pointer over the target before the button goes down.
	enum ClickPhase {
		kClickIdle,
		kClickMove,
		kClickHover,
		kClickPress,
		kClickRelease
	};
	ClickPhase _clickPhase = kClickIdle;
	int _clickFrames = 0;
	int _clickX = 0, _clickY = 0;
	bool _clickScreen = false;
	bool _clickPress = true;
	int _clickFollow = 0;
	uint32 _lastClickAt = 0;
	// Clicks still to make for the command being carried out, one after another
	Common::Array<NeuroClick> _clickQueue;
	void clickAt(const NeuroClick &click);
	void clickInRoom(int x, int y, int follow = 0);
	void updateClick();
	/** Finds a point in room coordinates where a click lands on the given object or actor. */
	bool findClickPoint(int thing, int &x, int &y) const;

	// The pointer travels to what she acts on, as a hand on the mouse would move
	// it, so that viewers can follow what she is doing.
	bool _pointerGlide = true;
	bool _pointerKnown = false;
	int _pointerX = 0, _pointerY = 0;
	int _moveFromX = 0, _moveFromY = 0;
	uint32 _moveStartedAt = 0, _moveMs = 0;
	void clickTarget(int &x, int &y) const;
	void pointerNow(int &x, int &y) const;
	// A sentence given directly once the pointer has been shown going over its words
	bool _shownSentence = false;
	int _shownVerb = 0, _shownObjects[2] = {0, 0};

	// Some rooms swap in their own mouse handler for a scene, and only that
	// one knows what clicking there should do, so her commands then have to
	// be real clicks.
	bool needsClicks() const;
	bool planClicks(const NeuroWorld &world, NeuroCommand &command);
	bool verbButton(int verbId, NeuroClick &click) const;

	// --- What is in the room, and what changed (world.cpp)

	void collectWorld(NeuroWorld &world);
	bool usableRoomObject(int index) const;
	void collectObjects(NeuroWorld &world, Common::Array<bool> &usable);
	void collectActors(NeuroWorld &world);
	void collectInventory(NeuroWorld &world);
	void collectVerbs(NeuroWorld &world);
	void collectSpots(NeuroWorld &world);
	bool groundBetween(int left, int right, Common::Point &point) const;
	void noteLines(NeuroWorld &world);

	/** The world as she was last told about it. */
	NeuroWorld _described;
	uint32 _nextWorldPoll = 0;
	uint32 _controlSince = 0;
	uint32 _speechBlockSince = 0;
	void pollWorld();
	int _lastShownRoom = -1;
	void trackActors();
	bool worldSettled(uint32 now);
	void reportRoom(const NeuroWorld &world);
	void reportChanges(const NeuroWorld &world);
	void askAboutDoor(const NeuroWorld &world, uint32 now);
	void learnVerbs(const NeuroWorld &world);

	enum RoomDetail {
		kRoomFirstVisit, ///< with the written description
		kRoomRevisit,    ///< just what is there
		kRoomStatus      ///< what is there and what she carries, for a reminder
	};
	Common::String describeRoom(const NeuroWorld &world, RoomDetail detail);
	/** layoutChanged: things appeared or went. comings: characters arrived or left. */
	Common::String describeChanges(const NeuroWorld &before, const NeuroWorld &after, bool &layoutChanged, bool &comings);
	/** Per room: the traffic count when its full description was last sent. */
	Common::HashMap<int, uint64> _seenRooms;

	// Where each character in the room was last seen, and from that the thing
	// each one who left was standing at (by lower-case name)
	int _actorSeenRoom = 0;
	struct SeenActor {
		Common::Point pos;
		Common::String name;
	};
	Common::HashMap<int, SeenActor> _actorSeen;
	Common::HashMap<Common::String, Common::String> _leftAt;
	/**
	 * Named characters seen in this room who are no longer in it; each is reported once.
	 * With known, only those among them: who came and went during a scene is not news.
	 */
	NeuroThingList departed(const NeuroWorld &world, const NeuroThingList *known = nullptr);
	Common::String lastSeenAt(int actor, const NeuroWorld &world) const;

	// A door she has just opened, to ask whether she wants to go through
	Common::String _openedDoor;

	// The costume each actor wore when it was named. Scripts reuse actors
	// for other characters without renaming them, which leaves a wrong name.
	Common::HashMap<int, int> _namedCostume;
	/** forList: for the list of characters in the room, as opposed to naming a speaker. */
	Common::String actorName(int actor, bool forList = false);

	// --- What is said and shown as text (speech.cpp)

	/** Converts game text to UTF-8, dropping control codes and name padding. */
	Common::String cleanText(const byte *text, bool resolveCodes);
	/** Who says the line; false if it is not to be passed on. */
	bool speakerOf(int actor, const Common::String &line, Common::String &speaker);
	/** A line just heard from the same speaker that is not a reaction to something she did. */
	bool isBackgroundChatter(const Common::String &spoken);

	Common::String _speechIdPrefix;
	uint32 _speechCounter = 0;
	bool _speechOpen = false;
	uint32 _lastTalkTime = 0;
	void sendSpeechStarted(const Common::String &speaker, const Common::String &line, bool noReply);
	void sendSpeechEnded();

	// A short line held back in case it is one piece of text being typed out
	struct HeldSpeech {
		bool waiting = false;
		bool ended = false;   ///< it has already left the screen
		bool noReply = false;
		uint32 dueAt = 0;
		Common::String speaker, text;
	};
	HeldSpeech _heldSpeech;
	void sendHeldSpeech();
	bool anotherLineFollows() const;

	/** The last line passed on, as "speaker: text". */
	Common::String _lastLine;
	// Background chatter repeats forever
	struct HeardLine {
		Common::String text;
		uint32 time;
	};
	Common::Array<HeardLine> _recentLines;

	// Speakers nobody has named are told apart by number within a room
	int _strangerRoom = 0;
	Common::Array<int> _strangers;
	// Several characters can share one actor and differ only in the colour
	// of their text; the colours seen so far for each speaker name
	Common::HashMap<Common::String, Common::Array<byte> > _voiceColors;

	// Servers that predate the speech commands get dialogue as batched context instead
	bool _dialogueAsContext = false;
	Common::String _pendingDialogue;
	uint32 _pendingSince = 0;
	/** The last line added to the batch and where it starts, to replace it when it is typed out further. */
	Common::String _lastLineText;
	uint _lastLineOffset = 0;
	void flushDialogue();

	// Text drawn on screen, gathered until the frame is over
	Common::String _drawnText;
	Common::String _lastDrawnText;
	void flushDrawnText();

	// --- Her journal (journal.cpp; kept beside each savegame)

	NeuroJournal _journal;
	Common::String journalFileName(int slot) const;
	void saveJournal(int slot);
	void loadJournal(int slot);
	void sendJournalNews();

	// --- The column beside the game in a window made to be streamed (panel.cpp)

	void updatePanel();
	void notePanelAction(const Common::String &name, const NeuroArgs &args);
	Common::String describeForPanel(const Common::String &name, const NeuroArgs &args);
	Common::String _panelNow, _panelKey, _panelTitle, _panelHide;
	Common::StringArray _panelRecent;
	uint32 _panelCheckedAt = 0;
	int _panelChat = 30;
	bool _panelHeld = false;

	// --- Actions that are never registered (debug.cpp)

	enum DebugActions {
		kDebugNone,
		kDebugOperator,
		kDebugAll
	};
	DebugActions _debugActions = kDebugNone;
	void handleDebugAction(const Common::String &id, const Common::String &name, const Common::String &data);
	/** The console's commands: save, load, status and settings. False if the name is not one of them. */
	bool handleOperatorAction(const Common::String &id, const Common::String &name, const Common::String &data);
	bool handleDeveloperAction(const Common::String &id, const Common::String &name, const Common::String &data);
	void sendStatus(const Common::String &id);
	void applySetting(const Common::String &setting, const Common::String &value);
	void debugDump();

	// --- The developer's dump of every room (extract.cpp)

	enum ExtractPhase {
		kExtractOff,
		kExtractDumpScripts,
		kExtractWaitForGame,
		kExtractEnterRoom,
		kExtractSettle,
		kExtractPan
	};
	struct Extraction {
		ExtractPhase phase = kExtractOff;
		Common::String dir;
		Common::Array<int> skip;
		int room = 0;
		int frames = 0;
		int width = 0, height = 0, panX = 0;
		Common::Array<byte> pixels;
	};
	Extraction _extract;
	void startExtraction();
	void dumpScripts(const Common::String &dir);
	void updateExtraction();
	bool extractNextRoom();
	void extractCopyScreen();
	void extractWriteRoom();
	bool writeRoomSnapshot(const Common::String &base, int room, int width, int height, int origin);
	bool captureRoom(const Common::String &label);
};

} // End of namespace Scumm

#endif
