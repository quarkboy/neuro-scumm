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

class ScummEngine;

/** How long a yes/no question about a passing moment can still be answered. */
static const uint32 kQuestionOpenMs = 20000;

struct NeuroThing {
	int id = 0;
	int x = 0;
	int state = 0;
	Common::String name;

	/** Extra words for the room listing; not part of the name. */
	Common::String note;
};

typedef Common::Array<NeuroThing> NeuroThingList;

/** A simulated mouse click, in room or screen coordinates. */
struct NeuroClick {
	int x = 0, y = 0;
	bool screen = false;
};

/** A validated verb sentence or walk, ready to run. */
struct NeuroCommand {
	bool walk = false;
	int verb = 0;
	int objects[2] = {0, 0};

	// Where to click, for walking
	int x = 0, y = 0;
	bool isSpot = false;

	/** As a follow-up: how long Guybrush must have stood still before it starts. */
	uint32 settleMs = 0;

	/** What she is told on success. */
	Common::String note;

	/** Not yet resolved: "VERB | TARGET | SECOND", worked out when it is due to run. */
	Common::String preset;

	/** When set, the command is carried out as these clicks, as a player would. */
	Common::Array<NeuroClick> clicks;
};

/** Everything the player could see or use at one moment. */
struct NeuroWorld {
	int room = 0;
	NeuroThingList objects;
	NeuroThingList actors;
	NeuroThingList inventory;
	NeuroThingList verbs;

	/** Places without an object that can still be walked to; id is -(index + 1). */
	NeuroThingList spots;
	Common::Array<Common::Point> spotPositions;

	/** Some things share a name and were numbered to tell them apart. */
	bool numbered = false;

	/** The verb area shows lines to say instead of verbs. */
	bool inDialogue = false;
};

/**
 * Lets Neuro-sama play the game through the Neuro API
 * (https://github.com/VedalAI/neuro-sdk).
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

	/** A script gave an actor a name. */
	void onActorNamed(int actor);

	/** While a simulated click is in progress, points the mouse at its target. */
	void overridePointer();

	/** What Neuro would be told on entering the current room. */
	Common::String describeWorld();

private:
	NeuroBridge(ScummEngine *vm, const Common::String &url);

	void onConnected();
	void registerActions(bool interactOnly = false);
	uint32 _registeredAt = 0;
	void handleMessage(const Common::String &text);
	void handleAction(const Common::String &id, const Common::String &name, const Common::String &data);
	void handleDebugAction(const Common::String &id, const Common::String &name, const Common::String &data);

	void actionInteract(const Common::String &id, const NeuroArgs &args);
	void actionSkipCutscene(const Common::String &id);

	bool resolveCommand(const NeuroWorld &world, const Common::String &verbName, const Common::String &targetName,
		const Common::String &secondName, NeuroCommand &command, Common::String &error);
	void runCommand(const NeuroCommand &command);
	bool resolvePreset(const NeuroWorld &world, const Common::String &preset, NeuroCommand &command);
	void updateQueuedCommand();

	// Development tool that dumps every room; see extract.cpp
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
	bool startExtraction();
	void dumpScripts(const Common::String &dir);
	void updateExtraction();
	bool extractNextRoom();
	void extractCopyScreen();
	void extractWriteRoom();

	// After each of her actions she is told how it turned out, so that
	// she can carry on at once instead of waiting to be prompted.
	bool _awaitingOutcome = false;
	bool _outcomeTold = false;   ///< she has been prompted with the result
	bool _outcomeSpoken = false; ///< someone said something in reply
	void finishAction(const NeuroWorld &world);

	// Follow-up steps, each run as soon as the one before has played out
	Common::Array<NeuroCommand> _queue;
	bool _queueSawBusy = false;
	uint32 _queueIdleSince = 0;
	uint32 _commandStartedAt = 0;
	void actionChooseDialogue(const Common::String &id, const NeuroArgs &args);

	void sendForce(const Common::String &state, const Common::String &query, const char *actionName, const Common::String &priority = Common::String());
	void checkAlerts(const NeuroWorld &before, const NeuroWorld &after);
	void fireAlert(const NeuroData::Alert &alert, const NeuroWorld &world);

	// How long she takes from prompt to action; alerts are sent that much early
	uint32 _reactionMs = 300;
	// A yes/no question from an alert, and what saying yes does
	Common::String _questionCommand;
	Common::String _questionThen;
	uint32 _questionAskedAt = 0;
	// A game timer that stands still while the question is open
	int _questionHoldVar = 0;
	int _questionHoldValue = 0;
	void actionAnswer(const Common::String &id, const NeuroArgs &args);

	int _pendingAlert = -1;
	uint32 _pendingAlertDue = 0;
	void updateForces(const NeuroWorld &world);

	/** Finds a point in room coordinates where a click lands on the given object or actor. */
	bool findClickPoint(int thing, int &x, int &y) const;
	void clickInRoom(int x, int y);
	void updateClick();

	/** Finds a thing by the name Neuro used for it; returns nullptr and fills error if there is no single match. */
	const NeuroThing *findThing(const NeuroThingList &things, const Common::String &name) const;

	bool playerHasControl() const;
	void pollWorld();
	void collectWorld(NeuroWorld &world);
	enum RoomDetail {
		kRoomFirstVisit, ///< with the written description
		kRoomRevisit,    ///< just what is there
		kRoomStatus      ///< what is there and what she carries, for a reminder
	};
	Common::String describeRoom(const NeuroWorld &world, RoomDetail detail) const;

	// Things said once are forgotten as her context scrolls on, so they are
	// said again after roughly this many tokens of traffic.
	uint32 _contextTokens = 32000;
	uint64 _tokensSent = 0;
	uint64 _introSentAt = 0;
	/** Per room: the traffic count when its full description was last sent. */
	Common::HashMap<int, uint64> _seenRooms;
	void sendIntro();
	Common::String describeChanges(const NeuroWorld &before, const NeuroWorld &after);

	// Where each character in the room was last seen, and from that the thing
	// each one who left was standing at (by lower-case name)
	int _actorSeenRoom = 0;
	struct SeenActor {
		Common::Point pos;
		Common::String name;
	};
	Common::HashMap<int, SeenActor> _actorSeen;
	/** Named characters seen in this room who are no longer in it; each is reported once. */
	NeuroThingList departed(const NeuroWorld &world);
	Common::HashMap<Common::String, Common::String> _leftAt;
	Common::String lastSeenAt(int actor, const NeuroWorld &world) const;

	/** Converts game text to UTF-8, dropping control codes and name padding. */
	Common::String cleanText(const byte *text, bool resolveCodes);

	/** Takes ownership of the values in data. */
	void send(const char *command, const Common::JSONObject *data = nullptr);
	void sendActionResult(const Common::String &id, bool success, const Common::String &message = Common::String());

	ScummEngine *_vm;
	Common::String _url;
	Common::String _gameName;
	WebSocketClient _ws;
	NeuroData _data;
	NeuroJournal _journal;
	Common::String journalFileName(int slot) const;
	void sendJournalNews();
	void saveJournal(int slot);
	void loadJournal(int slot);
	int _saveLoadSlot = -1;
	bool _savePending = false;

	// The costume each actor wore when it was named. Scripts reuse actors
	// for other characters without renaming them, which leaves a wrong name.
	Common::HashMap<int, int> _namedCostume;

	/** forList: for the list of characters in the room, as opposed to naming a speaker. */
	Common::String actorName(int actor, bool forList = false);
	void debugDump();
	bool _wasConnected = false;
	uint32 _nextConnectAttempt = 0;

	// A simulated click takes a few frames: the game's own hover script
	// has to see the pointer over the target before the button goes down.
	enum ClickPhase {
		kClickIdle,
		kClickHover,
		kClickPress,
		kClickRelease
	};
	ClickPhase _clickPhase = kClickIdle;
	int _clickFrames = 0;
	int _clickX = 0, _clickY = 0;
	bool _clickScreen = false;
	uint32 _lastClickAt = 0;
	// Clicks still to make for the command being carried out, one after another
	Common::Array<NeuroClick> _clickQueue;
	void clickAt(const NeuroClick &click);

	// Some rooms swap in their own mouse handler for a scene, and only that
	// one knows what clicking there should do, so her commands then have to
	// be real clicks.
	bool needsClicks() const;
	bool planClicks(const NeuroWorld &world, NeuroCommand &command);
	bool verbButton(int verbId, NeuroClick &click) const;

	NeuroWorld _described;

	// Testing aid: run the game at ScummVM's fast speed, always or only while she cannot act
	enum FastMode {
		kFastOff,
		kFastAlways,
		kFastScenes
	};
	FastMode _fastMode = kFastOff;
	void updateSpeed();

	// A loaded savegame can change everything at once
	bool _loadPending = false;
	void resetAfterLoad();

	// The game's normal verbs, learned the first time they are on screen
	NeuroThingList _standardVerbs;

	// Only one force may be outstanding at a time
	bool _forceActive = false;
	Common::String _forceAction;
	Common::String _currentAction;
	uint32 _forceSentAt = 0;
	uint32 _lastActivity = 0;
	uint32 _idleForceMs = 0;
	Common::String _forcedDialogue;

	// What her last action was aimed at; only these are worth a note when their look changes
	int _lastTargets[2] = {0, 0};
	uint32 _controlSince = 0;
	uint32 _speechBlockSince = 0;
	uint32 _nextWorldPoll = 0;

	// Servers that predate the speech commands get dialogue as batched context instead
	bool _dialogueAsContext = false;
	Common::String _pendingDialogue;
	Common::String _lastLine;

	// Background chatter (barking dogs, a muttering crowd) repeats forever
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
	uint32 _lastActionAt = 0;
	uint32 _pendingSince = 0;
	Common::String _lastLineText;
	uint _lastLineOffset = 0;
	uint32 _lastTalkTime = 0;
	void flushDialogue();

	Common::String _speechIdPrefix;
	uint32 _speechCounter = 0;
	bool _speechOpen = false;
};

} // End of namespace Scumm

#endif
