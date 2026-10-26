// Tests of a custom machine in a race on a stat pack (stat_pack.cpp CustomSumRefusal): the seat races the sum of its
// three parts' blocks as the pack left them in the table, and the go holds that sum to a machine's rows. The car
// data is made up here (stat_packs.h); a recording of version 4 holds the seat's custom machine and the pack.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stat_packs.h"

using namespace stat_packs;

namespace
{
constexpr u32 BODY = 3, COCKPIT = 43, BOOSTER = 58;

// A recording of two seats on the made-up pack, seat 1 on a custom machine (BODY, COCKPIT, BOOSTER): version 4,
// the lineup naming the custom id for seat 1.
void ARecording(const std::string& path)
{
  WriteReplay(path, {0, 1}, Stats(ThePack(), "Go Pack"));
  std::string file;
  CHECK(ReadFileToString(path, file));
  const std::string seat1 = "\"name\": \"P1\"}";
  const size_t at = file.find(seat1);
  CHECK(at != std::string::npos);
  const std::string custom = std::format("\"name\": \"P1\", \"custom\": {{\"body\": {}, \"cockpit\": {}, \"booster\": {}, "
                                         "\"pilot\": 6}}}}", BODY, COCKPIT, BOOSTER);
  file.replace(at, seat1.size(), custom);
  const u32 length = (u32(u8(file[6])) << 24 | u32(u8(file[7])) << 16 | u32(u8(file[8])) << 8 | u8(file[9])) +
                     static_cast<u32>(custom.size() - seat1.size());
  for (int i = 0; i < 4; ++i)
    file[6 + i] = static_cast<char>(length >> (24 - 8 * i));
  file[5] = 4;
  file[file.find("FZRL") + 8 + 1] = static_cast<char>(boot::CUSTOM_MACHINE);
  WriteText(path, file);
}

// The three parts' words: every word a pack may hold 0 but the weight (each part `weight`) and the booster's boost.
void TheParts(Guest& g, float body_weight)
{
  for (const u32 part : {BODY, COCKPIT, BOOSTER})
  {
    for (u32 at = 0x04; at <= 0x44; at += 4)
      g.Write32(TABLE + stats::BlockAt(stats::MACHINES + part) + at, 0);
    g.Write32(TABLE + stats::BlockAt(stats::MACHINES + part) + 4, Bits(part == BODY ? body_weight : 400.0f));
  }
  g.Write32(TABLE + stats::BlockAt(stats::MACHINES + BOOSTER) + 0x34, Bits(10.0f));
}
}  // namespace

void TestACustomMachineOnAStatPack()
{
  BEGIN_TEST();
  TestHost host;
  Guest g(host, Data());
  const std::string dir = Temp();
  // a body whose weight the pack left at 400: the sum 1,200 is a machine's; at -800, the sum 0 is not
  for (const float body_weight : {400.0f, -800.0f})
  {
    WriteABundle(dir);
    ARecording(dir + "/race.fzrp");
    Options options;
    options.bundle_dir = dir;
    options.play_file = dir + "/race.fzrp";
    host.log.clear();
    host.notices.clear();
    Init(host, options);
    LinkMainRel(g);
    LoadCarData(g);
    FrameTick();
    CHECK(Active() && host.log.empty() && HoldsOnly(g, ThePack()));
    TheParts(g, body_weight);
    MakeRacers(g, {6, 12});
    host.stops = 0;
    g.Write32(layout::READY_AT, 1);
    NetSync();
    const bool runs = body_weight > 0;
    CHECK((g.Read32(layout::RUNNING_AT) == 1) == runs && host.stops == (runs ? 0 : 1));
    if (!runs)
      CHECK(Says(host.log, std::format("seat 1's custom machine ({}, {}, {}) sums its Weight to a value the game is not "
                                       "safe with", BODY, COCKPIT, BOOSTER)));
    Shutdown();
    g.Write32(layout::READY_AT, 0);
    g.Write32(layout::RUNNING_AT, 0);
    g.Write32(layout::GO_AT, 0);
    g.Write32(MODULE + game::MACHINE_COUNT, 0);
  }
  std::filesystem::remove_all(dir);
}

// The words a custom machine takes from one part as they are (the body's grips and hover and wall corners, the
// cockpit's camera values) are held to a machine's row at the go: a
// part's row holds them by their size alone, which let a pack give a custom machine a hover corner's Z of 0 (a
// divisor) or a grip beyond a machine's, in a replay file or a server's match start (security audit 030, G1).
void TestACustomMachinesOwnPartWordsOnAStatPack()
{
  BEGIN_TEST();
  TestHost host;
  Guest g(host, Data());
  const std::string dir = Temp();
  struct Case
  {
    u32 part, at, bits;
    const char* said;  // empty: the race runs
  };
  const Case cases[] = {
      {BODY, 0x5C, 0, "takes its Hover0Z from its body (3)"},
      {BODY, 0x5C, Bits(0.001f), "takes its Hover0Z from its body (3)"},
      {BODY, 0xB0, Bits(-0.001f), ""},  // a wall corner's Z: no floor
      {BODY, 0x10, Bits(-46.0f), "takes its Grip1 from its body (3)"},
      {BODY, 0x10, Bits(-1.0f), ""},  // a negative grip: a machine may hold one
      {BODY, 0x14, Bits(4.0f), "takes its Grip3 from its body (3)"},
      {COCKPIT, 0x4C, Bits(-20.0f), "takes its CameraReorienting from its cockpit (43)"},
      {BOOSTER, 0x5C, 0, ""},  // the booster's corners are not raced
      {BODY, 0x5C, Bits(-1.5f), ""},
  };
  for (const Case& c : cases)
  {
    WriteABundle(dir);
    ARecording(dir + "/race.fzrp");
    Options options;
    options.bundle_dir = dir;
    options.play_file = dir + "/race.fzrp";
    host.log.clear();
    host.notices.clear();
    Init(host, options);
    LinkMainRel(g);
    LoadCarData(g);
    FrameTick();
    CHECK(Active() && host.log.empty() && HoldsOnly(g, ThePack()));
    TheParts(g, 400.0f);
    g.Write32(TABLE + stats::BlockAt(stats::MACHINES + c.part) + c.at, c.bits);
    MakeRacers(g, {6, 12});
    host.stops = 0;
    g.Write32(layout::READY_AT, 1);
    NetSync();
    const bool runs = std::string(c.said).empty();
    CHECK((g.Read32(layout::RUNNING_AT) == 1) == runs && host.stops == (runs ? 0 : 1));
    if (!runs)
      CHECK(Says(host.log, std::format("seat 1's custom machine ({}, {}, {}) {}", BODY, COCKPIT, BOOSTER, c.said)));
    Shutdown();
    g.Write32(layout::READY_AT, 0);
    g.Write32(layout::RUNNING_AT, 0);
    g.Write32(layout::GO_AT, 0);
    g.Write32(MODULE + game::MACHINE_COUNT, 0);
  }
  std::filesystem::remove_all(dir);
}
