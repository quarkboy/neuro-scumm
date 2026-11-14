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

#ifndef SCUMM_NEURO_WEBSOCKET_H
#define SCUMM_NEURO_WEBSOCKET_H

#include "common/array.h"
#include "common/str.h"

namespace Networking {
class Socket;
}

namespace Scumm {

/**
 * Minimal RFC 6455 client: plain ws:// only, text messages only.
 * All calls are non-blocking apart from connect().
 */
class WebSocketClient {
public:
	WebSocketClient() {}
	~WebSocketClient();

	bool connect(const Common::String &url);
	void close();
	bool isConnected() const { return _socket != nullptr; }

	bool sendText(const Common::String &text);

	/** Fetches the next complete text message, if one has arrived. */
	bool receive(Common::String &message);

private:
	enum Opcode {
		kOpContinuation = 0x0,
		kOpText = 0x1,
		kOpBinary = 0x2,
		kOpClose = 0x8,
		kOpPing = 0x9,
		kOpPong = 0xA
	};

	bool handshake(const Common::String &host, const Common::String &path);
	bool sendFrame(byte opcode, const byte *payload, uint32 len);
	void readAvailable();
	bool parseFrame(Common::String &message, bool &gotMessage);
	void consume(uint32 count);

	Networking::Socket *_socket = nullptr;
	Common::Array<byte> _inBuf;
	Common::String _fragment;
};

} // End of namespace Scumm

#endif
