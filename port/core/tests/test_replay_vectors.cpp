// The replay file's shared samples (protocol/tests/vectors/replays: a real recording of every version 2 to 7, cut to
// a few hundred steps, each as hex text, NAME.fzrp.hex), read by the generated reader (protocol/generated/replay_file.g.h): every part agrees with the
// line replays.tsv holds for it (written by the hand-written reader the generated one replaced; the launcher's test,
// protocol/tests/dotnet-replay, holds its reader to the same file), each file's version is the one its race's
// features make (replay::VersionOf), and the generated writer's parts give the file's bytes back, the results' JSON
// written again from its entries (Recorder's ResultsJson) included.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "harness.h"
#include "parts.h"
#include "picojson.h"
#include "fzgx/replay_info.h"
#include "replay_file.g.h"

namespace
{
std::string Fnv(const u8* data, size_t size)
{
  u64 h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < size; ++i)
    h = (h ^ data[i]) * 0x100000001b3ull;
  char out[17];
  std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
  return out;
}

std::string Hex(const u8* data, size_t size)
{
  std::string out;
  char two[3];
  for (size_t i = 0; i < size; ++i)
  {
    std::snprintf(two, sizeof two, "%02x", data[i]);
    out += two;
  }
  return out;
}

// The results' entries from their JSON, as the writer holds them.
std::vector<replay::Result> Entries(const std::string& json)
{
  picojson::value root;
  std::vector<replay::Result> out;
  if (!picojson::parse(root, json).empty())
    return out;
  for (const picojson::value& e : root.get("results").get<picojson::array>())
  {
    const auto n = [&](const char* key) { return e.get(key).get<double>(); };
    replay::Result r;
    r.seat = static_cast<u32>(n(replay::result::SEAT));
    r.rank = static_cast<u32>(n(replay::result::RANK));
    r.finished = e.get(replay::result::FINISHED).get<bool>();
    r.out = e.get(replay::result::OUT).get<bool>();
    r.laps = static_cast<s32>(n(replay::result::LAPS));
    if (e.get(replay::result::TIME_MS).is<double>())
      r.time_ms = static_cast<u32>(n(replay::result::TIME_MS));
    r.best_lap_ms = static_cast<u32>(n(replay::result::BEST_LAP_MS));
    out.push_back(r);
  }
  return out;
}

// The features a race's header names, as the game's readers judge them (files_replay.cpp ReadReplay).
replay::Features FeaturesOf(const picojson::object& header)
{
  const picojson::object& setup = header.at("match").get("setup").get<picojson::object>();
  replay::Features f;
  f.stats = setup.contains("stats") && !setup.at("stats").is<picojson::null>();
  for (const picojson::value& seat : setup.at("seats").get<picojson::array>())
  {
    f.custom = f.custom || seat.contains("custom");
    f.seat_packs = f.seat_packs || seat.contains("pack");
  }
  f.seat_packs = f.seat_packs || setup.contains("packs");
  if (setup.contains("rules") && setup.at("rules").contains("mineRegenMs"))
    f.mines = setup.at("rules").get("mineRegenMs").get<double>() * 60 / 1000 >= 1;
  const bool band = !header.contains("rubberBand") || header.at("rubberBand").get<bool>();
  f.grid = setup.contains("grid") || !band;
  return f;
}

std::string Describe(const std::string& name, const std::vector<u8>& d)
{
  const replay::Parts p = replay::Parse(d.data(), d.size());
  std::string line = name + "\t" + std::to_string(p.version) + "\t" + std::to_string(p.header_length) + "\t" +
                     Fnv(d.data() + p.HeaderAt(), p.header_length) + "\t" + std::to_string(p.mask) + "\t" +
                     std::to_string(p.seat);
  if (p.lineup)
  {
    const replay::Lineup l = replay::DecodeLineup(d.data() + p.lineup_at, p.lineup_length);
    std::string settings;
    for (s16 s : l.settings)
      settings += (settings.empty() ? "" : ",") + std::to_string(s);
    line += "\t" + std::to_string(p.lineup_length) + "\t" + Hex(l.machines.data(), 30) + "\t" + Hex(l.ranks.data(), 30) +
            "\t" + settings + "\t" + std::to_string(l.control_mode) + "\t" + std::to_string(l.rel_d70);
  }
  else
  {
    line += "\t-\t-\t-\t-\t-\t-";
  }
  std::vector<u8> rows, sums;
  for (size_t step = 0; step < p.Steps(); ++step)
  {
    const u8* at = d.data() + p.StepAt(step);
    rows.insert(rows.end(), at, at + p.Row());
    sums.insert(sums.end(), at + p.Row(), at + p.StepSize());
  }
  line += "\t" + std::to_string(p.Steps()) + "\t" + (p.Whole() ? "1" : "0") + "\t" + Fnv(rows.data(), rows.size()) +
          "\t" + Fnv(sums.data(), sums.size());
  if (!p.results)
    return line + "\t-\t-\t-";
  std::string entries;
  for (const replay::Result& r : Entries(std::string(d.begin() + p.results_at, d.begin() + p.results_at + p.results_length)))
    entries += (entries.empty() ? "" : " ") + std::format("{}:{}:{}:{}:{}:{}:{}", r.seat, r.rank, int{r.finished}, int{r.out},
                                                          r.laps, r.time_ms ? std::to_string(*r.time_ms) : "-", r.best_lap_ms);
  return line + "\t" + std::to_string(p.results_length) + "\t" + Fnv(d.data() + p.results_at, p.results_length) + "\t" +
         entries;
}

// The file written again by the generated writer's parts, from what the generated reader read of it.
std::vector<u8> WrittenAgain(const std::vector<u8>& d)
{
  const replay::Parts p = replay::Parse(d.data(), d.size());
  std::vector<u8> out = replay::EncodeStartAs(
      p.version, std::string_view(reinterpret_cast<const char*>(d.data()) + p.HeaderAt(), p.header_length), p.mask, p.seat);
  if (p.lineup)
  {
    const std::vector<u8> lineup = replay::EncodeLineup(replay::DecodeLineup(d.data() + p.lineup_at, p.lineup_length));
    out.insert(out.end(), lineup.begin(), lineup.end());
  }
  for (size_t step = 0; step < p.Steps(); ++step)
  {
    const u8* at = d.data() + p.StepAt(step);
    replay::EncodeStep(out, at, p.Row(), (u32{at[p.Row()]} << 24) | (u32{at[p.Row() + 1]} << 16) |
                                             (u32{at[p.Row() + 2]} << 8) | at[p.Row() + 3]);
  }
  if (p.results)
  {
    const std::string json = replay::ResultsJson(
        Entries(std::string(d.begin() + p.results_at, d.begin() + p.results_at + p.results_length)));
    const std::vector<u8> results = replay::EncodeResults(json);
    out.insert(out.end(), results.begin(), results.end());
  }
  return out;
}

std::vector<u8> Bytes(const std::filesystem::path& path)
{
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A sample: NAME.hex beside replays.tsv holds the file's bytes as hex (text: the repository takes no binary file).
std::vector<u8> Sample(const std::filesystem::path& folder, const std::string& name)
{
  std::ifstream f(folder / (name + ".hex"));
  std::vector<u8> out;
  std::string hex;
  for (std::string line; std::getline(f, line);)
    hex += line;
  for (size_t i = 0; i + 1 < hex.size(); i += 2)
    out.push_back(static_cast<u8>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  return out;
}
}  // namespace

// Every sample: the vectors' line, its version as its features make it, its bytes written again.
static void TestTheReplaySamplesOfEveryVersion()
{
  const std::filesystem::path folder = ProtocolFile("protocol/tests/vectors/replays");
  std::ifstream tsv(folder / "replays.tsv");
  CHECK(tsv.good());
  std::string line;
  std::vector<u16> versions;
  while (std::getline(tsv, line))
  {
    if (line.empty() || line[0] == '#')
      continue;
    const std::string name = line.substr(0, line.find('\t'));
    const std::vector<u8> d = Sample(folder, name);
    const std::string got = Describe(name, d);
    if (got != line)
      std::printf("  %s:\n    read    %s\n    vectors %s\n", name.c_str(), got.c_str(), line.c_str());
    CHECK(got == line);
    const replay::Parts p = replay::Parse(d.data(), d.size());
    picojson::value header;
    CHECK(picojson::parse(header, std::string(d.begin() + p.HeaderAt(), d.begin() + p.FramesAt())).empty());
    CHECK(replay::VersionOf(FeaturesOf(header.get<picojson::object>())) == p.version);
    CHECK(WrittenAgain(d) == d);
    versions.push_back(p.version);
  }
  for (u16 v : {2, 3, 4, 5, 6, 7})
    CHECK(std::count(versions.begin(), versions.end(), v) >= 1);
  // the writer writes version 7 alone
  CHECK(replay::EncodeStart("{}", 1, 0)[5] == replay::VERSION_GRID && replay::VERSION_WRITTEN == replay::VERSION_GRID);
}

// FZGX_REPLAY_ROUND_TRIP=FILE: every replay FILE names (one path a line) written again by the generated writer's
// parts, byte for byte (the proof, on real recordings, that the generated writer writes what the old one did).
static void TestRealRecordingsWrittenAgain()
{
  const char* list = std::getenv("FZGX_REPLAY_ROUND_TRIP");
  if (list == nullptr)
    return;
  std::ifstream files(list);
  u32 same = 0, differ = 0;
  for (std::string path; std::getline(files, path);)
  {
    const std::vector<u8> d = Bytes(path);
    const replay::Parts p = replay::Parse(d.data(), d.size());
    if (!p.marked || !p.frames || !p.Whole())
      continue;
    if (WrittenAgain(d) == d)
      ++same;
    else
      std::printf("  written again otherwise: %s\n", path.c_str()), ++differ;
  }
  std::printf("  real recordings written again: %u the same, %u otherwise\n", same, differ);
  CHECK(differ == 0 && same > 0);
}

// The runner's --replay-info: what the file holds, as one JSON object, for every sample.
static void TestTheReplayInfoOfEverySample()
{
  const std::filesystem::path folder = ProtocolFile("protocol/tests/vectors/replays");
  const std::filesystem::path temp = std::filesystem::temp_directory_path() / "fzgx-replay-info";
  std::filesystem::create_directories(temp);
  for (const char* name : {"v2.fzrp", "v2-no-steps.fzrp", "v6.fzrp", "v7.fzrp"})
  {
    const std::vector<u8> d = Sample(folder, name);
    std::ofstream(temp / name, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    std::string json, error;
    CHECK(ReplayInfo((temp / name).string(), &json, &error));
    picojson::value info;
    CHECK(picojson::parse(info, json).empty() && info.is<picojson::object>());
    const replay::Parts p = replay::Parse(d.data(), d.size());
    CHECK(info.get("version").get<double>() == p.version && info.get("seat").get<double>() == p.seat);
    CHECK(info.get("steps").get<double>() == static_cast<double>(p.Steps()) && info.get("whole").get<bool>());
    CHECK(info.get("seats").get<picojson::array>().size() == p.Seats() && info.get("lineup").get<bool>() == p.lineup);
    CHECK(info.get("header").get("match").is<picojson::object>());
    CHECK(info.get("results").is<picojson::null>() != p.results);
  }
  std::string json, error;
  CHECK(!ReplayInfo((folder / "replays.tsv").string(), &json, &error) && error.find("no replay") != std::string::npos);
  std::filesystem::remove_all(temp);
}

// The three above, run by TestARecordingRoundTrip (test_recording.cpp).
void TestTheReplayFilesSamples()
{
  BEGIN_TEST();
  TestTheReplaySamplesOfEveryVersion();
  TestRealRecordingsWrittenAgain();
  TestTheReplayInfoOfEverySample();
}
