// The relay fuzzer of fuzz_core (fuzz_replays.cpp says what both do and holds the program's main).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz.h"

namespace fzgx::net
{
u64 RelayKey(u64 token);  // relay_wire.cpp
}

namespace
{
// The relay's side of the protocol (net_internal.h's Type).
enum : u8 { PONG = 2, JOINED = 4, GO = 6, OFFICIAL = 8, DESYNC = 10, SEAT_LEFT = 11, SEAT_BACK = 12 };

constexpr u64 TOKEN = 0x1f2e3d4c5b6a7988;  // the race's ("debug.relay" below)

// A packet of the relay's: the header and, for this protocol version, the seat's key (relay_wire.cpp RelayKey; the
// client audit's M1). It must write the current protocol version: the game ignores packets of version 2 since
// version 3 (the cookie), and a fuzzer writing them reaches only the foreign-version branch.
std::string Packet(u8 type, u8 version = 3, u64 token = TOKEN)
{
  std::string p{'F', 'Z', static_cast<char>(version), static_cast<char>(type)};
  if (version == 3)
  {
    const u64 key = net::RelayKey(token);
    for (int i = 7; i >= 0; --i)
      p += static_cast<char>(key >> (8 * i));
  }
  return p;
}

// One race's relay (FuzzRelay): what it says of the race, what it has closed, and what it sends: a relay's packets,
// and what no relay should send. The order of the random numbers drawn is the race: a seed gives the same one again.
struct HostileRelay
{
  HostileRelay(Rng& random, Udp& socket, u32 seats, u8 seat, std::size_t row_bytes, unsigned long long& sent,
               unsigned long long& sent_hostile)
      : rng(random), relay(socket), mask(seats), my_seat(seat), row(row_bytes), packets(sent), hostile(sent_hostile)
  {
  }
  Rng& rng;
  Udp& relay;
  u32 mask;         // the race's seats as this relay says them
  u8 my_seat;
  std::size_t row;  // the bytes of a row of those seats
  unsigned long long& packets;
  unsigned long long& hostile;
  std::optional<Endpoint> client;
  u32 official = 0;  // frames this relay has closed
  bool went = false;
  unsigned long long wrong_key = 0;  // batches of packets without this seat's key

  void Send(const std::string& packet)
  {
    if (client)
      relay.Send(packet.data(), packet.size(), *client);
    ++packets;
  }

  std::string Officials(u32 first, u32 count, std::size_t row_size)
  {
    std::string p = Packet(OFFICIAL);
    Put32(p, first);
    p += static_cast<char>(count);
    Put32(p, mask);
    for (std::size_t i = 0; i < count * row_size; ++i)
      p += static_cast<char>(rng() % 4 == 0 ? rng() : 0);
    return p;
  }

  void Hear()
  {
    // what the game sent: its address, and nothing else is looked at
    u8 buffer[2048];
    std::size_t got = 0;
    Endpoint sender;
    while (relay.Receive(buffer, sizeof(buffer), &got, &sender))
      client = sender;
  }

  // What a relay sends: joined, go, the frames the game needs next.
  void SayTheRace(u32 frames, u32 step)
  {
    std::string joined = Packet(JOINED);
    Put32(joined, 7);
    joined += static_cast<char>(my_seat);
    Put32(joined, mask);
    joined += std::string(8, '\x5A');  // the cookie, echoed
    joined += '\0';                    // no flags
    Put32(joined, official);
    Send(joined);
    if (frames > 5 && (!went || rng() % 20 == 0))
    {
      std::string go = Packet(GO);
      Put32(go, 7);
      Put32(go, rng() % 4 == 0 ? rng() : 30);
      Send(go);
      went = true;
    }
    // the race goes on: the frames the game needs next, resent (a relay resends from the game's own count)
    official = std::max(official, step + 4 + rng() % 30);
    if (rng() % 50 != 0)
    {
      for (u32 f = step > 2 ? step - 2 : 0; f < official; f += 6)
        Send(Officials(f, std::min<u32>(6, official - f), row));
    }
  }

  void SayWhatNoRelayShould(u32 step)
  {
    // and with them, what no relay should send
    for (u32 k = rng() % 6; k > 0; --k)
    {
      ++hostile;
      switch (rng() % 14)
      {
      case 0: Send(Officials(rng(), rng() % 256, row)); break;                              // any frame, any count
      case 1: Send(Officials(official + rng() % 100000, 1 + rng() % 20, row)); break;       // far in the future
      case 2: Send(Officials(step > 500 ? step - 500 : 0, rng() % 256, row)); break;        // the past, again
      case 3: Send(Officials(step, 1 + rng() % 20, rng() % 400)); break;                    // rows of another size
      case 4: Send(Officials(0xFFFFFFF0u + rng() % 16, 255, row)); break;                   // frames that wrap
      case 5:
      {
        std::string pong = Packet(PONG);
        Put32(pong, rng());
        Put32(pong, rng() % 2 ? rng() : 0);
        Put32(pong, rng());
        if (rng() % 2)
          Put32(pong, rng() % 3 ? rng() : official * 16);
        Send(pong);
        break;
      }
      case 6:
      {
        // another "joined": any seat, with this race's player seats or with seats no race has (never another
        // race's: taken, the official frames of this one would not fit it and the game would only wait)
        std::string again = Packet(JOINED);
        Put32(again, rng());
        again += static_cast<char>(rng());
        Put32(again, rng() % 2 ? mask : rng() | (1u << (30 + rng() % 2)));
        for (int i = 0; i < 8; ++i)
          again += static_cast<char>(rng());
        again += static_cast<char>(rng() % 3 == 0 ? rng() & ~2u : 0);  // flags: returning, a spectator's (never "over")
        Put32(again, rng() % 2 ? official : rng());
        if (rng() % 8 == 0)
          again.resize(rng() % (again.size() + 4));  // another length
        Send(again);
        break;
      }
      case 7: Send(Packet(SEAT_LEFT) + static_cast<char>(rng())); break;
      case 11:
      {
        std::string back = Packet(SEAT_BACK);  // this seat or any, from any step
        back += static_cast<char>(rng() % 2 ? my_seat : rng());
        Put32(back, rng() % 2 ? step + rng() % 300 : rng());
        Send(back);
        break;
      }
      case 12:
      {
        // without this seat's key: any packet of the relay's, with another seat's key or none, from the relay's own
        // address (the client audit's M1): a desync among them, which taken would stop the race
        const u8 types[] = {PONG, JOINED, GO, OFFICIAL, DESYNC, SEAT_LEFT, SEAT_BACK};
        std::string other = Packet(types[rng() % 7], 3, rng() % 2 ? TOKEN + 1 + rng() % 1000 : rng());
        if (rng() % 4 == 0)
          other.resize(4);  // no key at all
        other += std::string(rng() % 32, static_cast<char>(rng()));
        Send(other);
        std::string desync = Packet(DESYNC, 3, TOKEN ^ (1ull << (rng() % 64)));
        Put32(desync, rng());
        Send(desync);
        ++wrong_key;
        break;
      }
      case 8: Send(Packet(static_cast<u8>(rng()), static_cast<u8>(rng() % 5)) + std::string(rng() % 64, static_cast<char>(rng()))); break;
      case 9: Send(std::string(rng() % 1400, static_cast<char>(rng()))); break;
      case 10:
      {
        std::string cut = Officials(step, 1 + rng() % 6, row);
        cut.resize(rng() % (cut.size() + 1));
        Send(cut);
        break;
      }
      default:
        if (rng() % 400 == 0)
        {
          std::string desync = Packet(DESYNC);
          Put32(desync, rng());
          Send(desync);
        }
      }
    }
  }
};

// One race against that relay, to its end. False: no socket to be the relay.
bool OneRace(Rng& rng, FuzzHost& host, const std::string& dir, Totals& n, unsigned long long& packets,
             unsigned long long& hostile, unsigned long long& gave_up)
{
  Udp relay;
  if (!relay.Open())
    return false;
  Options options;
  options.bundle_dir = dir;
  options.debug.relay = "127.0.0.1:" + std::to_string(relay.LocalPort()) + ":1f2e3d4c5b6a7988";
  if (rng() % 4 == 0)
    options.replay_file = dir + "/current-recording.fzrp";
  Init(host, options);
  // the race's seats as this relay says them: mostly a race's, sometimes not (seats 30 and 31, a seat that is not
  // a player's, a seat number no race has)
  u32 mask = rng() % 5 == 0 ? 0x3FFFFFFF : (1u << (1 + rng() % 6)) - 1;
  if (rng() % 6 == 0)
    mask |= 1u << (26 + rng() % 6);
  u8 my_seat = static_cast<u8>(std::countr_zero(mask));
  if (rng() % 6 == 0)
    my_seat = static_cast<u8>(rng());
  const std::size_t row = static_cast<std::size_t>(std::popcount(mask)) * 6;
  const u32 from = 20 + rng() % 40, to = from + (rng() % 10 == 0 ? rng() % 300 : rng() % 40);
  Game game(host, rng, from, to);
  HostileRelay r(rng, relay, mask, my_seat, row, packets, hostile);
  u32 frames = 0;
  const u32 limit = to + 60 + rng() % 100;
  for (; frames < limit; ++frames)
  {
    r.Hear();
    if (r.client)
    {
      const u32 step = game.g.Read32(net::STEP);
      r.SayTheRace(frames, step);
      r.SayWhatNoRelayShould(step);
    }
    if (!game.Frame())
      break;
  }
  n.frames += frames;
  n.stopped += host.stops != 0 ? 1 : 0;
  gave_up += game.g.Read32(layout::CONTROL + 132) != 0 ? 1 : 0;
  Shutdown();
  return true;
}
}  // namespace

void FuzzRelay(double seconds, u32 seed, const std::string& dir)
{
  Rng rng(seed);
  MakeBundle(dir);
  FuzzHost host;
  Totals n;
  unsigned long long packets = 0, hostile = 0, gave_up = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (std::chrono::steady_clock::now() < end)
  {
    std::printf("relay race %llu (seed %u)\n", n.inputs, seed);
    std::fflush(stdout);
    alarm(120);
    ++n.inputs;
    host.stops = 0;
    if (!OneRace(rng, host, dir, n, packets, hostile, gave_up))
      break;
    alarm(0);
  }
  Init(host, Options{});
  std::printf("fuzz relay: %llu races in %.0f s (seed %u): %llu displayed frames, %llu packets (%llu of them hostile), "
              "%llu races stopped by a desync packet, %llu given up for want of official frames; no fault\n",
              n.inputs, seconds, seed, n.frames, packets, hostile, n.stopped, gave_up);
}
