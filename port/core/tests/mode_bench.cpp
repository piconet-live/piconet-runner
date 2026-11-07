// A bench of the game mode's interpreter (mode.cpp), outside the test suite: a fake race of 30 machines whose state
// moves every step in a way fixed by the step number, each package given run over it. Two uses:
//   mode_bench time STEPS FILE...       the host's time a run and an operation, for each *.mode.json
//   mode_bench hash STEPS FILE...       a line for each run: the step, the hash of the mode's block and of the machines
//                                       and race record, the block's operation count. Two builds of the interpreter
//                                       must print the same bytes
//   mode_bench fuzz ROUNDS STEPS FILE...  the same hashes with each package's code changed at random positions: every
//                                       fault must fall on the same operation with the same state
// Every sixteenth step is run again from a copy of memory, as a rollback would, and has to give the same bytes. Built
// by hand, with the core's sources at the optimisation the game has (a mode's cost is an optimised build's question):
//   g++ -std=c++20 -O2 -Iinclude -Isrc -Ithird_party/picojson -I<protocol>/protocol/generated tests/mode_bench.cpp src/*.cpp -llzma
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "fzgx/guest.h"
#include "fzgx/host.h"
#include "mode.h"
#include "picojson.h"
#include "rollback.h"

using namespace fzgx;

namespace
{
constexpr u32 MODULE = 0x801BA660, MACHINES = 0x80400000, PROGRESS = 0x80480000, BLOCK = 0x80500000, COURSE = 0x80510000;
constexpr u32 SEATS = 30, R = MODULE + 0x1BDC70;

class BenchHost final : public Host
{
public:
  BenchHost() : ram(layout::RETAIL_RAM_SIZE, 0) {}
  u8* Ram() override { return ram.data(); }
  u32 RamSize() override { return static_cast<u32>(ram.size()); }
  u8* Scratch() override { return nullptr; }
  u32 ScratchSize() override { return 0; }
  void RequestStop() override {}
  float EmulationSpeed() override { return 1; }
  float CpuClock() override { return 1; }
  void SetPace(float, float) override {}
  void Log(LogLevel, const std::string&) override {}
  bool ReadGameFile(const std::string&, std::vector<u8>*) override { return false; }
  void CodeChanged(u32, u32) override {}
  void Recorder(std::string*, std::string*) override {}
  std::string CardFolder() override { return {}; }
  u32 PictureIntervals(u32*, u32) override { return 0; }
  std::string UserFolder() override { return {}; }
  std::vector<u8> ram;
};

u32 Next(u32& x)
{
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

u32 RealBits(float f)
{
  return std::bit_cast<u32>(f);
}

u32 MachineAt(u32 seat)
{
  return MACHINES + seat * game::MACHINE_SIZE;
}

u32 ProgressAt(u32 seat)
{
  return PROGRESS + seat * game::PROGRESS_SIZE;
}

void Setup(Guest& g)
{
  g.Write32(net::MODULE_POINTER, MODULE);
  g.Write32(MODULE + net::MACHINES, MACHINES);
  g.Write8(MODULE + net::MACHINE_COUNT, SEATS);
  g.Write16(MODULE + game::PHASE, game::PHASE_RACE - 1);
  g.Write32(MODULE + 0x1BDC20, COURSE);
  g.Write32(COURSE + 0x24, COURSE + 0x100);
  g.Write32(COURSE + 0x100, RealBits(-100.0f));
  for (u32 s = 0; s < SEATS; ++s)
  {
    const u32 m = MachineAt(s);
    g.Write32(m + game::MACHINE_PROGRESS, ProgressAt(s));
    g.Write32(m + game::MACHINE_SPEED, RealBits(300.0f + 20.0f * static_cast<float>(s)));
    g.Write32(m + game::MACHINE_ENERGY, RealBits(100.0f));
    g.Write32(m + 0x8, RealBits(1000.0f + static_cast<float>(s)));
    g.Write16(m + 0x5DA, 100);
    g.Write8(R + 0xE + s, static_cast<u8>(s));
    for (u32 c = 0; c < 3; ++c)
    {
      g.Write32(m + game::MACHINE_POSITION_X + 4 * c, RealBits(static_cast<float>(s * 10 + c)));
      g.Write32(m + 0x94 + 4 * c, RealBits(static_cast<float>(c) - 1.0f));
      g.Write32(m + 0xF4 + 0x10 * c, RealBits(c == 2 ? -1.0f : 0.0f));
    }
  }
}

// The step's race: what the game would have changed, fixed by the step's number. `busy`: enough of it to fire events.
void Move(Guest& g, u32 step, bool busy)
{
  u32 x = step * 2654435761u + 1;
  g.Write32(MODULE + game::RACE_CLOCK, step);
  if (step == 60)
    g.Write16(MODULE + game::PHASE, game::PHASE_RACE);
  for (u32 s = 0; s < SEATS; ++s)
  {
    const u32 m = MachineAt(s), p = ProgressAt(s);
    const u32 r = Next(x);
    g.Write32(m + game::MACHINE_SPEED, RealBits(300.0f + static_cast<float>((r >> 8) % 1000)));
    g.Write32(p + game::PROGRESS_DISTANCE, RealBits(static_cast<float>(step) * (5.0f + static_cast<float>(s) / 10.0f)));
    if (!busy)
      continue;
    u32 flags = g.Read32(m + game::MACHINE_FLAGS);
    if ((r & 0x3F) == 1)
      flags ^= 0x20;
    if ((r & 0x3F) == 2)
      flags ^= 0x2;
    if ((r & 0xFFF) == 3)
      flags |= game::MACHINE_FLAG_BROKEN_DOWN;
    if ((r & 0x3FFF) == 4)
      flags |= game::MACHINE_FLAG_FINISHED;
    if ((r & 0x3FF) == 5)
      flags ^= 0x00800000;
    if ((r & 0xFFFF) == 6)
      g.Write32(m + 0x58C, g.Read32(m + 0x58C) | 0x10);
    g.Write32(m + game::MACHINE_FLAGS, flags);
    if ((r & 0x7F) == 7)
      g.Write32(m + game::MACHINE_ENERGY, RealBits(100.0f - static_cast<float>(step % 90)));
    if ((r & 0x1FF) == 8)
      g.Write32(p + game::PROGRESS_LAPS, g.Read32(p + game::PROGRESS_LAPS) + 1);
    if ((r & 0x1F) == 9)
      g.Write32(p + 0x140, g.Read32(p + 0x140) + 1);
    if ((r & 0x3FF) == 10)
      g.Write8(m + 0x482, static_cast<u8>(g.Read8(m + 0x482) + 1));
    if ((r & 0x7FF) == 11)
      g.Write16(m + 0x214, 30);
    else if ((r & 0x7) == 3 && g.Read16(m + 0x214) != 0)
      g.Write16(m + 0x214, static_cast<u16>(g.Read16(m + 0x214) - 1));
    if ((r & 0xFF) == 12)
    {
      const u32 a = (r >> 16) % SEATS, b = (r >> 24) % SEATS;
      const u8 ra = g.Read8(R + 0xE + a), rb = g.Read8(R + 0xE + b);
      g.Write8(R + 0xE + a, rb);
      g.Write8(R + 0xE + b, ra);
    }
    if ((r & 0xFF) == 13)
      g.Write32(BLOCK + mode::TOUCH + 4 * s, 0x80000000u >> ((r >> 16) % SEATS));
  }
}

u64 Hash(const u8* p, size_t n, u64 h)
{
  for (size_t i = 0; i < n; ++i)
    h = (h ^ p[i]) * 0x100000001B3ull;
  return h;
}

// The block, the machines, the progress records and the race's first words
u64 State(const BenchHost& host)
{
  u64 h = 0xCBF29CE484222325ull;
  const u8* ram = host.ram.data();
  h = Hash(ram + (BLOCK - layout::RAM), mode::BLOCK, h);
  h = Hash(ram + (MACHINES - layout::RAM), SEATS * game::MACHINE_SIZE, h);
  h = Hash(ram + (PROGRESS - layout::RAM), SEATS * game::PROGRESS_SIZE, h);
  h = Hash(ram + (R - layout::RAM), 0x1500, h);
  return h;
}

picojson::value Load(const char* path)
{
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  picojson::value v;
  picojson::parse(v, text.str());
  return v;
}

std::optional<mode::Package> ReadPackage(const char* path)
{
  std::string why;
  std::optional<mode::Package> p = mode::Read(Load(path), &why);
  if (!p)
  {
    std::printf("%s: refused: %s\n", path, why.c_str());
    return p;
  }
  p->players = 0x3;
  p->seed = {1, 2, 3, 4};
  return p;
}

void Hashes(const char* name, const mode::Package& p, u32 steps, bool busy)
{
  BenchHost host;
  ModData data;
  Guest g(host, data);
  Setup(g);
  std::vector<u8> saved;
  for (u32 step = 0; step < steps; ++step)
  {
    Move(g, step, busy);
    const bool again = step % 16 == 15;
    if (again)
      saved = host.ram;
    mode::Run(g, p, BLOCK, MODULE);
    const u64 h = State(host);
    if (again)
    {
      host.ram = saved;
      mode::Run(g, p, BLOCK, MODULE);
      if (State(host) != h)
        std::printf("%s: step %u: run again differs\n", name, step);
    }
    std::printf("%s %u %016llx ops %u end %u fault %u\n", name, step, static_cast<unsigned long long>(h),
                g.Read32(BLOCK + mode::OPS), g.Read32(BLOCK + mode::END), g.Read32(BLOCK + mode::FAULT));
    if (g.Read32(BLOCK + mode::END) != 0 && step > 300)
      break;
  }
}

void Time(const char* name, const mode::Package& p, u32 steps)
{
  BenchHost host;
  ModData data;
  Guest g(host, data);
  Setup(g);
  double best = 1e30;
  u32 ops = 0, ran = 0;
  for (int round = 0; round < 5; ++round)
  {
    std::fill(host.ram.begin(), host.ram.end(), u8{0});
    Setup(g);
    double us = 0;
    ran = ops = 0;
    for (u32 step = 0; step < steps; ++step)
    {
      Move(g, step, false);
      const auto a = std::chrono::steady_clock::now();
      mode::Run(g, p, BLOCK, MODULE);
      us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count();
      ++ran;
      ops += g.Read32(BLOCK + mode::OPS);
      if (g.Read32(BLOCK + mode::END) != 0)
        break;
    }
    best = std::min(best, us);
  }
  std::printf("%-28s %10.2f us a run  %8.0f ops a run  %7.2f ns an op  (%u runs, best of 5) end %u fault %u\n", name,
              best / ran, static_cast<double>(ops) / ran, ops ? best * 1000.0 / ops : 0.0, ran,
              g.Read32(BLOCK + mode::END), g.Read32(BLOCK + mode::FAULT));
}

const char* BaseName(const char* path)
{
  const char* s = std::strrchr(path, '/');
  return s ? s + 1 : path;
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 4)
    return 2;
  const std::string what = argv[1];
  const u32 first = static_cast<u32>(std::strtoul(argv[2], nullptr, 10));
  if (what == "fuzz")
  {
    const u32 steps = static_cast<u32>(std::strtoul(argv[3], nullptr, 10));
    u32 x = 0x1234567;
    for (int a = 4; a < argc; ++a)
    {
      std::optional<mode::Package> original = ReadPackage(argv[a]);
      if (!original)
        continue;
      for (u32 round = 0; round < first; ++round)
      {
        mode::Package p = *original;
        const u32 changes = 1 + Next(x) % 4;
        for (u32 c = 0; c < changes && !p.code.empty(); ++c)
          p.code[Next(x) % p.code.size()] = static_cast<u8>(Next(x));
        mode::Prepare(p);
        const std::string name = std::string(BaseName(argv[a])) + "#" + std::to_string(round);
        Hashes(name.c_str(), p, steps, true);
      }
    }
    return 0;
  }
  for (int a = 3; a < argc; ++a)
  {
    const std::optional<mode::Package> p = ReadPackage(argv[a]);
    if (!p)
      continue;
    if (what == "time")
      Time(BaseName(argv[a]), *p, first);
    else
      Hashes(BaseName(argv[a]), *p, first, true);
  }
  return 0;
}
