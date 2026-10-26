// Tests of the start grid as the game reads it (files_match.cpp, files_replay.cpp, replay.cpp): "grid" of a match
// start's set-up or a replay's header (the coordinator's RaceSetup.Grid: the seats in starting order, pole first)
// into the race block (RACE_GRID_ON, RACE_GRID), which the image's site hands the game's grid sort (sites_grid.py,
// port/image/tests/test_grid.py); and a recording of a race with it, version 7.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stat_packs.h"

using namespace stat_packs;

namespace
{
// A replay of two seats whose set-up begins with `setup`, its version byte set to `version`.
void WriteAReplay(const std::string& path, const std::string& setup, char version, const std::string& header = "")
{
  WriteReplay(path, {1, 0}, setup, header);
  std::string file;
  CHECK(ReadFileToString(path, file));
  file[5] = version;
  WriteText(path, file);
}

std::string Grid(const std::string& list)
{
  return "\"grid\": " + list + ", ";
}

// A grid of every seat once is in the race block, slot by slot, with its word 1; none (absent or null) leaves both 0,
// the game's own sort, as every recording made before it. Anything else is refused: too few or too many entries, a
// seat twice, one the race does not have, a fraction, text, no list.
void TheStartGridInTheRaceBlock()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  const auto grid = [&](const std::string& setup) {
    PackOf(host, ARace(dir, setup));
    const bool active = Active();
    if (active)
      FrameTick();
    const u32 on = active ? Data().Read(layout::RACE_GRID_ON_AT, 4) : 99;
    const u32 slots = active ? Data().Read(layout::RACE_GRID_AT, 2) : 0xFFFF;
    Shutdown();
    return std::tuple{active, on, slots};
  };
  CHECK(grid(Grid("[1, 0]")) == std::tuple(true, 1u, 0x0100u));
  CHECK(grid(Grid("[0, 1]")) == std::tuple(true, 1u, 0x0001u));
  CHECK(grid("") == std::tuple(true, 0u, 0u));
  CHECK(grid(Grid("null")) == std::tuple(true, 0u, 0u));
  for (const char* bad : {"[1]", "[1, 0, 2]", "[1, 1]", "[0, 2]", "[0.5, 1]", "[\"1\", 0]", "[-1, 0]", "{}", "1", "[]"})
  {
    CHECK(!std::get<0>(grid(Grid(bad))));
    CHECK(Says(host.log, "the start grid is not every seat of the race's 2 once: refused"));
  }
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}

// A recording of a race on a grid is version 7, the grid in its header's match as it came, whatever else the race
// holds (a stat pack, mines that come back); without one a race keeps its version. A file whose version and grid
// disagree is not played: a version 7 file without a grid, or another version's with one.
void TheStartGridInARecording()
{
  BEGIN_TEST();
  TestHost host;
  const std::string dir = Temp();
  const auto recorded = [&](const std::string& setup) {
    PackOf(host, ARace(dir, setup));
    CHECK(boot::StartReplay(dir + "/rec.fzrp"));
    std::string file;
    CHECK(ReadFileToString(dir + "/rec.fzrp", file));
    Shutdown();
    return file;
  };
  const std::string mines = "\"rules\": {\"cpuLevel\": 3, \"mineRegenMs\": 200}, ";
  const std::string with = recorded(Grid("[1, 0]")), both = recorded(Grid("[1, 0]") + mines + Stats(ThePack())),
                    plain = recorded(""), regen = recorded(mines);
  CHECK(with.compare(0, 6, std::string("FZRP\0\7", 6)) == 0 && both.compare(0, 6, std::string("FZRP\0\7", 6)) == 0);
  // (and without a grid: a race from a match has no rubber band since 0.4.0, version 7 too)
  CHECK(plain.compare(0, 6, std::string("FZRP\0\7", 6)) == 0 && regen.compare(0, 6, std::string("FZRP\0\7", 6)) == 0);
  CHECK(Says(with, "\"grid\":[1,0]") && Says(both, "\"mineRegenMs\":200"));
  // read back
  WriteABundle(dir);
  Options options;
  options.bundle_dir = dir;
  options.play_file = dir + "/race.fzrp";
  const auto played = [&](const std::string& setup, char version, const std::string& header = "") {
    WriteAReplay(options.play_file, setup, version, header);
    host.log.clear();
    Init(host, options);
    const std::string said = Active() ? "played" : host.log;
    if (Active())
      FrameTick();
    const u32 on = Active() ? Data().Read(layout::RACE_GRID_ON_AT, 4) : 99;
    net::Recording steps;
    CHECK(net::ReadRecording(options.play_file, &steps));
    Shutdown();
    return said + (said == "played" ? " " + std::to_string(on) : "");
  };
  CHECK(played(Grid("[1, 0]"), 7) == "played 1" && played("", 2) == "played 0");
  CHECK(played(Grid("[1, 0]") + mines, 7) == "played 1");
  CHECK(Says(played("", 7), "a version 7 replay names no start grid and has the rubber band, and version 7 is a race "
                            "of 0.4.0's set-up: refused"));
  // no rubber band (every race and practice from 0.4.0) is version 7 too, with a grid or without one
  const std::string no_band = ", \"rubberBand\": false";
  CHECK(played("", 7, no_band) == "played 0" && played(Grid("[1, 0]"), 7, no_band) == "played 1");
  for (const char version : {2, 3, 4, 5, 6})
  {
    CHECK(Says(played(Grid("[0, 1]"), version), "holds a start grid or no rubber band, which only version 7 does: refused"));
    CHECK(Says(played("", version, no_band), "holds a start grid or no rubber band, which only version 7 does: refused"));
  }
  Init(host, Options{});
  std::filesystem::remove_all(dir);
}
}  // namespace

// Both of the above, in the suite's order.
void TestTheStartGrid()
{
  TheStartGridInTheRaceBlock();
  TheStartGridInARecording();
}
