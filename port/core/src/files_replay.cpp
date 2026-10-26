// A replay file's two readers: boot::ReadReplay gives the race a playback is set up with (the header's match, the
// lineup), net::ReadRecording the steps it runs and compares. Both take the file's parts from the generated reader
// (protocol/replay.toml, generated/replay_file.g.h) and judge them; what each refuses: protocol/formats.md section 1.
// Lifted from the Dolphin fork (dolphin-fzgx Core/FzgxBoot.cpp and Core/FzgxNet.cpp at 5bdc7c640e).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include "fzgx/replay_info.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "replay.h"
#include "replay_file.g.h"

namespace fzgx
{
namespace
{
u32 Get32(const u8* p)
{
  return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
}  // namespace

namespace boot
{
namespace
{
// ReadReplay's steps, in its order. Each says what it refuses.

// A replay file's bytes, its parts (the generated reader's, protocol/replay.toml), and its header read.
struct ReplayParts
{
  std::vector<u8> data;
  replay::Parts parts;
  picojson::value root;  // the header
};

std::optional<ReplayParts> ReadParts(const std::string& path)
{
  File f(path, "rb");
  const size_t size = f.GetSize();
  std::vector<u8> data(size <= MAX_REPLAY_BYTES ? size : 0);
  const bool read = f.IsOpen() && !data.empty() && f.ReadBytes(data.data(), data.size());
  const replay::Parts p = replay::Parse(data.data(), read ? data.size() : 0);
  if (!p.marked || !replay::Played(p.version))
  {
    Error("fzgx: {} is not a version 2, 3, 4, 5, 6 or 7 replay file", path);
    return std::nullopt;
  }
  picojson::value root;
  const std::string header = p.header ? std::string(data.begin() + p.HeaderAt(), data.begin() + p.FramesAt()) : "";
  if (!p.frames_marked || !p.lineup || data.size() - p.lineup_at < replay::LINEUP_LEAST || TooDeep(header) ||
      TooLarge(header) || !picojson::parse(root, header).empty() || !root.is<picojson::object>())
  {
    Error("fzgx: {}: no header or lineup", path);
    return std::nullopt;
  }
  return ReplayParts{std::move(data), p, std::move(root)};
}

// The frame preamble names seats of the race, and the file ends on a whole step.
bool StepsAreWhole(const replay::Parts& p, const std::string& path)
{
  // A file that ends early: every file the game closed ends on a whole step (a race cut short too), with the
  // results block behind the steps when the race was over. Half a step, or no step, is a broken copy.
  // the player seats and the recorder's seat are seats of the race: the session writes each seat's inputs by its
  // number and points the game's cameras at the recorder's
  if ((p.mask >> MAX_SEATS) != 0 || p.seat >= MAX_SEATS)
  {
    Error("fzgx: {} names a seat the game does not have (player seats {:08x}, recorded from seat {}): refused", path,
          p.mask, p.seat);
    return false;
  }
  if (p.Seats() == 0 || p.Steps() == 0 || !p.Whole())
  {
    Error("fzgx: {} is incomplete: it ends before the recording does ({} bytes of steps, {} a step)", path,
          p.StepBytes(), p.StepSize());
    return false;
  }
  if (p.Steps() > MAX_REPLAY_STEPS)
  {
    Error("fzgx: {} holds {} steps, and a replay is at most {} (six hours): refused", path, p.Steps(),
          MAX_REPLAY_STEPS);
    return false;
  }
  return true;
}

// The race of the header's match, with the recorder's seat, the input delay and the player seats.
std::optional<Match> MatchOfHeader(const picojson::object& header, const std::string& path)
{
  const picojson::object* json = Object(header, replay::header::MATCH);
  Match m;
  const picojson::object* setup = json ? Object(*json, "setup") : nullptr;
  const bool before_rules = setup && Object(*setup, "rules") == nullptr;
  if (!json || !FromMatch(m, *json, path, before_rules ? 100 : 50))
    return std::nullopt;
  namespace h = replay::header;
  static_assert(h::SEAT_HIGH == MAX_SEATS - 1 && h::DELAY_HIGH == MAX_DELAY && h::FLOW_HIGH == FLOW_GAME_OUT &&
                h::MASK_HIGH == (1u << MAX_SEATS) - 1);
  const std::optional<u32> seat = Whole(header, h::SEAT, h::SEAT_DEFAULT, h::SEAT_LOW, h::SEAT_HIGH);
  const std::optional<u32> delay = Whole(header, h::DELAY, h::DELAY_DEFAULT, h::DELAY_LOW, h::DELAY_HIGH);
  const std::optional<u32> mask = Whole(header, h::MASK, m.player_mask, h::MASK_LOW, h::MASK_HIGH);
  // the race's flow (files.h RACE_FLOW): a recording made before it names none and plays with the masks of before
  const std::optional<u32> flow = Whole(header, h::FLOW, h::FLOW_DEFAULT, h::FLOW_LOW, h::FLOW_HIGH);
  // the rubber band (files.h RACE_RUBBER_BAND_ON): a recording made before 0.4.0 names none and raced with it
  const auto band = header.find(h::RUBBER_BAND);
  const bool band_said = band != header.end();
  if (!seat || !delay || !mask || !flow || (band_said && !band->second.is<bool>()))
  {
    Error("fzgx: {}: the header's seat, input delay, player seats, flow or rubber band is not a number in its range "
          "(the band: true or false): refused", path);
    return std::nullopt;
  }
  Put32(m.race.data() + RACE_FLOW, *flow);
  Put32(m.race.data() + RACE_RUBBER_BAND_ON, band_said && !band->second.get<bool>() ? 0 : 1);
  m.seat = static_cast<int>(*seat);
  m.delay = *delay;
  m.player_mask = *mask;
  m.other_sounds = OtherSoundsWord(TheOptions().other_sounds);
  return m;
}

// The recorded lineup's machines into the race block (port 0's and every seat's), as the race's boot reads them.
bool LineupIntoRace(Match& m, const std::vector<u8>& data, const replay::Parts& p, const std::string& path)
{
  const replay::Lineup lineup = replay::DecodeLineup(data.data() + p.lineup_at, data.size() - p.lineup_at);
  const u8* l = lineup.machines.data();
  const u8 racers = static_cast<u8>(std::clamp<u32>(Get32(m.race.data() + 12) + 1, 1, 30));
  // Every one of the 30 slots, not the race's own racers alone: all 30 bytes go into the race block below, and an id
  // above 40 there is a custom machine's, which no race of a match or a replay has (a stat pack's rule for the
  // custom parts rests on that: stat_ranges.h). A recording of a race of fewer than 30 holds 0 in the slots after
  // its racers (the recorder copies the game's own 30 bytes, which the match's reader filled), so no recording the
  // game or the fork wrote is refused here; the slots are not written as 0, which would change what one writes.
  for (u8 slot = 0; slot < 30; ++slot)
  {
    if (l[slot] > MAX_MACHINE && !(l[slot] == CUSTOM_MACHINE && IsCustom(m.race, slot)))
    {
      Error("fzgx: {}: the lineup's {} {} has a machine the game does not have ({}): refused", path,
            slot < racers ? "racer" : "slot without a racer,", slot, l[slot]);
      return false;
    }
  }
  u8* r = m.race.data();
  for (int i = 0; i < 30; ++i)
    r[32 + i] = l[i];
  Put32(r + 8, l[0]);
  return true;
}
}  // namespace

// A replay file (protocol/replay.toml, read by the generated reader; formats.md section 1): the race of its
// header's match, with the lineup's machines (of which a playback as the race reads the machines).
std::optional<Match> ReadReplay(const std::string& path)
{
  const std::optional<ReplayParts> parts = ReadParts(path);
  if (!parts)
    return std::nullopt;
  const auto& [data, p, root] = *parts;
  const auto& header = root.get<picojson::object>();
  // A replay of another game than this one (a header that names none is the fork's, which ran only this game).
  if (const std::string game = Text(header, replay::header::GAME); !game.empty() && game != GAME_ID)
  {
    Error("fzgx: {} is a replay of another version of the game ({}); this one is {}", path, game, GAME_ID);
    return std::nullopt;
  }
  if (!StepsAreWhole(p, path))
    return std::nullopt;
  std::optional<Match> match = MatchOfHeader(header, path);
  if (!match || !LineupIntoRace(*match, data, p, path))
    return std::nullopt;
  Match& m = *match;
  // A race whose mines come back (the header's match names the rule, files_match.cpp) is version 5 and no other is,
  // with a stat pack or a custom machine or neither: a program from before the rule refuses it. A recording made
  // before the rule names none, and plays with the mines gone, as it was raced.
  const u32 version = p.version;
  // A race with a pack a seat (setup.packs, a seat's pack) is version 6 and no other is, whatever else it holds: a
  // program from before packs a seat would race every seat on the race's pack. Version 6 says nothing of the rest.
  // A race of 0.4.0's set-up (OfVersion7: on the coordinator's start grid, setup.grid, or with no rubber band, the
  // header's "rubberBand": false) is version 7 and no other is, whatever else it holds: a program from before would
  // start it from the game's own grid or race its CPU drivers with the band. Version 7 says nothing of the rest.
  const bool version_7 = version == replay::VERSION_GRID;
  if (version_7 != OfVersion7(m))
  {
    Error("fzgx: {}: a version {} replay {}: refused", path, version,
          version_7 ? "names no start grid and has the rubber band, and version 7 is a race of 0.4.0's set-up"
                    : "holds a start grid or no rubber band, which only version 7 does");
    return std::nullopt;
  }
  const bool version_6 = version == replay::VERSION_SEATS;
  if (!version_7 && version_6 != HasSeatPacks(m))
  {
    Error("fzgx: {}: a version {} replay {}: refused", path, version,
          version_6 ? "names no pack a seat, and version 6 is a race with one" : "holds a pack a seat, which only version 6 does");
    return std::nullopt;
  }
  const bool version_5 = version == replay::VERSION_MINES;
  const bool version_4 = version == replay::VERSION_CUSTOM;
  if (!version_7 && !version_6 && version_5 != (Get32(m.race.data() + RACE_MINE_REGEN) != 0))
  {
    Error("fzgx: {}: a version {} replay {}: refused", path, version,
          version_5 ? "names no mine rule, and version 5 is a race with one" : "holds the mine rule, which only version 5 does");
    return std::nullopt;
  }
  // A race with a stat pack is version 3 and no other is: a program from before stat packs would play a version 2
  // file that holds one on the disc's own car data, so no program writes such a file and this one plays none.
  // (A race with a custom machine is version 4, with a pack or without: below.)
  if (const bool version_3 = version == replay::VERSION_STATS;
      !version_7 && !version_6 && !version_5 && !version_4 && version_3 != m.stats.has_value())
  {
    Error("fzgx: {}: a version {} replay {}: refused", path, version_3 ? 3 : 2,
          version_3 ? "names no stat pack, and version 3 is a race with one" : "holds a stat pack, which only version 3 does");
    return std::nullopt;
  }
  // A race with a custom machine is version 4 and no other is: a program from before custom machines refuses it
  // (a stat pack in it too is read as in version 3)
  bool custom = false;
  for (u32 seat = 0; seat < MAX_SEATS; ++seat)
    custom = custom || IsCustom(m.race, seat);
  if (!version_7 && !version_6 && !version_5 && version_4 != custom)
  {
    Error("fzgx: {}: a version {} replay {}: refused", path, version,
          version_4 ? "names no custom machine, and version 4 is a race with one" : "holds a custom machine, which only version 4 does");
    return std::nullopt;
  }
  // as the race: the race's own boot, which the recording replays exactly
  if (!Bundle(m))
    return std::nullopt;
  Notice("fzgx: replay {}: seen from seat {}, bundle {}", path, m.seat, m.bundle_commit);
  return match;
}
}  // namespace boot

namespace net
{
// A replay file (protocol/replay.toml): the player seats and the recorder's seat, then per step from go the player
// seats' records (in seat order) and the state checksum. The lineup and the results are skipped when present, and
// a part of a step at the end is not read.
bool ReadRecording(const std::string& path, Recording* out)
{
  File file(path, "rb");
  // (a recording of an hour of 30 seats is 40 MiB)
  const size_t size = file.GetSize();
  std::vector<u8> data(size <= replay::MAX_BYTES ? size : 0);
  const bool read = file.IsOpen() && !data.empty() && file.ReadBytes(data.data(), data.size());
  const replay::Parts p = replay::Parse(data.data(), read ? data.size() : 0);
  if (!p.marked || !boot::PlayedVersion(p.version))
  {
    Error("fzgx: {} is not a version 2, 3, 4, 5, 6 or 7 replay file", path);
    return false;
  }
  if (!p.frames || !p.frames_marked)
  {
    Error("fzgx: {}: no frames", path);
    return false;
  }
  // Seats of the race only (30): each seat's inputs are written by its number, and the recorder's seat is where the
  // game's cameras are pointed.
  if (p.mask == 0 || (p.mask >> replay::MAX_SEATS) != 0 || p.seat >= replay::MAX_SEATS)
  {
    Error("fzgx: {} names a seat the game does not have (player seats {:08x}, recorded from seat {})", path, p.mask,
          p.seat);
    return false;
  }
  out->seat = static_cast<int>(p.seat);
  out->index = -1;
  out->seats.clear();
  for (int i = 0; i < 32; ++i)
  {
    if (p.mask >> i & 1)
    {
      if (i == out->seat)
        out->index = static_cast<int>(out->seats.size());
      out->seats.push_back(i);
    }
  }
  out->rows.clear();
  out->checksums.clear();
  if (p.Steps() > boot::MAX_REPLAY_STEPS)
  {
    Error("fzgx: {} holds {} steps, and a replay is at most {} (six hours)", path, p.Steps(), boot::MAX_REPLAY_STEPS);
    return false;
  }
  for (size_t step = 0; step < p.Steps(); ++step)
  {
    const size_t at = p.StepAt(step);
    out->rows.emplace_back(data.begin() + at, data.begin() + at + p.Row());
    out->checksums.push_back(Get32(data.data() + at + p.Row()));
  }
  return true;
}
}  // namespace net
bool ReplayInfo(const std::string& path, std::string* json, std::string* error)
{
  File file(path, "rb");
  const size_t size = file.GetSize();
  std::vector<u8> data(size <= replay::MAX_BYTES ? size : 0);
  const bool read = file.IsOpen() && !data.empty() && file.ReadBytes(data.data(), data.size());
  const replay::Parts p = replay::Parse(data.data(), read ? data.size() : 0);
  if (!p.marked || !replay::Played(p.version) || !p.frames || !p.frames_marked)
  {
    *error = path + " is no replay of a version the game plays (2 to 7), or ends before its frames";
    return false;
  }
  const std::string header(data.begin() + p.HeaderAt(), data.begin() + p.FramesAt());
  picojson::value root, results;
  if (boot::TooDeep(header) || !picojson::parse(root, header).empty() || !root.is<picojson::object>())
  {
    *error = path + ": its header is no JSON object";
    return false;
  }
  const std::string tail = p.results ? std::string(data.begin() + p.results_at,
                                                   data.begin() + p.results_at + p.results_length) : "";
  const bool has_results = p.results && !boot::TooDeep(tail) && picojson::parse(results, tail).empty();
  std::string seats;
  for (u32 i = 0; i < 32; ++i)
    if (p.mask >> i & 1)
      seats += (seats.empty() ? "" : ",") + std::to_string(i);
  *json = std::format("{{\"version\":{},\"seat\":{},\"mask\":{},\"seats\":[{}],\"steps\":{},\"whole\":{},"
                      "\"lineup\":{},\"header\":{},\"results\":{}}}",
                      p.version, p.seat, p.mask, seats, p.Steps(), p.Whole(), p.lineup, root.serialize(),
                      has_results ? results.serialize() : "null");
  return true;
}
}  // namespace fzgx
