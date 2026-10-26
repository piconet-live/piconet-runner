// SPDX-License-Identifier: GPL-3.0-or-later

#include "udp.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fzgx
{
namespace
{
void Startup()
{
#ifdef _WIN32
  static const bool started = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  (void)started;
#endif
}
}  // namespace

Udp::~Udp()
{
  if (!m_open)
    return;
#ifdef _WIN32
  closesocket(static_cast<SOCKET>(m_socket));
#else
  close(static_cast<int>(m_socket));
#endif
}

std::optional<Endpoint> Udp::Resolve(const std::string& host, u16 port)
{
  Startup();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found)
    return std::nullopt;
  Endpoint endpoint;
  endpoint.address = reinterpret_cast<const sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
  endpoint.port = port;
  freeaddrinfo(found);
  return endpoint;
}

bool Udp::Open()
{
  Startup();
  const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (s == INVALID_SOCKET)
    return false;
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
#else
  if (s < 0)
    return false;
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = 0;
  if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
  {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return false;
  }
  m_socket = static_cast<std::intptr_t>(s);
  m_open = true;
  return true;
}

u16 Udp::LocalPort() const
{
  sockaddr_in local{};
#ifdef _WIN32
  int length = sizeof(local);
  if (!m_open || getsockname(static_cast<SOCKET>(m_socket), reinterpret_cast<sockaddr*>(&local), &length) != 0)
    return 0;
#else
  socklen_t length = sizeof(local);
  if (!m_open || getsockname(static_cast<int>(m_socket), reinterpret_cast<sockaddr*>(&local), &length) != 0)
    return 0;
#endif
  return ntohs(local.sin_port);
}

void Udp::Send(const void* data, std::size_t size, const Endpoint& to)
{
  if (!m_open)
    return;
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = to.address;
  remote.sin_port = htons(to.port);
#ifdef _WIN32
  sendto(static_cast<SOCKET>(m_socket), static_cast<const char*>(data), static_cast<int>(size), 0,
         reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
#else
  sendto(static_cast<int>(m_socket), data, size, 0, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
#endif
}

bool Udp::Receive(void* buffer, std::size_t capacity, std::size_t* received, Endpoint* from)
{
  if (!m_open)
    return false;
  sockaddr_in remote{};
#ifdef _WIN32
  int length = sizeof(remote);
  const int n = recvfrom(static_cast<SOCKET>(m_socket), static_cast<char*>(buffer), static_cast<int>(capacity), 0,
                         reinterpret_cast<sockaddr*>(&remote), &length);
#else
  socklen_t length = sizeof(remote);
  const auto n = recvfrom(static_cast<int>(m_socket), buffer, capacity, 0, reinterpret_cast<sockaddr*>(&remote),
                          &length);
#endif
  if (n < 0)
    return false;
  *received = static_cast<std::size_t>(n);
  from->address = remote.sin_addr.s_addr;
  from->port = ntohs(remote.sin_port);
  return true;
}
}  // namespace fzgx
