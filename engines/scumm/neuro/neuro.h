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

#include "common/formats/json.h"
#include "common/str.h"

#include "scumm/neuro/websocket.h"

namespace Scumm {

class ScummEngine;

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

private:
	NeuroBridge(ScummEngine *vm, const Common::String &url);

	void onConnected();
	void registerActions();
	void handleMessage(const Common::String &text);
	void handleAction(const Common::String &id, const Common::String &name, const Common::String &data);

	/** Takes ownership of the values in data. */
	void send(const char *command, const Common::JSONObject *data = nullptr);
	void sendActionResult(const Common::String &id, bool success, const Common::String &message = Common::String());

	ScummEngine *_vm;
	Common::String _url;
	Common::String _gameName;
	WebSocketClient _ws;
	bool _wasConnected = false;
	uint32 _nextConnectAttempt = 0;
};

} // End of namespace Scumm

#endif
