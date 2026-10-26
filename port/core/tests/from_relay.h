// The core's tests' relays: the start of a packet the relay sends a game, with the seat's key (protocol/relay.toml;
// the client audit's M1).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "fzgx/host.h"

namespace fzgx::net
{
u64 RelayKey(u64 token);  // relay_wire.cpp: the seat's key, which opens every packet the relay sends a game
}

// The token every test's relay is joined with ("debug.relay" ...:1f2e3d4c5b6a7988).
constexpr fzgx::u64 TEST_TOKEN = 0x1f2e3d4c5b6a7988;

// The start of a packet the relay sends a game: the mark, the version, the type, and the seat's key (protocol/
// relay.toml; the client audit's M1).
inline std::string FromRelay(char type, fzgx::u64 token = TEST_TOKEN)
{
  std::string p = std::string("FZ\x03", 3) + type;
  const fzgx::u64 key = fzgx::net::RelayKey(token);
  for (int i = 7; i >= 0; --i)
    p += static_cast<char>(key >> (8 * i));
  return p;
}
