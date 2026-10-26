// The seats' block (table::SEAT_BLOCK; handbook/stat-pack-constants.md): every seat's constants, and a car data row
// for every seat whose stock machine races a pack of its own. The native module's patched sites read a machine's own
// seat's record and row (port/image/sites_seats.py, sites_dash.py); the fork's image reads none of it.
//
// What a seat races: its choice (Match::seat_pack): -1 the race's pack (setup.stats, laid over the shared car data as
// ever: stat_pack.cpp), 0 the disc's own, k the race's k-th pack of setup.packs. Its constants are that pack's, and the
// game's own where the pack has none. Its row, for a stock machine with a choice of its own (0 or k): the machine's
// block as the loader left it, with that pack's values for the machine over it; a custom machine's stats are the sum
// of its parts in the shared table, so it takes only its constants.
//
// When. The constants are part of the block the boot writes (Prepare, and OnFrame in the reference configuration),
// as the race block is. The rows are made on the first frame the loaded car data stands, before the race's pack is
// laid over it in the same frame (stat_pack.cpp), so a row starts from the disc's own block; then each row is written
// and its byte turned on. The block is the data window's: no rollback puts it back, and no step writes it. The go
// refuses a race whose rows are not on, or whose block is not the one the match makes.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <cmath>
#include <cstring>
#include <format>

#include "const_ranges.h"
#include "stat_ranges.h"

namespace fzgx::consts
{
namespace
{
bool NormalOrZero(u64 bits, u32 exponent_bits, u32 mantissa_bits)
{
  const u64 exponent = (bits >> mantissa_bits) & ((u64{1} << exponent_bits) - 1);
  return bits == 0 || (exponent != 0 && exponent != (u64{1} << exponent_bits) - 1);
}

std::string Number(double v)
{
  return std::format("{}", v);
}
}  // namespace

std::string ValueText(u32 slot, u32 bits, u32 low_bits)
{
  if (CONSTANTS[slot].kind == Kind::Number32)
    return std::format("{}", std::bit_cast<float>(bits));
  return Number(std::bit_cast<double>(u64{bits} << 32 | low_bits));
}

std::optional<std::string> Refusal(u32 slot, u32 bits, u32 low_bits)
{
  const Constant& c = CONSTANTS[slot];
  if (c.kind == Kind::Number64Low)
    return std::nullopt;
  const bool single = c.kind == Kind::Number32;
  const u64 all = single ? u64{bits} : u64{bits} << 32 | low_bits;
  if (!(single ? NormalOrZero(all, 8, 23) : NormalOrZero(all, 11, 52)))
    return std::format("is the bits {:0{}X}, which is no number a pack may hold (a denormal, -0, an infinity or none at all)",
                       all, single ? 8 : 16);
  const double v = single ? double{std::bit_cast<float>(bits)} : std::bit_cast<double>(all);
  if (!(v >= c.low))
    return std::format("is {}, and must be at least {}", ValueText(slot, bits, low_bits), Number(c.low));
  if (!(v <= c.high))
    return std::format("is {}, and must be at most {}", ValueText(slot, bits, low_bits), Number(c.high));
  return std::nullopt;
}
}  // namespace fzgx::consts

namespace fzgx::boot
{
namespace
{
constexpr u32 FLAGS = table::SEAT_BLOCK;
constexpr u32 DURATION = 0x38;  // a block's boost duration
bool s_rows = false;  // the rows are made and on
// the disc's boost durations, read on the first frame the table stands (before the race's pack is laid over it)
std::optional<std::array<u32, stats::BLOCKS>> s_durations;

// The pack a seat races (its constants, and a stock machine's row): null, the disc's own.
const StatPack* PackOfSeat(const Match& m, u32 seat)
{
  const int k = m.seat_pack[seat];
  if (k < 0)
    return m.stats ? &*m.stats : nullptr;
  return k == 0 ? nullptr : &m.packs[static_cast<size_t>(k) - 1];
}

// A seat races a row of its own: a choice of its own and a stock machine (the race block's, a replay's lineup's too).
bool RowOn(const Match& m, u32 seat)
{
  return m.seat_pack[seat] >= 0 && m.race[32 + seat] <= MAX_MACHINE;
}

u32 RowAt(u32 seat)
{
  return table::SEAT_ROWS_AT + seat * stats::BLOCK_SIZE;
}
}  // namespace

bool HasSeatPacks(const Match& m)
{
  if (!m.packs.empty())
    return true;
  for (const int k : m.seat_pack)
  {
    if (k >= 0)
      return true;
  }
  return false;
}

std::vector<u8> SeatBlock(const Match& m)
{
  std::vector<u8> block(table::SEAT_BLOCK_SIZE, 0);
  for (u32 seat = 0; seat < MAX_SEATS; ++seat)
  {
    const StatPack* pack = PackOfSeat(m, seat);
    u8* record = block.data() + table::SEAT_CONSTANTS + seat * table::SEAT_CONSTANT;
    for (u32 slot = 0; slot < consts::SLOTS; ++slot)
    {
      u32 bits = consts::CONSTANTS[slot].stock;
      if (pack)
      {
        for (const StatEntry& e : pack->constants)
        {
          if (e.at == slot)
            bits = e.value;
        }
      }
      Put32(record + consts::CONSTANTS[slot].at, bits);
    }
  }
  return block;
}

std::optional<std::string> BoostLengthRefusal(const StatPack& pack,
                                              const std::array<u32, file::CAR_MACHINES + file::CAR_PARTS>& disc)
{
  constexpr u32 BASE = 5, SCALE = 6;  // the slots
  static_assert(std::string_view(consts::CONSTANTS[BASE].name) == "BoostLengthBase" &&
                std::string_view(consts::CONSTANTS[SCALE].name) == "BoostLengthScale");
  u32 base = consts::CONSTANTS[BASE].stock, scale = consts::CONSTANTS[SCALE].stock;
  bool held = false;
  for (const StatEntry& e : pack.constants)
  {
    if (e.at == BASE || e.at == SCALE)
    {
      (e.at == BASE ? base : scale) = e.value;
      held = true;
    }
  }
  if (!held)
    return std::nullopt;
  std::array<u32, stats::BLOCKS> d = disc;
  for (const StatEntry& e : pack.entries)
  {
    if (e.at == DURATION && e.block < stats::BLOCKS)
      d[e.block] = e.value;
  }
  const auto seconds = [&](u32 block) { return std::bit_cast<float>(d[block]); };
  float longest = 0;
  for (u32 machine = 0; machine < stats::MACHINES; ++machine)
    longest = std::max(longest, seconds(machine));
  // a custom machine: a body (parts 0 to 24), a cockpit (25 to 49) and a booster (50 to 74), summed
  constexpr u32 EACH = file::CAR_PARTS / 3;
  float sum = 0;
  for (u32 kind = 0; kind < 3; ++kind)
  {
    float most = -std::numeric_limits<float>::max();
    for (u32 part = 0; part < EACH; ++part)
      most = std::max(most, seconds(stats::MACHINES + kind * EACH + part));
    sum += most;
  }
  longest = std::max(longest, sum);
  // as the game counts: the single product, then the double sum, cut toward 0
  const double frames = double{std::bit_cast<float>(base)} + double{std::bit_cast<float>(scale) * longest};
  if (frames < 256)
    return std::nullopt;
  return std::format("the pack {}'s energy boost lasts {} + {} x boost duration, {} frames for the longest boost "
                     "duration its car data gives ({}), and the game counts at most 255",
                     pack.name, consts::ValueText(BASE, base), consts::ValueText(SCALE, scale), std::floor(frames), longest);
}

void ResetSeats()
{
  s_rows = false;
  s_durations.reset();
}

void LaySeatRows(Guest& g, const Match& m)
{
  if (!s_durations && TableStands(g))
  {
    std::array<u32, stats::BLOCKS> d{};
    for (u32 block = 0; block < stats::BLOCKS; ++block)
      d[block] = g.Read32(game::CAR_DATA + stats::BlockAt(block) + DURATION);
    s_durations = d;
  }
  if (s_rows || !TableStands(g))
    return;
  u32 made = 0;
  for (u32 seat = 0; seat < MAX_SEATS; ++seat)
  {
    if (!RowOn(m, seat))
      continue;
    const u32 machine = m.race[32 + seat];
    std::array<u8, stats::BLOCK_SIZE> row{};
    g.CopyFrom(row.data(), game::CAR_DATA + stats::BlockAt(machine), row.size());
    if (const StatPack* pack = PackOfSeat(m, seat))
    {
      for (const StatEntry& e : pack->entries)
      {
        if (e.block == machine)
          Put32(row.data() + e.at, e.value);
      }
    }
    g.CopyTo(RowAt(seat), row.data(), row.size());
    g.Write8(FLAGS + seat, 1);
    ++made;
  }
  s_rows = true;
  if (made != 0)
    Notice("fzgx: stat packs a seat: {} seats race a row of their own", made);
}

std::optional<std::string> SeatsRefusal(const Guest& g, const Match& m)
{
  // A race without a pack is the game's own: its records hold the game's numbers from the boot on (Prepare), and
  // nothing of it is judged here, as before packs.
  if (!m.stats && !HasSeatPacks(m))
    return std::nullopt;
  // the energy boost's length of every pack in the race: within its count's byte for its longest boost
  std::vector<const StatPack*> packs;
  if (m.stats)
    packs.push_back(&*m.stats);
  for (const StatPack& p : m.packs)
    packs.push_back(&p);
  for (const StatPack* p : packs)
  {
    const bool holds = std::any_of(p->constants.begin(), p->constants.end(), [](const StatEntry& e) { return e.at == 5 || e.at == 6; });
    if (holds && !s_durations)
      return std::format("the pack {} sets the energy boost's length, and the disc's boost durations were not read "
                         "before the race's pack was laid", p->name);
    if (holds)
    {
      if (std::optional<std::string> why = BoostLengthRefusal(*p, *s_durations))
        return why;
    }
  }
  // the constants: the block the match makes (a frame that wrote another block would race another race)
  const std::vector<u8> block = SeatBlock(m);
  std::vector<u8> held(MAX_SEATS * table::SEAT_CONSTANT);
  g.CopyFrom(held.data(), table::SEAT_CONSTANTS_AT, held.size());
  if (std::memcmp(held.data(), block.data() + table::SEAT_CONSTANTS, held.size()) != 0)
    return std::string("the seats' constants are not the ones the race's packs give");
  u32 rows = 0;
  for (u32 seat = 0; seat < MAX_SEATS; ++seat)
  {
    const bool on = g.Read8(FLAGS + seat) != 0;
    if (on != RowOn(m, seat))
      return std::format("seat {}'s row is {}, and its pack says {}", seat, on ? "on" : "off", on ? "none" : "one");
    rows += on;
  }
  if (HasSeatPacks(m))
  {
    std::string seats;
    for (u32 seat = 0; seat < MAX_SEATS; ++seat)
    {
      if (m.seat_pack[seat] >= 0)
        seats += std::format("{}{}:{}", seats.empty() ? "" : " ", seat,
                             m.seat_pack[seat] == 0 ? std::string("disc") : m.packs[m.seat_pack[seat] - 1].name);
    }
    Notice("fzgx: stat packs a seat: {} rows on; seats of their own: {}", rows, seats);
  }
  return std::nullopt;
}
}  // namespace fzgx::boot
