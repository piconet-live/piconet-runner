// Any colour for every racer's machine: lifted from the Dolphin fork (dolphin-fzgx Core/FzgxPaint.cpp at
// 5bdc7c640e). The stock texture files come from the host (the extracted game), not from a disc image: files_disc.cpp
// reads them.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <vector>
#include <format>
#include <optional>

namespace fzgx::paint
{
namespace
{
constexpr u32 BSS = 0x1BA040;           // main.rel's .bss from its header ([0x800030C8])
constexpr u32 CARS = BSS + 0x6D83C;     // the car objects (0x440 each, racer order)
constexpr u32 CAR_SIZE = 0x440;
constexpr u32 RAM = game::RAM;
constexpr u32 RAM_SIZE = 0x1800000;     // the game's 24 MiB
// tests: control block (0x81810000) +536 machines repainted, +540 skipped, +544 the last racer skipped + 1 and why
// (1 no disc, 2 no stock set, 3 no stock copy, 4 bad machine id) in its top byte
constexpr u32 REPORT = table::PAINT_REPORT_AT;

// main.rel .data 0x1F4FC: the machines' folder names under vehicle/, by machine id
constexpr std::array<const char*, 41> MACHINES = {
    "rgaze", "wcat",  "gfox",  "itiger", "fsting", "wgoose", "bfalcon", "dclaw", "gstar", "lwy",   "mwolf",
    "spira", "danc",  "arobin", "bfang", "sphan",  "gpan",   "hspeed",  "sang",  "kmete", "qmete", "tnori",
    "nthun", "wboar", "hhawk", "wwasp",  "mty",    "mhurr",  "cbear",   "bbull", "darks", "fats",  "scarp",
    "pinks", "mgull", "srat",  "smoon",  "prabbit", "gtaxi", "rrock",   "rainp"};

void Put16(u8* p, u16 v)
{
  p[0] = static_cast<u8>(v >> 8);
  p[1] = static_cast<u8>(v);
}

void Put32(u8* p, u32 v)
{
  p[0] = static_cast<u8>(v >> 24);
  p[1] = static_cast<u8>(v >> 16);
  p[2] = static_cast<u8>(v >> 8);
  p[3] = static_cast<u8>(v);
}

double Lum(double r, double g, double b)
{
  return 0.299 * r + 0.587 * g + 0.114 * b;
}

std::array<double, 3> From565(u16 c)
{
  return {((c >> 11) & 31) * 255.0 / 31, ((c >> 5) & 63) * 255.0 / 63, (c & 31) * 255.0 / 31};
}

u16 To565(const std::array<double, 3>& c)
{
  return static_cast<u16>((std::lround(c[0] * 31 / 255) << 11) | (std::lround(c[1] * 63 / 255) << 5) |
                          std::lround(c[2] * 31 / 255));
}

// The paint is where the set's four files differ; it takes the colour, its
// brightness relative to the paint's mean brightness shading it darker or lighter
Bytes Recolor(const Bytes& stock, const std::vector<const Bytes*>& variants, const Rgb& rgb)
{
  const std::vector<Slot> slots = Slots(stock);
  const auto paint = [&](u32 o, u32 unit) {
    return std::ranges::any_of(variants, [&](const Bytes* v) {
      return v->size() >= o + unit && std::memcmp(v->data() + o, stock.data() + o, unit) != 0;
    });
  };
  double sum = 0;
  size_t count = 0;
  for (const Slot& s : slots)
  {
    if (s.format != 14)
      continue;
    for (u32 o = s.start; o + 8 <= s.end; o += 8)
    {
      if (!paint(o, 8))
        continue;
      const auto c0 = From565(Be16(stock.data() + o)), c1 = From565(Be16(stock.data() + o + 2));
      sum += Lum(c0[0], c0[1], c0[2]) + Lum(c1[0], c1[1], c1[2]);
      count += 2;
    }
  }
  const double mean = count ? sum / count : 128.0;
  const std::array<double, 3> target = {double(rgb.r), double(rgb.g), double(rgb.b)};
  const auto shade = [&](const std::array<double, 3>& c) {
    const double t = std::clamp(0.5 * Lum(c[0], c[1], c[2]) / std::max(mean, 1.0), 0.0, 1.0);
    std::array<double, 3> out;
    for (int i = 0; i < 3; ++i)
      out[i] = t < 0.5 ? target[i] * 2 * t : target[i] + (255 - target[i]) * (2 * t - 1);
    return out;
  };
  Bytes out = stock;
  for (const Slot& s : slots)
  {
    if (s.format == 14)  // CMPR: 8-byte blocks, two RGB565 colours and 2-bit indices
    {
      for (u32 o = s.start; o + 8 <= s.end; o += 8)
      {
        if (!paint(o, 8))
          continue;
        const u16 c0 = Be16(stock.data() + o), c1 = Be16(stock.data() + o + 2);
        u32 idx = Be32(stock.data() + o + 4);
        u16 n0 = To565(shade(From565(c0))), n1 = To565(shade(From565(c1)));
        if (c0 > c1)  // four opaque colours: needs n0 > n1
        {
          if (n0 < n1)
          {
            std::swap(n0, n1);
            idx ^= 0x55555555;
          }
          else if (n0 == n1)
          {
            idx = 0;
          }
        }
        else if (n0 > n1)  // three colours and transparent: needs n0 <= n1
        {
          std::swap(n0, n1);
          u32 remapped = 0;
          for (int b = 0; b < 32; b += 2)
          {
            const u32 v = (idx >> b) & 3;
            remapped |= (v < 2 ? v ^ 1 : v) << b;
          }
          idx = remapped;
        }
        Put16(out.data() + o, n0);
        Put16(out.data() + o + 2, n1);
        Put32(out.data() + o + 4, idx);
      }
    }
    else if (s.format == 5)  // RGB5A3
    {
      for (u32 o = s.start; o + 2 <= s.end; o += 2)
      {
        if (!paint(o, 2))
          continue;
        u16 v = Be16(stock.data() + o);
        if (v & 0x8000)  // opaque RGB555
        {
          const auto c = shade({((v >> 10) & 31) * 255.0 / 31, ((v >> 5) & 31) * 255.0 / 31, (v & 31) * 255.0 / 31});
          v = static_cast<u16>(0x8000 | (std::lround(c[0] * 31 / 255) << 10) | (std::lround(c[1] * 31 / 255) << 5) |
                               std::lround(c[2] * 31 / 255));
        }
        else  // ARGB3444: keep alpha
        {
          const u16 a = (v >> 12) & 7;
          const auto c = shade({((v >> 8) & 15) * 17.0, ((v >> 4) & 15) * 17.0, (v & 15) * 17.0});
          v = static_cast<u16>((a << 12) | (std::lround(c[0] / 17) << 8) | (std::lround(c[1] / 17) << 4) |
                               std::lround(c[2] / 17));
        }
        Put16(out.data() + o, v);
      }
    }
  }
  return out;
}

std::vector<const Bytes*> Others(const std::array<Bytes, 4>& set, int k)
{
  std::vector<const Bytes*> others;
  for (int i = 0; i < 4; ++i)
  {
    if (i != k)
      others.push_back(&set[i]);
  }
  return others;
}

// PaintMachines' steps, in its order.

// A machine's four stock texture files, by machine id and whether it is the local seat's main set.
using Sets = std::map<std::pair<u8, bool>, std::array<Bytes, 4>>;

// A set's file as it stands loaded in memory.
struct Copy
{
  u32 at;
  u8 machine;
  bool main;
  int variant;
  bool used = false;
};

// Every racer's machine id, and the stock sets of those machines as the disc has them.
Sets StockSets(Guest& g, u32 cars, int main_racer, std::array<u8, 30>& machine)
{
  // the stock sets of the racers' machines: opponent sets for everyone, the main set for the local seat
  std::map<std::pair<u8, bool>, std::array<Bytes, 4>> sets;
  for (int racer = 0; racer < 30; ++racer)
  {
    machine[racer] = g.Read8(cars + racer * CAR_SIZE + 0x328);
    if (machine[racer] >= MACHINES.size())
      continue;
    for (const bool main : {false, racer == main_racer})
    {
      const auto key = std::make_pair(machine[racer], main);
      if (sets.contains(key))
        continue;
      if (auto set = ReadSet(MACHINES[machine[racer]], main))
        sets.emplace(key, std::move(*set));
    }
  }
  return sets;
}

// Where those files stand loaded.
std::vector<Copy> LoadedCopies(const std::vector<u8>& ram, const Sets& sets)
{
  // every loaded copy of those sets: one pass over RAM (copies start 32-aligned), keyed by their first 32 bytes
  std::map<std::vector<u8>, std::vector<std::pair<std::pair<u8, bool>, int>>> heads;
  for (const auto& [key, set] : sets)
  {
    for (int k = 0; k < 4; ++k)
    {
      if (set[k].size() >= 32)
        heads[std::vector<u8>(set[k].begin(), set[k].begin() + 32)].emplace_back(key, k);
    }
  }
  std::vector<Copy> copies;
  std::vector<u8> head(32);
  for (u32 at = 0; at + 32 <= ram.size(); at += 32)
  {
    std::memcpy(head.data(), ram.data() + at, 32);
    const auto it = heads.find(head);
    if (it == heads.end())
      continue;
    for (const auto& [key, k] : it->second)
    {
      const Bytes& f = sets.at(key)[k];
      if (at + f.size() <= ram.size() && std::memcmp(ram.data() + at, f.data(), f.size()) == 0)
      {
        copies.push_back({at, key.first, key.second, k});
        break;
      }
    }
  }
  return copies;
}

// Which loaded copy is each wanted racer's own (-1: none found).
std::array<int, 30> OwnCopies(Guest& g, u32 cars, int main_racer, const std::array<u8, 30>& machine,
                              std::vector<Copy>& copies, const auto& wants)
{
  // a racer's opponent model (car +0x330) holds at +0x68 the copy loaded after the racer's own (the copies are
  // loaded in the models' order); the last racer's points past them, and gets its machine's one copy left
  std::array<int, 30> own;
  own.fill(-1);
  std::vector<int> unresolved;
  for (int racer = 0; racer < 30; ++racer)
  {
    if (!wants(racer) || racer == main_racer || machine[racer] >= MACHINES.size())
      continue;
    const u32 model = g.Read32(cars + racer * CAR_SIZE + 0x330);
    const u32 next = model >= RAM && model < RAM + RAM_SIZE - 0x70 ? g.Read32(model + 0x68) : 0;
    const auto it = std::ranges::find_if(copies, [&](const Copy& c) { return !c.main && c.at + RAM == next; });
    int i = -1;
    if (it != copies.end())
    {
      for (int j = static_cast<int>(it - copies.begin()) - 1; j >= 0 && i < 0; --j)
      {
        if (!copies[j].main)
          i = j;
      }
    }
    if (i >= 0 && copies[i].machine == machine[racer] && !copies[i].used)
    {
      own[racer] = i;
      copies[i].used = true;
    }
    else
    {
      unresolved.push_back(racer);
    }
  }
  for (const int racer : unresolved)
  {
    std::vector<int> left;
    for (int j = 0; j < static_cast<int>(copies.size()); ++j)
    {
      if (!copies[j].main && !copies[j].used && copies[j].machine == machine[racer])
        left.push_back(j);
    }
    if (left.size() == 1)
    {
      own[racer] = left[0];
      copies[left[0]].used = true;
    }
  }
  if (main_racer >= 0 && main_racer < 30 && wants(main_racer))
  {
    const auto it = std::ranges::find_if(copies, [&](const Copy& c) { return c.main && c.machine == machine[main_racer]; });
    if (it != copies.end())
      own[main_racer] = static_cast<int>(it - copies.begin());
  }
  return own;
}

// Each wanted racer's copy repainted, or replaced by a stock file; the counts to the log and the mod.
void Repaint(Guest& g, const std::array<std::optional<Rgb>, 30>& colors, const std::array<int, 30>& stock,
             const Sets& sets, const std::vector<Copy>& copies, const std::array<int, 30>& own, const auto& wants)
{
  int painted = 0, skipped = 0;
  for (int racer = 0; racer < 30; ++racer)
  {
    if (!wants(racer))
      continue;
    if (own[racer] < 0)
    {
      Warn("fzgx: colours: racer {}: no copy of its own found", racer);
      g.Write32(REPORT + 8, 3u << 24 | (racer + 1));
      ++skipped;
      continue;
    }
    const Copy& c = copies[own[racer]];
    const auto& set = sets.at({c.machine, c.main});
    if (stock[racer] >= 0 && stock[racer] < 4)
    {
      // a stock colour: that stock file in place of the one loaded (the same size: the set's four files are alike)
      const Bytes& want = set[stock[racer]];
      if (want.size() != set[c.variant].size())
      {
        Warn("fzgx: colours: racer {}: stock colour {} is not the loaded file's size", racer, stock[racer]);
        ++skipped;
        continue;
      }
      if (stock[racer] != c.variant)
        g.CopyTo(RAM + c.at, want.data(), want.size());
      ++painted;
      continue;
    }
    const Bytes out = Recolor(set[c.variant], Others(set, c.variant), *colors[racer]);
    g.CopyTo(RAM + c.at, out.data(), out.size());
    ++painted;
  }
  Notice("fzgx: colours: {} machines repainted, {} skipped ({} copies loaded)", painted, skipped,
                 copies.size());
  g.Write32(REPORT, static_cast<u32>(painted));
  g.Write32(REPORT + 4, static_cast<u32>(skipped));
}
}  // namespace

void PaintMachines(Guest& g,
                   const std::array<std::optional<Rgb>, 30>& colors, const std::array<int, 30>& stock,
                   int main_racer)
{
  const auto wants = [&](int racer) { return colors[racer].has_value() || (stock[racer] >= 0 && stock[racer] < 4); };
  std::vector<u8> ram(RAM_SIZE);
  g.CopyFrom(ram.data(), RAM, ram.size());
  const u32 module = g.Read32(game::MODULE_LIST);
  const u32 cars = g.Read32(module + CARS);

  std::array<u8, 30> machine{};
  const Sets sets = StockSets(g, cars, main_racer, machine);
  std::vector<Copy> copies = LoadedCopies(ram, sets);
  const std::array<int, 30> own = OwnCopies(g, cars, main_racer, machine, copies, wants);
  Repaint(g, colors, stock, sets, copies, own, wants);
}
}  // namespace fzgx::paint
