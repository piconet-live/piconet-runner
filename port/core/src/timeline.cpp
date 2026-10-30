// The racer timeline: the race's events, found while the
// replay scrubber's front edge buffers. After each front step (scrub.cpp, TookFrontStep) the machines' blocks are
// read, never written, and compared with what the step before left. A playback's only.
//
// Where each event comes from:
//   KO        flags +0x0 gains 0x80 (out of energy: fn_1_24F10 sets it when the energy goes below 0, and its KO is
//             that moment) without 0x800 in the same step, the racer not finished (0x10000). The attacker: a racer
//             whose skull count +0x482 grew in the same step (fn_1_2A318 credits it in the same contact call). With
//             several, the one whose "last machine approached" +0x4C1 is the victim, or the victim's is it.
//   Out       flags gains 0x800 (out of the course: fn_1_25004, which sets 0x80 with it; fn_1_24A4C, the fall)
//             with no 0x80 before: destroyed without a KO, a fall.
//   Retired   flags gains 0x08000000 alone (the launcher's "out" mask, playback.cpp); none seen so far.
//   Finish    flags gains 0x10000 (fn_1_250F8); the time is the sum of the laps, progress record +0x1D8 (as the
//             recording's results take it); the place is the order of finishing, ties in a step by that time.
//   Lap       laps completed, progress record +0x118, grows, without the finish.
//   FastLap   at a lap or the finish, the lap just completed (progress record +0x16C, "1 lap ago") is the fastest
//             of the race so far.
//   Lead      the first racer: a finisher if any (from then on there are no more changes), else the racer still in
//             with the most distance over all laps, progress record +0xF8 (what the launcher's standings order by;
//             the game's rank byte +0x115 is not the race's place). A new leader counts once it has led for
//             LEAD_STEPS: a change and its change back within that are not an event.
//   Rail, Hit and the one-second rule: timeline_hits.cpp.
//   GO        the race clock: racer 0's race time, progress record +0x1E4 (frames, u32), first above 0.
// Places (a lap's, the rows' order) follow the same order: finished, by place; racing, by distance; out, by the
// distance they died with.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include "files.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <numeric>

namespace fzgx::net
{
namespace
{
constexpr u32 NONE = 0xFFFFFFFF;
constexpr u32 BLOCK = game::MACHINE_SIZE;
constexpr u32 LEAD_STEPS = 30;  // half a second
constexpr u32 OUT_FLAGS = game::MACHINE_FLAG_RETIRED | game::MACHINE_FLAG_OUT | game::MACHINE_FLAG_BROKEN_DOWN;
// stand-in colours for a seat whose set-up names none (bit 24 says so)
constexpr u32 STAND_IN[8] = {0xE6194B, 0x3CB44B, 0xFFE119, 0x4363D8, 0xF58231, 0x911EB4, 0x46F0F0, 0xF032E6};

u32 Ms(Guest& g, u32 at)
{
  return g.Read8(at + 8) * 60000u + g.Read8(at + 9) * 1000u + g.Read16(at + 10);
}

float F(Guest& g, u32 at)
{
  const float v = std::bit_cast<float>(g.Read32(at));
  return std::isfinite(v) ? v : 0.0f;
}

}  // namespace

// The seats' colours from the race's set-up (the match or the replay's header): "color" {r, g, b}; and their names.
void SetupColours(Timeline& t_line)
{
  for (u32 i = 0; i < Timeline::MOST; ++i)
    t_line.colour[i] = STAND_IN[i % 8] | 0x1000000;
  const boot::Match* match = boot::Get();
  const picojson::object* setup = match ? boot::Object(match->json, "setup") : nullptr;
  const auto seats = setup ? setup->find("seats") : picojson::object::const_iterator{};
  if (!setup || seats == setup->end() || !seats->second.is<picojson::array>())
    return;
  for (const picojson::value& v : seats->second.get<picojson::array>())
  {
    if (!v.is<picojson::object>())
      continue;
    const auto& seat = v.get<picojson::object>();
    const auto n = seat.find("seat");
    const picojson::object* c = boot::Object(seat, "color");
    if (n == seat.end() || !n->second.is<double>())
      continue;
    const double i = n->second.get<double>();
    const auto part = [&](const char* key) {
      if (!c)
        return 0u;
      const auto it = c->find(key);
      return it != c->end() && it->second.is<double>() ? static_cast<u32>(std::clamp(it->second.get<double>(), 0.0, 255.0)) : 0u;
    };
    if (i < 0 || i >= Timeline::MOST)
      continue;
    t_line.colour[static_cast<u32>(i)] = part("r") << 16 | part("g") << 8 | part("b");
    const std::string name = boot::Text(seat, "name");
    std::size_t length = 0;
    for (const char ch : name)
      if (length + 1 < sizeof(t_line.names[0]) && ch >= 0x20 && ch < 0x7F)
        t_line.names[static_cast<u32>(i)][length++] = ch;
  }
}

namespace
{

bool IsOut(const TimelineState& t, u32 i)
{
  return t.line.out[i] != 0;
}

bool IsFinished(const TimelineState& t, u32 i)
{
  return (t.flags[i] & game::MACHINE_FLAG_FINISHED) != 0 && !IsOut(t, i);
}

// The racers in the race's order now: finished (by place), racing (by distance), out (by the distance they died with).
std::array<u8, 30> Order(const TimelineState& t, std::array<u8, 30>* places = nullptr)
{
  std::array<u8, 30> order{};
  std::iota(order.begin(), order.begin() + t.racers, 0);
  const auto group = [&](u32 i) { return IsOut(t, i) ? 2 : IsFinished(t, i) ? 0 : 1; };
  std::stable_sort(order.begin(), order.begin() + t.racers, [&](u8 a, u8 b) {
    if (group(a) != group(b))
      return group(a) < group(b);
    if (group(a) == 0)
      return t.line.end[a] != t.line.end[b] ? t.line.end[a] < t.line.end[b] : t.finish_ms[a] < t.finish_ms[b];
    return t.distance[a] > t.distance[b];
  });
  if (places)
    for (u32 k = 0; k < t.racers; ++k)
      (*places)[order[k]] = static_cast<u8>(k + 1);
  return order;
}

}  // namespace

void TimelineAdd(TimelineState& t, const TimelineEvent& e)
{
  std::vector<TimelineEvent>& events = t.line.events;
  // (a lead change is dated back to when it began: keep them by step)
  auto at = events.end();
  while (at != events.begin() && std::prev(at)->step > e.step)
    --at;
  events.insert(at, e);
  ++t.line.version;
}

namespace
{

void Start(Guest& g, TimelineState& t, u32 machines, u32 count, u32 step)
{
  t.started = true;
  t.racers = std::min<u32>(count, Timeline::MOST);
  t.line.racers = t.racers;
  t.line.first = step;
  t.next = step;
  SetupColours(t.line);
  for (u32 i = 0; i < t.racers; ++i)
  {
    const u32 base = machines + i * BLOCK;
    const u32 progress = g.Read32(base + game::MACHINE_PROGRESS);
    t.flags[i] = g.Read32(base);
    t.skulls[i] = g.Read8(base + game::MACHINE_SKULLS);
    t.laps[i] = progress ? g.Read32(progress + game::PROGRESS_LAPS) : 0;
    t.distance[i] = progress ? F(g, progress + game::PROGRESS_DISTANCE) : 0;
    t.line.end[i] = NONE;
  }
  for (u32 i = t.racers; i < Timeline::MOST; ++i)
    t.line.end[i] = NONE;
  t.rail_shown.fill(NONE);
  for (auto& row : t.hit_shown)
    row.fill(NONE);
  const std::array<u8, 30> order = Order(t);
  t.leader = t.racers ? order[0] : -1;
  ++t.line.version;
}

// One racer's step: what changed since the step before. KO victims and attackers are paired afterwards.
struct Change
{
  u32 before = 0;  // the flags the step before left
  bool ko = false;
  u32 credits = 0;
  u32 approached = 0xFF;
};

// A racer's place now (1 first).
u8 PlaceNow(const TimelineState& t, u32 i)
{
  std::array<u8, 30> places{};
  Order(t, &places);
  return places[i];
}

// One racer after a step: its end (finished or out), its lap, its fastest lap; a KO waits for its attacker.
void ReadRacer(Guest& g, TimelineState& t, u32 base, u32 i, u32 step, Change* change)
{
  const u32 progress = g.Read32(base + game::MACHINE_PROGRESS);
  const u32 flags = g.Read32(base);
  const u32 before = t.flags[i];
  change->before = before;
  const u32 skulls = g.Read8(base + game::MACHINE_SKULLS);
  change->credits = skulls > t.skulls[i] ? skulls - t.skulls[i] : 0;
  change->approached = g.Read8(base + game::MACHINE_APPROACHED);
  t.skulls[i] = skulls;
  t.flags[i] = flags;
  if (progress == 0)
    return;
  if (!IsOut(t, i))
    t.distance[i] = F(g, progress + game::PROGRESS_DISTANCE);
  if (t.line.go == NONE && i == 0 && g.Read32(progress + game::PROGRESS_RACE_TIME) != 0)
    t.line.go = step + 1 - g.Read32(progress + game::PROGRESS_RACE_TIME);
  const u32 gained = flags & ~before;
  if (!IsOut(t, i) && (before & game::MACHINE_FLAG_FINISHED) == 0 && (gained & game::MACHINE_FLAG_FINISHED) == 0 && (gained & OUT_FLAGS) != 0)
  {
    t.line.out[i] = 1;
    t.line.end[i] = step;
    if ((gained & 0x800) != 0 && (before & 0x80) == 0)
      TimelineAdd(t, {step, TimelineEvent::Out, static_cast<u8>(i)});
    else if ((gained & 0x80) != 0)
      change->ko = true;  // (its event once the attackers are known)
    else
      TimelineAdd(t, {step, TimelineEvent::Retired, static_cast<u8>(i)});
  }
  const u32 laps = g.Read32(progress + game::PROGRESS_LAPS);
  const bool lap = static_cast<s32>(laps) > static_cast<s32>(t.laps[i]) && static_cast<s32>(laps) >= 1 && !IsOut(t, i);
  t.laps[i] = laps;
  if ((gained & game::MACHINE_FLAG_FINISHED) != 0 && !IsOut(t, i))
  {
    t.finish_ms[i] = Ms(g, progress + game::PROGRESS_LAP_SUM);
    t.line.end[i] = step;
    ++t.finished;
    TimelineAdd(t, {step, TimelineEvent::Finish, static_cast<u8>(i), 0xFF, PlaceNow(t, i), t.finish_ms[i]});
  }
  else if (lap)
    TimelineAdd(t, {step, TimelineEvent::Lap, static_cast<u8>(i), 0xFF, PlaceNow(t, i), laps});
  const u32 ms = lap ? Ms(g, progress + 0x16C) : 0;
  if (ms != 0 && ms < t.fastest_ms)
  {
    t.fastest_ms = ms;
    TimelineAdd(t, {step, TimelineEvent::FastLap, static_cast<u8>(i), 0xFF, 0, ms});
  }
}

// The KOs of a step: each victim with an attacker credited in the same step (the one near it when several were).
void PairKOs(TimelineState& t, std::array<Change, 30>& changes, u32 step)
{
  for (u32 v = 0; v < t.racers; ++v)
  {
    if (!changes[v].ko)
      continue;
    u32 by = 0xFF, candidates = 0, only = 0xFF;
    for (u32 a = 0; a < t.racers && by == 0xFF; ++a)
    {
      if (a == v || changes[a].credits == 0)
        continue;
      ++candidates;
      only = a;
      if (changes[v].approached == a || changes[a].approached == v)
        by = a;
    }
    if (by == 0xFF && candidates == 1)
      by = only;  // (several, none of them near: not known)
    if (by != 0xFF)
      --changes[by].credits;
    TimelineAdd(t, {step, TimelineEvent::KO, static_cast<u8>(v), static_cast<u8>(by)});
  }
}

// The lead: no more changes once someone has finished.
void Lead(TimelineState& t, u32 step)
{
  if (t.finished != 0 || t.racers == 0)
    return;
  const int now = Order(t)[0];
  if (IsOut(t, static_cast<u32>(now)) || now == t.leader)
  {
    t.candidate = -1;
    return;
  }
  if (now != t.candidate)
  {
    t.candidate = now;
    t.candidate_since = step;
  }
  else if (step - t.candidate_since >= LEAD_STEPS)
  {
    TimelineAdd(t, {t.candidate_since, TimelineEvent::Lead, static_cast<u8>(now), static_cast<u8>(t.leader)});
    t.leader = now;
    t.candidate = -1;
  }
}

void Read(Guest& g, TimelineState& t, u32 machines, u32 step)
{
  std::array<Change, 30> changes{};
  for (u32 i = 0; i < t.racers; ++i)
    ReadRacer(g, t, machines + i * BLOCK, i, step, &changes[i]);
  PairKOs(t, changes, step);
  std::array<u32, 30> before{};
  std::array<u8, 30> approached{};
  for (u32 i = 0; i < t.racers; ++i)
  {
    before[i] = changes[i].before;
    approached[i] = static_cast<u8>(changes[i].approached);
  }
  TimelineHits(g, t, machines, step, before, approached);
  Lead(t, step);
}

// Every SAMPLE_EVERY steps: each racer's energy (+0x184 of its most, +0x5DA) and whether the computer drives it: the
// step's record for its seat is the "AI drives" one (steering 0x80: frame.cpp, AI_DRIVES; the seat routine then keeps
// the AI's own controls), or it is no player's seat. (Flags 0x04000000 tell nothing: every seat of our races is made
// a CPU's, whose controls the seat routine replaces.)
void Sample(Guest& g, const Session& s, TimelineState& t, u32 machines, u32 step)
{
  std::array<bool, 30> computer{};
  computer.fill(true);
  if (step < s.official.size())
    for (std::size_t j = 0; j < s.seats.size(); ++j)
      if (const int seat = s.seats[j]; seat >= 0 && seat < 30 && (j + 1) * RECORD <= s.official[step].size())
        computer[seat] = s.official[step][j * RECORD + 1] == 0x80;
  std::array<u8, 30> places{};
  Order(t, &places);
  for (u32 i = 0; i < t.racers; ++i)
    t.line.places.push_back(places[i]);
  for (u32 i = 0; i < t.racers; ++i)
  {
    const u32 base = machines + i * BLOCK;
    const float most = static_cast<float>(g.Read16(base + game::MACHINE_MAX_ENERGY));
    const float energy = F(g, base + game::MACHINE_ENERGY);
    const u32 share = IsOut(t, i) || most <= 0 ? 0 : static_cast<u32>(std::clamp(energy / most, 0.0f, 1.0f) * 127.0f + 0.5f);
    t.line.samples.push_back(static_cast<u8>(share | (computer[i] ? 0x80 : 0)));
  }
  ++t.line.version;
}

std::string RaceClock(const Timeline& line, u32 step)
{
  const u32 go = line.go != NONE ? line.go : line.first;
  const bool before = step < go;
  const u32 tenths = (before ? go - step : step - go) / 6;
  return std::format("{}{}:{:02}.{}", before ? "-" : "", tenths / 600, tenths / 10 % 60, tenths % 10);
}

std::string Ms(u32 ms)
{
  return std::format("{}:{:02}.{:03}", ms / 60000, ms / 1000 % 60, ms % 1000);
}
}  // namespace

void TimelineStep(Guest& g, Session& s, u32 step)
{
  TimelineState& t = s.timeline;
  if (t.started && step < t.next)
    return;  // (read once; the front edge never runs a step twice, but say so here)
  const auto began = std::chrono::steady_clock::now();
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = g.Read8(module + MACHINE_COUNT);
  if (machines == 0 || count == 0)
    return;
  if (!t.started)
    Start(g, t, machines, count, step);
  Read(g, t, machines, step);
  if ((step - t.line.first) % Timeline::SAMPLE_EVERY == 0)
    Sample(g, s, t, machines, step);
  t.next = step + 1;
  const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count();
  t.us += us;
  t.most_us = std::max(t.most_us, us);
  ++t.read;
}

void TimelineSend(Session& s)
{
  TimelineState& t = s.timeline;
  if (!t.started || t.sent == t.line.version)
    return;
  t.line.front = s.scrub.front;
  const std::array<u8, 30> order = Order(t);
  std::copy(order.begin(), order.end(), t.line.order);
  TheHost().ReplayTimeline(t.line);
  t.sent = t.line.version;
}

void TimelineSummary(Session& s)
{
  TimelineState& t = s.timeline;
  if (!t.started || std::exchange(t.said, true))
    return;
  const Timeline& line = t.line;
  std::array<u32, 10> kinds{};
  for (const TimelineEvent& e : line.events)
    ++kinds[std::min<u32>(e.kind, 9)];
  const std::size_t bytes = line.events.capacity() * sizeof(TimelineEvent) + line.samples.capacity() +
                            line.places.capacity() + sizeof(TimelineState);
  Notice("fzgx: timeline: {} steps read ({:.1f} us a step on average, {:.1f} us at most); {} events (KO {}, out {}, "
         "retired {}, finish {}, lap {}, lead {}, fastest lap {}, rail {}, hit {}; {} hits folded by the one-second "
         "rule); {} samples a racer; {:.1f} KiB in all; GO at step {}",
         t.read, t.read ? t.us / t.read : 0.0, t.most_us, line.events.size(), kinds[1], kinds[2], kinds[3], kinds[4],
         kinds[5], kinds[6], kinds[7], kinds[8], kinds[9], t.folded, t.racers ? line.samples.size() / t.racers : 0,
         bytes / 1024.0, line.go);
  std::array<u8, 30> rows{};
  const std::array<u8, 30> order = Order(t);
  for (u32 k = 0; k < t.racers; ++k)
    rows[order[k]] = static_cast<u8>(k + 1);
  std::string text;
  for (u32 k = 0; k < t.racers; ++k)
    text += std::format("{}P{}=racer {}{}", k ? ", " : "", k + 1, order[k], line.out[order[k]] ? " (out)" : "");
  Notice("fzgx: timeline: rows: {}", text);
  for (const TimelineEvent& e : line.events)
  {
    const auto who = [&](u32 i) { return std::format("P{} (racer {})", rows[i], i); };
    std::string what;
    switch (e.kind)
    {
    case TimelineEvent::KO: what = std::format("KO {}{}", who(e.racer), e.other != 0xFF ? " by " + who(e.other) : ""); break;
    case TimelineEvent::Out: what = std::format("out {}", who(e.racer)); break;
    case TimelineEvent::Retired: what = std::format("retired {}", who(e.racer)); break;
    case TimelineEvent::Finish: what = std::format("finish {} place {} time {}", who(e.racer), e.place, Ms(e.value)); break;
    case TimelineEvent::Lap: what = std::format("lap {} {} place {}", e.value, who(e.racer), e.place); break;
    case TimelineEvent::Lead: what = std::format("lead {}{}", who(e.racer), e.other != 0xFF ? " from " + who(e.other) : ""); break;
    case TimelineEvent::FastLap: what = std::format("fastest lap {} {}", who(e.racer), Ms(e.value)); break;
    case TimelineEvent::Rail: what = std::format("rail {} (flag {}){}", who(e.racer), e.place, e.value ? std::format(" +{} folded", e.value) : ""); break;
    case TimelineEvent::Hit: what = std::format("hit {} with {}{}", who(e.racer), who(e.other), e.value ? std::format(" +{} folded", e.value) : ""); break;
    default: break;
    }
    Notice("fzgx: timeline: {} step {} {}", RaceClock(line, e.step), e.step, what);
  }
}
}  // namespace fzgx::net
