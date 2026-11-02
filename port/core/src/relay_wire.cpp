// The relay's packets: the one place a datagram of the relay is received and its bytes are read (Drain, Handle),
// and what this game sends it (Send, SendInputs, Ping).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "board_net.h"

#include "host_timers.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace fzgx::net
{
using namespace std::chrono_literals;

namespace
{
u64 Get64(const u8* p)
{
  u64 v = 0;
  for (int i = 0; i < 8; ++i)
    v = v << 8 | p[i];
  return v;
}

int s_exit_code = 0;  // what the process ends with (ExitCode): a game started again into a race that was over, or
                      // one closed while its seat raced (Shutdown, session.cpp)

// "joined" for a game started again into its race, from a relay that says the race is over (or the seat left it for
// good): nothing to rejoin. The game stops at once, under LOADING, and its exit code says why (the launcher takes the
// player back to the lobby). Left unanswered, such a Join would hold the game under LOADING until closed (run
// rj-p3c).
void TakeTheRaceOver(Session& s, const relay::Joined& j)
{
  if (s.exit_sent)
    return;
  s.exit_sent = true;
  s.phase = Phase::Failed;
  s_exit_code = EXIT_RACE_OVER;
  Notice("fzgx: the relay says the race is over (frame {}): nothing to rejoin; stopping", j.closed);
  TheHost().RequestStop();
}

// "joined" while the race runs: the answer to a Join sent again (the network dropped, or the address changed). The
// cookie is the new address's; a seat the relay had handed to the AI meanwhile catches up from this game's own step,
// and is the player's again from the step a "seat back" names.
void TakeJoinedAgain(Session& s, const relay::Joined& j)
{
  s.cookie = j.cookie;
  s.target = std::max(s.target, j.closed);
  if (s.watcher && (j.flags & relay::JOINED_OVER) != 0 && !s.exit_sent)
  {
    // a spectator whose race the relay has dropped (every racer left it): nothing more will come; it stops, as a
    // player leaving the results does
    s.exit_sent = true;
    Notice("fzgx: the relay has dropped the race this spectator watched (frame {}); stopping", j.closed);
    TheHost().RequestStop();
  }
  if (s.watcher)
    return;  // (a spectator has no seat to come back to: the frames come again from its acknowledgement)
  if ((j.flags & relay::JOINED_RETURNING) != 0 && !s.returning)
  {
    s.returning = true;
    s.own_from = NEVER;
    s.hand_back = 0;
    Notice("fzgx: back in the race at the relay's frame {} (this game at {}); the AI drives this seat until it is "
           "given back", j.closed, s.official.size());
  }
  else if ((j.flags & relay::JOINED_RETURNING) == 0)
    Notice("fzgx: the relay answered again; this seat still races");
}

// The packets by their types (Body), each decoded by the generated header (relay_packets.g.h, which checks its
// length); the session's own checks are here.

// "joined": this game's seat, and the race's player seats.
void TakeJoined(Session& s, const relay::Joined& j)
{
  if (s.phase != Phase::Joining && s.my_seat == j.seat)
    return TakeJoinedAgain(s, j);
  if (s.phase == Phase::Joining && (j.flags & relay::JOINED_OVER) != 0)
    return TakeTheRaceOver(s, j);
  if (s.phase == Phase::Joining)
  {
    const u32 mask = j.player_seats;
    // Seats of a race only (30): each seat's inputs are written by its number, and the local seat's is where the
    // game looks for its own machine. A relay that says otherwise (a LAN server is anyone's) is not joined.
    //
    // And the seat is one of them: a seat outside the mask has no row of its own, and the game then waited for
    // frames it could never run (the relay fuzzer)
    if (j.seat >= 30 || mask == 0 || (mask >> 30) != 0 || (mask >> j.seat & 1) == 0)
    {
      if (!s.refused_logged)
        Error("fzgx: the relay names a seat no race has (seat {}, player seats {:08x}); not joined", j.seat, mask);
      s.refused_logged = true;
      return;
    }
    s.my_seat = j.seat;
    s.player_seats = mask;
    s.seats.clear();
    for (int i = 0; i < 32; ++i)
    {
      if (mask >> i & 1)
      {
        if (i == s.my_seat)
          s.my_index = static_cast<int>(s.seats.size());
        s.seats.push_back(i);
      }
    }
    s.phase = Phase::Joined;
    GiveTheReplayItsPlace(s);
    s.cookie = j.cookie;
    s.target = j.closed;
    if ((j.flags & relay::JOINED_RETURNING) != 0)
    {
      // the race runs and this seat was away: from step 0, on the official frames, the AI driving it
      s.returning = true;
      s.own_from = NEVER;
    }
    if ((j.flags & relay::JOINED_SPECTATOR) != 0)
    {
      // a spectator (stage A2): no seat of its own, so no input is ever
      // sent (acknowledgements only: SendInputs); the seat named is the machine the game is set up behind, watched
      // first. It watches behind live, on official frames only (spectate.cpp)
      s.watcher = true;
      s.own_from = NEVER;
    }
    Notice("fzgx: joined the race as {} {} of {} player seats{}", s.watcher ? "a spectator, watching seat" : "seat",
           s.my_seat, s.seats.size(),
           s.returning ? std::format("; the race runs (frame {}): catching up", s.target) : std::string());
  }
}

// "pong": this game's own ping back, for the round trip; with the relay's race clock, for the time sync.
void TakePong(Session& s, const relay::Pong& p)
{
  const u64 sent = static_cast<u64>(p.client_ticks);
  const auto now = Clock::now();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  const double rtt = (static_cast<double>(now_ns) - static_cast<double>(sent)) / 1e6;
  // The time is this game's own, sent back. One that is no round trip a race can have (from before the ping, or
  // a minute old) is no answer to a ping of this game: taken, it went into the race clock and, as a number no
  // counter holds, into the mod's round-trip word.
  if (!(rtt >= 0 && rtt <= 60000))
    return;
  s.rtt_samples.push_back(rtt);
  s.summary.Ping(rtt);
  if (s.rtt_samples.size() > 9)
    s.rtt_samples.pop_front();
  std::vector<double> sorted(s.rtt_samples.begin(), s.rtt_samples.end());
  std::sort(sorted.begin(), sorted.end());
  s.rtt_ms = sorted[sorted.size() / 2];
  if (p.relay_ms && s.go)
  {
    // the relay read its clock about half this round trip ago
    const double since_go = std::chrono::duration<double, std::milli>(now - s.go_local).count();
    s.relay_offsets.push_back(*p.relay_ms + rtt / 2 - since_go);
    if (s.relay_offsets.size() > 5)
      s.relay_offsets.pop_front();
    std::vector<double> offsets(s.relay_offsets.begin(), s.relay_offsets.end());
    std::sort(offsets.begin(), offsets.end());
    s.relay_offset_ms = offsets[offsets.size() / 2];
  }
}

// "official": the frames the relay closed, kept until the ones before them are here. Their rows are of the seats
// joined: a packet that names other seats is not this race's (its length was checked against its own mask).
void TakeOfficial(Session& s, const relay::Official& o)
{
  if (s.seats.empty())
    return;
  if (o.player_seats != s.player_seats)
  {
    if (!s.refused_logged)
      Error("fzgx: the relay sent official frames of player seats {:08x}, this race's are {:08x}; not kept",
            o.player_seats, s.player_seats);
    s.refused_logged = true;
    return;
  }
  const u32 first = o.first_frame;
  const u32 count = o.count;
  const size_t row = s.seats.size() * RECORD;
  s.last_official = Clock::now();
  if (count > 0)
    s.target = std::max(s.target, first + count);
  for (u32 k = 0; k < count; ++k)
  {
    const u32 frame = first + k;
    if (frame < s.official.size())
      continue;
    if (u64{frame} >= u64{s.frontier} + MAX_BEHIND)
    {
      if (!s.refused_logged)
        Error("fzgx: the relay sent official frame {}, far past this game's step {}; not kept", frame, s.frontier);
      s.refused_logged = true;
      continue;
    }
    s.pending[frame].assign(o.records.begin() + k * row, o.records.begin() + (k + 1) * row);
  }
  for (auto it = s.pending.find(static_cast<u32>(s.official.size())); it != s.pending.end();
       it = s.pending.find(static_cast<u32>(s.official.size())))
  {
    s.official.push_back(std::move(it->second));
    s.pending.erase(it);
    NoteOfficialRow(s, static_cast<u32>(s.official.size()) - 1);
  }
}

// "desync": the relay found this game's state different from the majority's.
void TakeDesync(Session& s, const relay::Desync& d)
{
  ++s.desyncs;
  Warn("fzgx: this game's state differs from the majority at step {}", d.frame);
  Notice("fzgx: official rows that came together up to here: {}", s.summary.bursts.Words());
  board::Desync(d.frame);
  // The relay disconnected this game from the race (the AI drives its machine in the other games; a desynced game
  // is never repaired): stop, as leaving the results does, so it never races on alone even
  // without a launcher. Shutdown closes a replay being recorded.
  if (!s.exit_sent)
  {
    s.exit_sent = true;
    Notice("fzgx: the race desynced at step {} and the relay disconnected this game; stopping", d.frame);
    TheHost().RequestStop();
  }
}

// "seat back": a seat that came back is its player's again from a step on.
void TakeSeatBack(Session& s, const relay::SeatBack& b)
{
  if (static_cast<int>(b.seat) == s.my_seat && s.hand_back != b.frame)
  {
    // this seat is the player's again from that step (every game's official rows switch there)
    s.hand_back = b.frame;
    s.own_from = s.hand_back;
    s.returning = false;
    Notice("fzgx: this seat is the player's again from step {}", s.hand_back);
  }
  else if (static_cast<int>(b.seat) != s.my_seat)
    Notice("fzgx: seat {} is back in the race from step {}", b.seat, b.frame);
}

// A packet of the relay's, by its body after the header (the key's included, checked by Handle). A body of a length
// its type does not have decodes to nothing and is dropped.
void Body(Session& s, relay::Type type, const u8* b, size_t len)
{
  switch (type)
  {
  case relay::Type::Joined:
    if (const auto j = relay::DecodeJoined(b, len))
      TakeJoined(s, *j);
    break;
  case relay::Type::Go:
    if (const auto go = relay::DecodeGo(b, len); go && !s.go)
    {
      s.go = true;
      s.start_barrier = go->start_barrier;
      s.go_local = Clock::now();
    }
    break;
  case relay::Type::Pong:
    if (const auto p = relay::DecodePong(b, len))
      TakePong(s, *p);
    break;
  case relay::Type::Official:
    if (const auto o = relay::DecodeOfficial(b, len))
      TakeOfficial(s, *o);
    break;
  case relay::Type::Desync:
    if (const auto d = relay::DecodeDesync(b, len))
      TakeDesync(s, *d);
    break;
  case relay::Type::SeatLeft:
    if (const auto left = relay::DecodeSeatLeft(b, len))
      Notice("fzgx: seat {} left the race", left->seat);
    break;
  case relay::Type::SeatBack:
    if (const auto back = relay::DecodeSeatBack(b, len))
      TakeSeatBack(s, *back);
    break;
  default:
    break;
  }
}

void Handle(Session& s, const u8* p, size_t n)
{
  if (const auto version = relay::ForeignVersion(p, n))
  {
    if (!s.foreign_logged)
    {
      s.foreign_logged = true;
      Error("fzgx: the relay speaks protocol version {}, this game {}; its packets are ignored", *version,
            relay::VERSION);
    }
    return;
  }
  if (n < relay::HEADER_SIZE || p[0] != relay::MARK[0] || p[1] != relay::MARK[1])
    return;
  const u8* b = p + relay::HEADER_SIZE;
  const size_t len = n - relay::HEADER_SIZE;
  const auto type = static_cast<relay::Type>(p[3]);
  if (!relay::FromRelay(type))
    return;  // (no other packet comes from a relay)
  // Only a packet with this seat's key is the relay's: the address alone could be anyone's who can send from it,
  // on the path or forging it (protocol/relay.toml).
  if (len < relay::KEY_SIZE || Get64(b) != s.key)
  {
    if (!s.key_logged)
      Error("fzgx: a packet from the relay's address without this seat's key (type {}); such packets are ignored",
            static_cast<int>(p[3]));
    s.key_logged = true;
    return;
  }
  Body(s, type, b, len);
}
}  // namespace

u64 RelayKey(u64 token)
{
  constexpr char LABEL[] = "fzgx relay key";
  u8 input[sizeof(LABEL) - 1 + 8];
  std::copy_n(LABEL, sizeof(LABEL) - 1, input);
  for (int i = 0; i < 8; ++i)
    input[sizeof(LABEL) - 1 + i] = static_cast<u8>(token >> (8 * (7 - i)));
  const std::string hex = Sha1Hex(input, sizeof(input));
  u64 key = 0;
  for (int i = 0; i < 16; ++i)
    key = key << 4 | static_cast<u64>(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
  return key;
}

void Send(Session& s, const std::vector<u8>& packet)
{
  if (s.playback || Blackout(s))
    return;
  if (s.lag_ms == 0 && s.jitter_ms == 0)
    s.socket.Send(packet.data(), packet.size(), s.relay);
  else
    s.outgoing.emplace_back(Clock::now() + Held(s, s.lag_ms / 2), packet);
}

void Drain(Session& s)
{
  if (s.playback)
    return;
  FZGX_TIME(Drain);
  static std::vector<u8> buffer(65536);  // any UDP datagram (a 30-seat official packet once exceeded 2 KiB)
  std::size_t received = 0;
  Endpoint from;
  const auto now = Clock::now();
  const bool blackout = Blackout(s);
  while (s.socket.Receive(buffer.data(), buffer.size(), &received, &from))
  {
    if (from != s.relay || blackout)
      continue;
    if (s.lag_ms == 0 && s.jitter_ms == 0)
      Handle(s, buffer.data(), received);
    else
      s.lagged.emplace_back(now + Held(s, s.lag_ms - s.lag_ms / 2),
                            std::vector<u8>(buffer.begin(), buffer.begin() + received));
  }
  FlushOutgoing(s);
  Release(s.lagged, now, [&](const std::vector<u8>& packet) { Handle(s, packet.data(), packet.size()); });
}

void SendInputs(Session& s)
{
  // a seat the AI drives (away, or not yet given back): none of its records are sent, only how far this game has the
  // official frames (an Input of none), at most every 20 ms
  if (s.my_inputs.empty() || s.own_from == NEVER || s.my_inputs.rbegin()->first < s.own_from)
  {
    const auto now = Clock::now();
    if (s.phase == Phase::Running && now - s.last_ack > std::chrono::milliseconds(20))
    {
      s.last_ack = now;
      Send(s, relay::Encode(relay::Input{.token = s.token, .cookie = s.cookie, .first_frame = 0,
                                         .next_official = static_cast<u32>(s.official.size()), .count = 0,
                                         .records = {}}));
      s.last_input = now;
    }
    while (!s.my_inputs.empty() && s.my_inputs.begin()->first + MAX_FRAMES < s.official.size())
      s.my_inputs.erase(s.my_inputs.begin());
    return;
  }
  const u32 last = s.my_inputs.rbegin()->first;
  // From the first frame not yet official; but at least the newest input, late or not: in lockstep (the intro, the
  // countdown) the game runs behind the relay, so every local input is for a frame already closed, and none was sent:
  // the relay kept the seat at its neutral record through the countdown (no revving). Late, it becomes the seat's
  // latest input, used by the frames that close after it (RaceTimeline).
  const u32 first = std::max(s.own_from, std::min(last, std::max<u32>(static_cast<u32>(s.official.size()),
                                                 last + 1 > MAX_FRAMES ? last + 1 - MAX_FRAMES : 0)));
  relay::Input input{.token = s.token, .cookie = s.cookie, .first_frame = first,
                     .next_official = static_cast<u32>(s.official.size()), .count = 0, .records = {}};
  u32 count = 0;
  for (u32 f = first; f <= last; ++f)
  {
    const auto it = s.my_inputs.find(f);
    if (it == s.my_inputs.end())
      break;
    input.records.insert(input.records.end(), it->second.begin(), it->second.end());
    ++count;
  }
  input.count = static_cast<u8>(count);
  Send(s, relay::Encode(input));
  s.last_input = Clock::now();
  while (!s.my_inputs.empty() && s.my_inputs.begin()->first + MAX_FRAMES < s.official.size())
    s.my_inputs.erase(s.my_inputs.begin());
}

void Ping(Session& s)
{
  const auto now = Clock::now();
  if (now - s.last_ping < (s.go ? 100ms : 500ms))  // in the race often: the race clock follows the relay's
    return;
  s.last_ping = now;
  const u64 ns = static_cast<u64>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
  // (the token: the pong carries the seat's key)
  Send(s, relay::Encode(relay::Ping{.nonce = 0, .client_ticks = static_cast<std::int64_t>(ns), .token = s.token}));
}
int ExitCode()
{
  return s_exit_code;
}
void ClearExitCode()
{
  s_exit_code = 0;
}
void SetExitCode(int code)
{
  s_exit_code = code;
}
}  // namespace fzgx::net
