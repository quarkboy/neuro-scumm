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

#include "scumm/neuro/websocket.h"

#include "backends/networking/basic/socket.h"

#include "common/base64.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/random.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace Scumm {

static const uint32 kHandshakeTimeoutMs = 3000;
static const uint32 kMaxMessageSize = 16 * 1024 * 1024;

WebSocketClient::~WebSocketClient() {
	close();
}

void WebSocketClient::close() {
	delete _socket;
	_socket = nullptr;
	_inBuf.clear();
	_fragment.clear();
}

bool WebSocketClient::connect(const Common::String &url) {
	close();

	if (!url.hasPrefixIgnoreCase("ws://")) {
		warning("Neuro: Only ws:// URLs are supported, got '%s'", url.c_str());
		return false;
	}

	Common::String host = url.substr(5);
	Common::String path = "/";
	size_t slash = host.findFirstOf('/');
	if (slash != Common::String::npos) {
		path = host.substr(slash);
		host = host.substr(0, slash);
	}
	if (host.empty())
		return false;

	// The socket layer is libcurl in connect-only mode, which wants a scheme it knows.
	_socket = Networking::Socket::connect("http://" + host);
	if (!_socket)
		return false;

	if (!handshake(host, path)) {
		close();
		return false;
	}
	return true;
}

bool WebSocketClient::handshake(const Common::String &host, const Common::String &path) {
	Common::RandomSource rnd("neuro_ws");
	byte key[16];
	for (uint i = 0; i < sizeof(key); i++)
		key[i] = rnd.getRandomNumber(255);

	Common::String request = Common::String::format(
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		path.c_str(), host.c_str(), Common::b64EncodeData(key, sizeof(key)).c_str());

	if (_socket->send(request.c_str(), request.size()) != request.size())
		return false;

	Common::String response;
	const uint32 deadline = g_system->getMillis() + kHandshakeTimeoutMs;
	while (g_system->getMillis() < deadline) {
		int ready = _socket->ready();
		if (ready < 0)
			return false;
		if (ready == 0) {
			g_system->delayMillis(5);
			continue;
		}

		// Read one byte at a time so that no frame data is swallowed with the headers
		char c;
		if (_socket->recv(&c, 1) != 1)
			return false;
		response += c;
		if (response.hasSuffix("\r\n\r\n")) {
			if (!response.hasPrefix("HTTP/1.1 101")) {
				warning("Neuro: WebSocket upgrade refused: %s", response.substr(0, response.findFirstOf('\r')).c_str());
				return false;
			}
			return true;
		}
	}

	warning("Neuro: WebSocket handshake timed out");
	return false;
}

bool WebSocketClient::sendFrame(byte opcode, const byte *payload, uint32 len) {
	if (!_socket)
		return false;

	Common::Array<byte> frame;
	frame.reserve(len + 14);
	frame.push_back(0x80 | opcode);
	if (len < 126) {
		frame.push_back(0x80 | len);
	} else if (len < 65536) {
		frame.push_back(0x80 | 126);
		frame.push_back(len >> 8);
		frame.push_back(len & 0xFF);
	} else {
		frame.push_back(0x80 | 127);
		for (int i = 0; i < 4; i++)
			frame.push_back(0);
		for (int shift = 24; shift >= 0; shift -= 8)
			frame.push_back((len >> shift) & 0xFF);
	}

	// Clients must mask every frame
	Common::RandomSource rnd("neuro_ws");
	byte mask[4];
	for (int i = 0; i < 4; i++) {
		mask[i] = rnd.getRandomNumber(255);
		frame.push_back(mask[i]);
	}
	for (uint32 i = 0; i < len; i++)
		frame.push_back(payload[i] ^ mask[i & 3]);

	if (_socket->send((const char *)frame.data(), frame.size()) != frame.size()) {
		close();
		return false;
	}
	return true;
}

bool WebSocketClient::sendText(const Common::String &text) {
	return sendFrame(kOpText, (const byte *)text.c_str(), text.size());
}

void WebSocketClient::readAvailable() {
	byte buf[4096];
	while (_socket) {
		int ready = _socket->ready();
		if (ready == 0)
			return;

		size_t count = (ready > 0) ? _socket->recv(buf, sizeof(buf)) : 0;
		if (count == 0) {
			// Readable with nothing to read means the peer closed the connection
			debug(1, "Neuro: WebSocket connection lost");
			delete _socket;
			_socket = nullptr;
			return;
		}

		uint oldSize = _inBuf.size();
		_inBuf.resize(oldSize + count);
		memcpy(_inBuf.data() + oldSize, buf, count);
	}
}

void WebSocketClient::consume(uint32 count) {
	uint remaining = _inBuf.size() - count;
	if (remaining)
		memmove(_inBuf.data(), _inBuf.data() + count, remaining);
	_inBuf.resize(remaining);
}

bool WebSocketClient::parseFrame(Common::String &message, bool &gotMessage) {
	gotMessage = false;
	if (_inBuf.size() < 2)
		return false;

	const byte *data = _inBuf.data();
	const bool fin = (data[0] & 0x80) != 0;
	const byte opcode = data[0] & 0x0F;
	const bool masked = (data[1] & 0x80) != 0;
	uint64 len = data[1] & 0x7F;
	uint32 offset = 2;

	if (len == 126) {
		if (_inBuf.size() < 4)
			return false;
		len = READ_BE_UINT16(data + 2);
		offset = 4;
	} else if (len == 127) {
		if (_inBuf.size() < 10)
			return false;
		len = READ_BE_UINT64(data + 2);
		offset = 10;
	}

	if (len > kMaxMessageSize) {
		warning("Neuro: Oversized WebSocket frame, dropping connection");
		close();
		return false;
	}

	byte mask[4] = {0, 0, 0, 0};
	if (masked) {
		if (_inBuf.size() < offset + 4)
			return false;
		memcpy(mask, data + offset, 4);
		offset += 4;
	}

	if (_inBuf.size() < offset + len)
		return false;

	Common::String payload((const char *)data + offset, (uint32)len);
	if (masked) {
		for (uint32 i = 0; i < payload.size(); i++)
			payload.setChar(payload[i] ^ mask[i & 3], i);
	}
	consume(offset + (uint32)len);

	switch (opcode) {
	case kOpText:
	case kOpBinary:
		_fragment = payload;
		break;
	case kOpContinuation:
		_fragment += payload;
		break;
	case kOpPing:
		sendFrame(kOpPong, (const byte *)payload.c_str(), payload.size());
		return true;
	case kOpClose:
		sendFrame(kOpClose, (const byte *)payload.c_str(), payload.size());
		close();
		return false;
	default:
		return true;
	}

	if (fin) {
		message = _fragment;
		_fragment.clear();
		gotMessage = true;
	}
	return true;
}

bool WebSocketClient::receive(Common::String &message) {
	readAvailable();

	bool gotMessage = false;
	while (parseFrame(message, gotMessage)) {
		if (gotMessage)
			return true;
	}
	return false;
}

} // End of namespace Scumm
