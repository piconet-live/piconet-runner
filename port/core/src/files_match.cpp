// A race's match (the launch file's "match" part, files_launch.cpp; protocol/formats.md section 2): the
// coordinator's match start as the launcher wrote it, with this player's seat and relay. Read here into the race the
// game is set up with, and refused when a number is outside what the game has. A replay's header holds a match start
// too and goes through FromMatch. Lifted from the Dolphin fork (dolphin-fzgx Core/FzgxBoot.cpp at 5bdc7c640e).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fzgx::boot
{
namespace
{
constexpr size_t MAX_NAME = 16;        // what a results row fits
// The stage numbers that hold a course: the 26 cup courses, the story's stages 37 to
// 45, and the scenes 49 and 50.
constexpr std::array<u8, 37> COURSES = {1,  3,  5,  7,  8,  9,  10, 11, 13, 14, 15, 16, 17, 21, 24, 25, 26, 27, 28,
                                        29, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 49, 50};

double Number(const picojson::object& o, const std::string& key, double fallback)
{
  const auto it = o.find(key);
  return it != o.end() && it->second.is<double>() ? it->second.get<double>() : fallback;
}

// A colour's part: 0 to 255, whatever number the file holds.
u8 Shade(const picojson::object& o, const std::string& key)
{
  const double v = Number(o, key, 0);
  return static_cast<u8>(v >= 0 && v <= 255 ? v : v > 255 ? 255 : 0);
}

// A player name as the results screens' font draws it (namecodes.game_text): A-Z (lower case drawn as upper case),
// 0-9, space, '.', '-', and '&', stored as ','; spaces collapsed; at most MAX_NAME characters.
std::string GameText(const std::string& name)
{
  std::string out;
  bool space = false;
  for (const char c : name)
  {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80)
      continue;
    if (c == ' ')
    {
      space = !out.empty();
      continue;
    }
    if (!std::isalnum(u) && c != '.' && c != '-' && c != '&')
      continue;
    if (space)
      out += ' ';
    space = false;
    out += c == '&' ? ',' : static_cast<char>(std::toupper(u));
  }
  return out.substr(0, MAX_NAME);
}
}  // namespace

// JSON that somebody else wrote, before it is parsed: the parser here walks arrays and objects by recursion, one call
// a level, with no limit, so a header that opens some ten thousand brackets was the end of the stack (finding L4).
// A match and a replay's header are five levels deep.
bool TooDeep(const std::string& json, int limit)
{
  int depth = 0;
  bool text = false;
  for (size_t i = 0; i < json.size(); ++i)
  {
    const char c = json[i];
    if (text)
    {
      if (c == '\\')
        ++i;
      else if (c == '"')
        text = false;
    }
    else if (c == '"')
      text = true;
    else if ((c == '[' || c == '{') && ++depth > limit)
      return true;
    else if (c == ']' || c == '}')
      --depth;
  }
  return false;
}

// The same text, for a number too large to be one (1e999, four hundred digits): the parser ends the program on it
// (the constructor of its value of one), so a file that holds one is refused before it is parsed. Numbers are looked
// for outside texts, where the parser reads them.
bool TooLarge(const std::string& json)
{
  bool text = false;
  for (size_t i = 0; i < json.size(); ++i)
  {
    const char c = json[i];
    if (text)
    {
      if (c == '\\')
        ++i;
      else if (c == '"')
        text = false;
    }
    else if (c == '"')
      text = true;
    else if (c == '-' || (c >= '0' && c <= '9'))
    {
      char* end = nullptr;
      if (!std::isfinite(std::strtod(json.c_str() + i, &end)))
        return true;
      i = std::max(i + 1, static_cast<size_t>(end - json.c_str())) - 1;
    }
  }
  return false;
}

// A member that must be a whole number from low to high: its value, the fallback when the member is not there, and
// nothing when it is there and is anything else (text, a fraction, a number outside the range, one too large to be
// a number at all).
std::optional<u32> Whole(const picojson::object& o, const std::string& key, u32 fallback, u32 low, u32 high)
{
  const auto it = o.find(key);
  if (it == o.end())
    return fallback;
  if (!it->second.is<double>())
    return std::nullopt;
  const double v = it->second.get<double>();
  if (!(v >= low && v <= high) || v != std::floor(v))
    return std::nullopt;
  return static_cast<u32>(v);
}

std::string Text(const picojson::object& o, const std::string& key)
{
  const auto it = o.find(key);
  return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : "";
}

const picojson::object* Object(const picojson::object& o, const std::string& key)
{
  const auto it = o.find(key);
  return it != o.end() && it->second.is<picojson::object>() ? &it->second.get<picojson::object>() : nullptr;
}

// A seat's custom machine ("custom": body, cockpit, booster, pilot, colors) into its entry of the race block
// (files.h RACE_CUSTOM). Every member is required but the colours (three whole numbers, 32 bits each; absent: 0,
// the game's own colour of each part). A colour other than 0 is red, green, blue and the paint slot, which must be
// the parts' (another slot paints nothing). False: a member missing, of another
// kind or outside its range.
bool CustomOfSeat(const picojson::object& seat, u8* entry)
{
  const picojson::object* c = Object(seat, "custom");
  if (!c)
    return false;
  const std::optional<u32> body = Whole(*c, "body", 99, 0, 24);
  const std::optional<u32> cockpit = Whole(*c, "cockpit", 99, 25, 49);
  const std::optional<u32> booster = Whole(*c, "booster", 99, 50, 74);
  const std::optional<u32> pilot = Whole(*c, "pilot", 99, 0, MAX_MACHINE);
  if (!body || !cockpit || !booster || !pilot || *body == 99 || *cockpit == 99 || *booster == 99 || *pilot == 99)
    return false;
  std::array<u32, 3> colors{};
  if (const auto it = c->find("colors"); it != c->end())
  {
    if (!it->second.is<picojson::array>() || it->second.get<picojson::array>().size() != 3)
      return false;
    const auto& list = it->second.get<picojson::array>();
    for (size_t i = 0; i < 3; ++i)
    {
      const picojson::object one{{"v", list[i]}};
      const std::optional<u32> word = Whole(one, "v", 0, 0, 0xFFFFFFFF);
      if (!word || (*word != 0 && (*word & 0xFF) != file::PART_PAINT_SLOT))
        return false;
      colors[i] = *word;
    }
  }
  Put32(entry, 0xC0000000);
  const std::array<u32, 3> parts = {*body, *cockpit, *booster};
  for (size_t i = 0; i < 3; ++i)
  {
    entry[4 + 8 * i] = static_cast<u8>(parts[i]);
    Put32(entry + 8 + 8 * i, colors[i]);
  }
  entry[28] = static_cast<u8>(*pilot);
  return true;
}

u32 OtherSoundsWord(const std::string& sounds)
{
  // the launcher's "other racers' sounds": full, quieter (the game's split-screen factor taken to 30, 0.51), off
  return sounds == "full" ? 257 : sounds == "off" ? 1 : 132;
}

// A seat's custom machine into the race block and its machine (CUSTOM_MACHINE), or the reason it is refused in the log.
bool CustomSeat(Match& m, const picojson::object& s, int seat, const std::string& path, u8& machine)
{
  // a garage-built machine: its parts, its pilot and its colours, checked by kind before the game sees them (an id
  // of the wrong kind is read past the game's tables: fgx-re's exploit), into the seat's entry
  if (!CustomOfSeat(s, m.race.data() + RACE_CUSTOM + seat * CUSTOM_ENTRY))
  {
    Error("fzgx: {}: seat {}'s custom machine is not one the game can build (a body 0-24, a cockpit 25-49, a "
          "booster 50-74, a pilot 0-{}, three colour words of paint slot 1): refused", path, seat, MAX_MACHINE);
    return false;
  }
  machine = CUSTOM_MACHINE;
  return true;
}

namespace
{
// The host's rules (Online.Protocol RaceRules, a match start's "rules"; none in a replay from before them) into the
// race block: the CPUs' level (Master unless set), boosting on the first lap, restore, the race's end after the
// first finish, the mines' coming back.
void RulesIntoRace(Match& m, const picojson::object* rules)
{
  u8* r = m.race.data();
  const auto flag = [&](const char* key) {
    const auto it = rules ? rules->find(key) : picojson::object::const_iterator{};
    return rules && it != rules->end() && it->second.is<bool>() && it->second.get<bool>();
  };
  Put32(r + RACE_CPULEVEL, static_cast<u32>(std::clamp(rules ? Number(*rules, "cpuLevel", 3) : 3.0, 0.0, 3.0)));
  m.boost_from_start = flag("boostFromStart");
  Put32(r + RACE_BOOST, m.boost_from_start ? 1 : 0);
  Put32(r + RACE_RESTORE, flag("restore") ? 1 : 0);
  // the race ends for everyone this many seconds after the first finish; a match start without it (every recording
  // made before the rule) has no such end
  const double limit = std::clamp(rules ? Number(*rules, "finishLimitSeconds", 0) : 0.0, 0.0, double{MAX_FINISH_LIMIT});
  Put32(r + RACE_FINISH_LIMIT, static_cast<u32>(limit) * STEPS_A_SECOND);
  // a mine that went off comes back this many milliseconds later, as whole steps; a match start without it (every
  // recording made before the rule) keeps the game's own way, a mine stays gone
  const double mines = std::clamp(rules ? Number(*rules, "mineRegenMs", 0) : 0.0, 0.0, double{MAX_MINE_REGEN_MS});
  Put32(r + RACE_MINE_REGEN, static_cast<u32>(mines) * STEPS_A_SECOND / 1000);
}

// The start grid of the set-up ("grid", the coordinator's RaceSetup.Grid: the seats in starting order, pole first)
// into the race block: RACE_GRID_ON 1 and each slot's seat at RACE_GRID. None (absent or null): the word stays 0
// and the game sorts its own grid, as every race and recording before it. One that is not every seat of the race
// exactly once (as many entries as racers, each a whole number below that, none twice) is refused, never bent into
// shape: the image's site would hand the game a machine it does not have (the security audit's finding H5).
bool GridIntoRace(Match& m, const picojson::object& setup, u32 racers, const std::string& path)
{
  const auto it = setup.find("grid");
  if (it == setup.end() || it->second.is<picojson::null>())
    return true;
  const bool list = it->second.is<picojson::array>();
  const auto& grid = list ? it->second.get<picojson::array>() : picojson::array{};
  std::array<bool, MAX_SEATS> seen{};
  bool whole = list && grid.size() == racers;
  for (size_t slot = 0; whole && slot < grid.size(); ++slot)
  {
    const picojson::value& v = grid[slot];
    const double seat = v.is<double>() ? v.get<double>() : -1;
    whole = seat >= 0 && seat < racers && seat == std::floor(seat) && !seen[static_cast<u32>(seat)];
    if (whole)
    {
      seen[static_cast<u32>(seat)] = true;
      m.race[RACE_GRID + slot] = static_cast<u8>(seat);
    }
  }
  if (!whole)
  {
    Error("fzgx: {}: the start grid is not every seat of the race's {} once: refused", path, racers);
    return false;
  }
  Put32(m.race.data() + RACE_GRID_ON, 1);
  return true;
}
}  // namespace

bool HasGrid(const Match& m)
{
  const u8* w = m.race.data() + RACE_GRID_ON;
  return (w[0] | w[1] | w[2] | w[3]) != 0;
}

bool OfVersion7(const Match& m)
{
  const u8* band = m.race.data() + RACE_RUBBER_BAND_ON;
  return HasGrid(m) || (band[0] | band[1] | band[2] | band[3]) == 0;
}

// The race's seats from the match (the coordinator's MatchStart, or a replay header's copy): machines, the player seats,
// names and colours; the race block's setup words.
// default_setting: a seat's machine setting when the match names none. A match start's is the middle (50). A replay
// from before the host's rules (no "rules" in its set-up; the fork's recordings up to 2026-10-03) was raced before
// the mod took settings from the match at all, which is what 100 gives: read with 50 such a replay differed from
// its recording from the race's first moving step (step 183) on, and with 100 it plays as recorded.
bool FromMatch(Match& m, const picojson::object& json, const std::string& path, double default_setting)
{
  const picojson::object* setup = Object(json, "setup");
  const auto seats_it = setup ? setup->find("seats") : picojson::object::const_iterator{};
  if (!setup || seats_it == setup->end() || !seats_it->second.is<picojson::array>())
  {
    Error("fzgx: {}: no race set-up or seats", path);
    return false;
  }
  m.json = json;
  const auto& seats = seats_it->second.get<picojson::array>();
  const std::optional<u32> course = Whole(*setup, "course", 1, 0, 255);
  const std::optional<u32> lap_count = Whole(*setup, "laps", 3, 0, MAX_LAPS);
  const std::optional<u32> shuffle = Whole(*setup, "shuffleSeed", 0x1234, 0, 0xFFFFFFFF);
  const std::optional<u32> cpu_seed = Whole(*setup, "cpuSeed", 0x12345678, 0, 0xFFFFFFFF);
  if (seats.empty() || !course || std::find(COURSES.begin(), COURSES.end(), *course) == COURSES.end() || !lap_count ||
      !shuffle || !cpu_seed)
  {
    Error("fzgx: {}: the race's set-up is not one the game has (no seats, a course the disc does not hold, laps "
          "above {}, or a seed that is no number): refused",
          path, MAX_LAPS);
    return false;
  }
  if (!StatsOfSetup(*setup, path, &m.stats))
    return false;
  if (!PacksOfSetup(*setup, path, &m.packs))
    return false;
  const u32 racers = static_cast<u32>(std::min<size_t>(seats.size(), MAX_SEATS));
  const u32 laps = *lap_count;
  std::array<u8, 30> machines{};
  for (const picojson::value& v : seats)
  {
    if (!v.is<picojson::object>())
      continue;
    const auto& s = v.get<picojson::object>();
    const std::optional<u32> seat_number = Whole(s, "seat", MAX_SEATS, 0, MAX_SEATS - 1);
    if (!seat_number || *seat_number >= MAX_SEATS)
      continue;
    const int seat = static_cast<int>(*seat_number);
    const std::optional<u32> machine = Whole(s, "machine", 0, 0, MAX_MACHINE);
    if (!machine)
    {
      Error("fzgx: {}: seat {} names a machine the game does not have: refused", path, seat);
      return false;
    }
    machines[seat] = static_cast<u8>(*machine);
    if (s.find("custom") != s.end() && !CustomSeat(m, s, seat, path, machines[seat]))
      return false;
    // a pack of the seat's own: 0 the disc's, k the race's k-th of setup.packs; none, the race's pack
    if (s.find("pack") != s.end())
    {
      const std::optional<u32> pack = Whole(s, "pack", 0, 0, static_cast<u32>(m.packs.size()));
      if (!pack)
      {
        Error("fzgx: {}: seat {} names a pack the race does not have: refused", path, seat);
        return false;
      }
      m.seat_pack[seat] = static_cast<int>(*pack);
    }
    m.race[RACE_SETTINGS + seat] = static_cast<u8>(std::clamp(Number(s, "setting", default_setting), 0.0, 100.0));
    // a stock colour (0-3, the machine's own) wins over the colour, which is then only its swatch for the lists
    const auto stock = s.find("stockColor");
    if (stock != s.end() && stock->second.is<double>() && stock->second.get<double>() >= 0 &&
        stock->second.get<double>() < 4)
    {
      m.stock[seat] = static_cast<int>(stock->second.get<double>());
    }
    else if (const picojson::object* c = Object(s, "color"))
    {
      m.colors[seat] = paint::Rgb{Shade(*c, "r"), Shade(*c, "g"), Shade(*c, "b")};
    }
    const auto cpu = s.find("cpu");
    const bool is_cpu = cpu != s.end() && cpu->second.is<bool>() && cpu->second.get<bool>();
    if (!Text(s, "playerId").empty())
    {
      m.player_mask |= 1u << seat;
      if (!is_cpu)
      {
        const std::string name = GameText(Text(s, "name"));
        std::memcpy(m.names.data() + seat * NAME_SIZE, name.data(), name.size());
      }
    }
  }
  u8* r = m.race.data();
  std::memcpy(r, "FZRC", 4);
  Put32(r + 4, *course);
  Put32(r + 8, machines[0]);
  Put32(r + 12, racers - 1);
  Put32(r + 16, laps);
  Put32(r + 20, laps ? 6 : 3);
  Put32(r + 24, *shuffle);
  Put32(r + 28, *cpu_seed);
  std::memcpy(r + 32, machines.data(), machines.size());
  RulesIntoRace(m, Object(*setup, "rules"));
  if (!GridIntoRace(m, *setup, racers, path))
    return false;
  m.seat_block = SeatBlock(m);
  return true;
}

// A practice (Options::practice): the match names one seat, the player's, and no relay; the race is raced alone.
std::optional<Match> ReadPractice(Match& m, const picojson::object& json, const std::string& path)
{
  if (!FromMatch(m, json, path))
    return std::nullopt;
  if (m.player_mask != 1u || Object(json, "relay") != nullptr)
  {
    Error("fzgx: {}: a practice names one seat, seat 0, a player's, and no relay: refused", path);
    return std::nullopt;
  }
  Put32(m.race.data() + RACE_FLOW, FLOW_GAME_OUT);
  Put32(m.race.data() + RACE_RUBBER_BAND_ON, 0);   // every driver at its own pace (0.4.0 on)
  m.seat = 0;
  m.delay = 0;
  m.other_sounds = OtherSoundsWord(TheOptions().other_sounds);
  if (!Bundle(m))
    return std::nullopt;
  Notice("fzgx: practice {}: alone, no relay, bundle {}", Text(json, "matchId"), m.bundle_commit);
  return m;
}

std::optional<Match> ReadMatch(const std::string& text, const std::string& path)
{
  picojson::value root;
  if (TooDeep(text) || !picojson::parse(root, text).empty() || !root.is<picojson::object>())
  {
    Error("fzgx: cannot read the match of {}", path);
    return std::nullopt;
  }
  const auto& json = root.get<picojson::object>();
  const picojson::object* relay = Object(json, "relay");
  Match m;
  if (TheOptions().practice)
    return ReadPractice(m, json, path);
  if (!relay || !FromMatch(m, json, path))
  {
    Error("fzgx: {}: no match or relay", path);
    return std::nullopt;
  }
  // a race raced from here on is the game's flow, with no rubber band, and its recording says so (replay.cpp
  // StartReplay)
  Put32(m.race.data() + RACE_FLOW, FLOW_GAME_OUT);
  Put32(m.race.data() + RACE_RUBBER_BAND_ON, 0);
  const std::optional<u32> seat = Whole(json, "seat", 0, 0, MAX_SEATS - 1);
  const std::optional<u32> port = Whole(*relay, "port", 0, 0, 65535);
  if (!seat || !port)
  {
    Error("fzgx: {}: the seat or the relay's port is not a number in its range: refused", path);
    return std::nullopt;
  }
  m.seat = static_cast<int>(*seat);
  m.delay = TheOptions().delay;
  m.relay.host = Text(*relay, "host");
  if (m.relay.host.empty())
    m.relay.host = TheOptions().relay_host;
  m.relay.port = static_cast<u16>(*port);
  // A match that names no relay this game can join (no host, no port, a token that is none) started the race with
  // no session: the game raced alone while the others waited for it (finding L7).
  if (!TryParse(Text(json, "token"), &m.relay.token, 16) || m.relay.token == 0 || m.relay.host.empty() ||
      m.relay.port == 0)
  {
    Error("fzgx: {}: the match names no relay to join (its host, its port or its token is missing or is none): "
          "refused", path);
    return std::nullopt;
  }
  m.other_sounds = OtherSoundsWord(TheOptions().other_sounds);
  if (!Bundle(m))
    return std::nullopt;
  Notice("fzgx: match {}: seat {}, relay {}:{}, bundle {}", Text(json, "matchId"), m.seat,
                 m.relay.host, m.relay.port, m.bundle_commit);
  return m;
}
}  // namespace fzgx::boot
