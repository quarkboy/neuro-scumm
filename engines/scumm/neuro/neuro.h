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
#include "common/rect.h"
#include "common/formats/json.h"
#include "common/str.h"

#include "scumm/neuro/data.h"
#include "scumm/neuro/websocket.h"

namespace Scumm {

class ScummEngine;

struct NeuroThing {
	int id = 0;
	int x = 0;
	int state = 0;
	Common::String name;

	/** Extra words for the room listing; not part of the name. */
	Common::String note;
};

typedef Common::Array<NeuroThing> NeuroThingList;

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

	/** While a simulated click is in progress, points the mouse at its target. */
	void overridePointer();

	/** What Neuro would be told on entering the current room. */
	Common::String describeWorld();

private:
	NeuroBridge(ScummEngine *vm, const Common::String &url);

	void onConnected();
	void registerActions();
	void handleMessage(const Common::String &text);
	void handleAction(const Common::String &id, const Common::String &name, const Common::String &data);

	void actionInteract(const Common::String &id, const Common::JSONValue *args);
	void actionSkipCutscene(const Common::String &id);
	void actionChooseDialogue(const Common::String &id, const Common::JSONValue *args);

	void sendForce(const Common::String &state, const Common::String &query, const char *actionName);
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
	Common::String describeRoom(const NeuroWorld &world) const;
	Common::String describeChanges(const NeuroWorld &before, const NeuroWorld &after) const;

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

	NeuroWorld _described;

	// The game's normal verbs, learned the first time they are on screen
	NeuroThingList _standardVerbs;

	// Only one force may be outstanding at a time
	bool _forceActive = false;
	uint32 _forceSentAt = 0;
	uint32 _lastActivity = 0;
	uint32 _idleForceMs = 0;
	Common::String _forcedDialogue;

	// What her last action was aimed at; only these are worth a note when their look changes
	int _lastTargets[2] = {0, 0};
	uint32 _controlSince = 0;
	uint32 _nextWorldPoll = 0;

	// Servers that predate the speech commands get dialogue as batched context instead
	bool _dialogueAsContext = false;
	Common::String _pendingDialogue;
	Common::String _lastLine;
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
