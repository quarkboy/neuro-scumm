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
#include "common/formats/json.h"
#include "common/str.h"

#include "scumm/neuro/websocket.h"

namespace Scumm {

class ScummEngine;

struct NeuroThing {
	int id = 0;
	Common::String name;
};

typedef Common::Array<NeuroThing> NeuroThingList;

/** Everything the player could see or use at one moment. */
struct NeuroWorld {
	int room = 0;
	NeuroThingList objects;
	NeuroThingList actors;
	NeuroThingList inventory;
	NeuroThingList verbs;
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

	/** What Neuro would be told on entering the current room. */
	Common::String describeWorld();

private:
	NeuroBridge(ScummEngine *vm, const Common::String &url);

	void onConnected();
	void registerActions();
	void handleMessage(const Common::String &text);
	void handleAction(const Common::String &id, const Common::String &name, const Common::String &data);

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
	bool _wasConnected = false;
	uint32 _nextConnectAttempt = 0;

	NeuroWorld _described;
	uint32 _controlSince = 0;
	uint32 _nextWorldPoll = 0;

	Common::String _speechIdPrefix;
	uint32 _speechCounter = 0;
	bool _speechOpen = false;
};

} // End of namespace Scumm

#endif
