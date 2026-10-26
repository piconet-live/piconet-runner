// What the fuzzers of the core use (fuzz_replays.cpp, which says what they do, fuzz_relay.cpp, fuzz_launch.cpp): the host, the
// game as far as a session can tell, a bundle, and the counts a run prints.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unistd.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "fzgx/core.h"
#include "internal.h"
#include "picojson.h"
#include "replay.h"
#include "rollback.h"
#include "udp.h"

using namespace fzgx;

constexpr u32 MODULE = 0x801BA660;

// 32 bits a call, whatever the platform's generator gives.
struct Rng
{
  explicit Rng(u32 seed) : engine(seed) {}
  u32 operator()() { return static_cast<u32>(engine()); }
  std::mt19937 engine;
};
constexpr u32 PHASE = net::BSS + 0x3C2A;

class FuzzHost : public Host
{
public:
  FuzzHost() : ram(layout::RETAIL_RAM_SIZE, 0), scratch(0x4000, 0) {}
  u8* Ram() override { return ram.data(); }
  u32 RamSize() override { return static_cast<u32>(ram.size()); }
  u8* Scratch() override { return scratch.data(); }
  u32 ScratchSize() override { return static_cast<u32>(scratch.size()); }
  void RequestStop() override { ++stops; }
  float EmulationSpeed() override { return 1.0f; }
  float CpuClock() override { return 6.0f; }
  void SetPace(float, float) override {}
  void Log(LogLevel, const std::string& text) override { log_bytes += text.size(); }
  bool ReadGameFile(const std::string&, std::vector<u8>*) override { return false; }

  std::vector<u8> ram, scratch;
  int stops = 0;
  std::size_t log_bytes = 0;
};

// The game as far as the session can tell: 30 machines with their progress records, the phase word, and the frame
// routine (the hook before a frame's first step and before each further one).
struct Game
{
  Game(FuzzHost& h, Rng& r, u32 race_from, u32 race_to)
      : host(h), g(h, Data()), rng(r), from(race_from), to(race_to)
  {
    std::fill(host.ram.begin(), host.ram.end(), 0);
    first = (MODULE + net::DATA) & ~(net::PAGE - 1);
    machines = first + 0x400000;
    g.Write32(net::MODULE_POINTER, MODULE);
    g.Write32(MODULE + net::MACHINES, machines);
    g.Write8(MODULE + net::MACHINE_COUNT, 30);
    for (u32 i = 0; i < 30; ++i)
      g.Write32(machines + i * 0x620 + 0x49C, first + 0x500000 + i * 0x200);
    g.Write32(net::DOL_RNG, 0x2026);
  }
  void Step(u32 t)
  {
    for (u32 i = 0; i < 30; ++i)
    {
      u8 record[6] = {};
      g.CopyFrom(record, layout::INPUTS + ((t & 1023) * 30 + i) * 8, 6);
      u32 h = g.Read32(machines + i * 0x620 + 0x7C) ^ (t * 2654435761u);
      for (const u8 b : record)
        h = (h ^ b) * 0x01000193;
      g.Write32(machines + i * 0x620 + 0x7C, h);
      if (h % 97 == 0)
        g.Write32(machines + i * 0x620, g.Read32(machines + i * 0x620) | (h % 2 ? 0x10000 : 0x08000000));  // finished, out
    }
    if (rng() % 64 == 0)
      g.Write32(layout::SLOT + 4, rng() % 40);  // the stand-in seat moves (D-pad), now and then to a seat that is none
  }
  // One displayed frame; false once the session asked the game to stop, or gave up.
  bool Frame()
  {
    const u32 step = g.Read32(net::STEP);
    g.Write16(MODULE + PHASE, step < from ? 0x28 : step < to ? 0x29 : 0x2A);
    g.Write32(layout::CONTROL + 44, 1);
    const bool running = g.Read32(layout::CONTROL + 120) != 0;
    g.Write32(layout::CONTROL + 12, 0);
    NetSync();
    if (host.stops != 0 || g.Read32(layout::CONTROL + 132) != 0)
      return false;
    if (!running)
      return true;
    Step(g.Read32(net::STEP));
    g.Write32(net::STEP, g.Read32(net::STEP) + 1);
    const u32 extra = std::min<u32>(g.Read32(layout::CONTROL + 8), 64);
    most_extra = std::max(most_extra, g.Read32(layout::CONTROL + 8));
    g.Write32(layout::CONTROL + 12, 1);
    for (u32 k = 0; k < extra; ++k)
    {
      NetSync();
      Step(g.Read32(net::STEP));
      g.Write32(net::STEP, g.Read32(net::STEP) + 1);
    }
    g.Write32(layout::CONTROL + 12, 0);
    return true;
  }
  FuzzHost& host;
  Guest g;
  Rng& rng;
  u32 from, to;
  u32 first = 0, machines = 0, most_extra = 0;
};

inline void WriteFile(const std::string& path, const std::string& bytes)
{
  std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

inline void Put32(std::string& s, u32 v)
{
  for (int i = 3; i >= 0; --i)
    s += static_cast<char>(v >> (8 * i));
}

inline void MakeBundle(const std::string& dir)
{
  std::string image(0x100, '\0');
  image.replace(0, 4, "FZMD");
  image.replace(0x10, 4, "FZSI");
  WriteFile(dir + "/mod.bin", image);
  WriteFile(dir + "/manifest.json", "{\"commit\": \"fuzz\"}");
  WriteFile(dir + "/codes.txt", "$Boot to race\n48000000 800030C8\n$Replay boot\n04000000 00000001\n");
}

struct Totals
{
  unsigned long long inputs = 0, taken = 0, read = 0, frames = 0, stopped = 0;
  unsigned long long launch = 0, launch_taken = 0, launch_raced = 0;
};

// fuzz_replays.cpp, fuzz_relay.cpp, fuzz_launch.cpp, fuzz_packs.cpp, fuzz_board.cpp
void Mutate(picojson::value& v, Rng& rng, int& budget);
void FuzzLaunchFile(Rng& rng, FuzzHost& host, const std::string& dir, Totals& n);
void FuzzReplays(double seconds, u32 seed, const std::string& dir);
void FuzzRelay(double seconds, u32 seed, const std::string& dir);
void FuzzPacks(double seconds, u32 seed, const std::string& dir);
void FuzzABoard(Rng& rng, FuzzHost& host, const std::string& dir);
