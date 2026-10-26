// A UDP socket (IPv4, not blocking), as the relay client and the live feed use it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "fzgx/host.h"

namespace fzgx
{
struct Endpoint
{
  u32 address = 0;  // in network byte order
  u16 port = 0;
  bool operator==(const Endpoint&) const = default;
};

class Udp
{
public:
  Udp() = default;
  ~Udp();
  Udp(const Udp&) = delete;
  Udp& operator=(const Udp&) = delete;

  // A host name or dotted address; nothing when it does not resolve.
  static std::optional<Endpoint> Resolve(const std::string& host, u16 port);

  // Binds any free port; false when there is no socket to be had.
  bool Open();
  bool IsOpen() const { return m_open; }
  // The port it is bound to (tests).
  u16 LocalPort() const;
  void Send(const void* data, std::size_t size, const Endpoint& to);
  // One waiting datagram into `buffer` (at most `capacity` bytes); false when none waits.
  bool Receive(void* buffer, std::size_t capacity, std::size_t* received, Endpoint* from);

private:
  std::intptr_t m_socket = -1;
  bool m_open = false;
};
}  // namespace fzgx
