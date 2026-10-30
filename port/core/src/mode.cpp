// The game mode's interpreter (mode.h): the rules of the race's package, once a step at the image's mode point, in
// the order protocol/formats.md section 9 gives. Every state it keeps between two steps is in the block (mode.h's
// layout: the variables, the edges, the waiting rules with their locals, the generator), so a step run again after a
// rollback runs from what the undo log put back; within a run it has only its stack, the rule's locals, the sort
// buffers and the queue, all made anew at every run.
//
// Exactness: the operations count, never time (the budget is the same in every game); the reals are binary32, one
// operation at a time (no expression for the compiler to contract), with the host's floating-point control set to
// round to nearest with denormals kept for the run (the emulator's may differ); a NaN result is 0x7FC00000, as
// modes.toml says. The square root is the game's own (GameSqrt, the compiler's ModeOperators.GameSqrt step for
// step). Where a value meets the game (the held speed) the game's own routines do the arithmetic (the image's
// mode_hold, with the game's square root).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mode.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "out_test.h"
#include "rollback.h"
#include "files.h"
#include "util.h"

namespace fzgx::mode
{
namespace
{
using namespace modes;

constexpr u32 QNAN = 0x7FC00000;

// The game's words the race side reads and writes (the race map: handbook/race-map-machine.md, race-map-flow.md; the
// research notes of wave 2A). A machine block's:
constexpr u32 M_WEIGHT = 0x8, M_MOMENTUM = 0x94, M_TOP_SPEED = 0xD8, M_START_CHARGE = 0xDC;
constexpr u32 M_UP = 0xF0, M_NOSE = 0xF4, M_ROW = 0x10;            // the orientation, rows of 0x10: columns 1 and 2
constexpr u32 M_PITCH = 0x1F4, M_STRAFE = 0x1F8, M_STEER = 0x1FC, M_ACCEL = 0x200, M_BRAKE = 0x204;
constexpr u32 M_RESTORING = 0x214, M_SURFACE = 0x218, M_BASE_SPEED = 0x224;   // (u16; the step's surface bits)
constexpr u32 M_LAST_HIT = 0x4AC, M_SETTLED = 0x481, M_KOS = 0x482, M_CPU = 0x484;  // (u8, u8, the CPU block)
constexpr u32 M_CONTACT = 0x58C, M_KO_WAIT = 0x593, M_MAX_ENERGY = 0x5DA;     // (u8 at 0x593; u16 at 0x5DA)
constexpr u32 F_RESTORE = 0x1, F_AIRBORNE = 0x2, F_SPIN = 0x8, F_BOOSTING = 0x20, F_BROKEN = 0x80, F_DRIVEN = 0x400,
              F_FALLEN = 0x800, F_FROZEN = 0x8000, F_FINISHED = 0x10000, F_SIDE = 0x20000, F_WALL = 0x00800000,
              F_RETIRED = 0x08000000, F_TIME_OVER = 0x10000000, F_DESTROYED = 0x40000000;
// a progress record's
constexpr u32 P_FRACTION = 0xE8, P_LAP_DISTANCE = 0x100, P_WRONG_WAY = 0x10C, P_STANDING = 0x115, P_CHECKPOINT = 0x140,
              P_LAP_STEPS = 0x160;
// the race record R (.bss 0x3C30), from the module's base: the rule word, the standing (a seat a byte, and its
// progress records), the timed mode's timer (u16), the results' slot, the results' records (0xA0 a seat), the time
// limit and its bonuses
constexpr u32 R_BASE = 0x1BDC70, R_ORDER = 0xE, R_ORDER_P = 0x2C, R_TIMER = 0xA4, R_SLOT = 0xA7, R_RECORDS = 0xD4,
              R_RECORD = 0xA0, R_TIME_LIMIT = 0x13F8, R_BONUS = 0x13FA, R_BONUS_END = 0x1404;
constexpr u32 RULE_STOP = 0x80, RULE_TIMED = 0x40000;
// the car manager (its machines' pointer is net::MACHINES = +0x18): a seat's standing, and the standing's seats
constexpr u32 CM_BASE = 0x227860, CM_RANK = 0x21C, CM_ORDER = 0x23C;
constexpr u32 STORY_LEVEL = 0x1BA040 + 0x9D;   // .bss 0x9C + 1: the story's difficulty, which CPU modes read
constexpr u32 RACE_COURSE_AT = table::RACE + 4;
constexpr u32 ROLL_ACCEL = 0x3F19999A;          // 0.6f: the game's own pre-GO accelerator (.rodata 0xE24)
constexpr u32 OUT_CAUSES[][2] = {{F_TIME_OVER, 5}, {F_DESTROYED, 2}, {F_FALLEN, 3}, {F_BROKEN, 1}, {F_RETIRED, 4}};
constexpr u32 STACK = 256;
constexpr u32 MOST_KEYS = 8;

// The host's floating-point control for the run: round to nearest, denormals kept, every exception masked (MXCSR's
// power-on value), and back as it was after.
class FloatControl
{
public:
#if defined(__x86_64__) || defined(_M_X64)
  FloatControl() : m_was(_mm_getcsr()) { _mm_setcsr(0x1F80); }
  ~FloatControl() { _mm_setcsr(m_was); }

private:
  unsigned m_was;
#else
#error "the mode interpreter sets the host's floating-point control: add this architecture's (the ARM port)"
#endif
};

u32 Bits(float f)
{
  return std::isnan(f) ? QNAN : std::bit_cast<u32>(f);
}
float Real(u32 bits)
{
  return std::bit_cast<float>(bits);
}
s32 Int(u32 bits)
{
  return static_cast<s32>(bits);
}
u32 Word(s32 v)
{
  return static_cast<u32>(v);
}

// The game's square root (main.dol 0x8006D0B4): x times 1/sqrt(x), from the processor's estimate (frsqrte) and two
// Newton steps y = y (1.5 - 0.5 x y y), in the processor's single-precision operations: fmuls takes its second operand
// rounded to 25 bits, fnmsubs is fused and rounded once to single. A NaN stays one, infinity itself; zero, a negative
// number or a denormal give 0. The estimate's table is Dolphin's (Common/FloatUtils.cpp, GPL-2.0-or-later), as the
// recomp's runtime has it.
double Force25Bit(double d)
{
  const u64 bits = std::bit_cast<u64>(d);
  return std::bit_cast<double>((bits & 0xFFFFFFFFF8000000ull) + (bits & 0x0000000008000000ull));
}

double ApproximateRsqrt(double value)
{
  struct Entry
  {
    s32 base, dec;
  };
  static constexpr Entry TABLE[32] = {
      {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d}, {0x130c000, -0x435},
      {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2}, {0x0d2e000, -0x365}, {0x0b7c000, -0x32e},
      {0x09e5000, -0x2fc}, {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
      {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226}, {0x0105800, -0x20b},
      {0x3ffa000, -0x7a4}, {0x3c29000, -0x700}, {0x38aa000, -0x670}, {0x3572000, -0x5f2},
      {0x3279000, -0x584}, {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
      {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2}, {0x2287000, -0x38e},
      {0x20c1000, -0x35e}, {0x1f12000, -0x332}, {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},
  };
  const u64 bits = std::bit_cast<u64>(value);  // (a positive normal number: GameSqrt's)
  const u64 mantissa = bits & 0x000FFFFFFFFFFFFFull;
  std::int64_t exponent = static_cast<std::int64_t>(bits & 0x7FF0000000000000ull);
  const u64 lsb = static_cast<u64>(exponent) & 0x0010000000000000ull;
  exponent = (0x3FF0000000000000ll - (exponent - 0x3FE0000000000000ll) / 2) & 0x7FF0000000000000ll;
  const u32 i = static_cast<u32>((lsb | mantissa) >> 37);
  const Entry& e = TABLE[i / 2048];
  return std::bit_cast<double>(static_cast<u64>(exponent) |
                               static_cast<u64>(e.base + e.dec * static_cast<s32>(i % 2048)) << 26);
}

// a x c + addend, fused and rounded once to single (to nearest): the double fma, its tie at single settled by the
// exact error
float SingleFma(double a, double c, double addend)
{
  c = Force25Bit(c);
  double result = std::fma(a, c, addend);
  const u64 bits = std::bit_cast<u64>(result);
  if ((bits & 0x1FFFFFFFull) == 0x10000000ull)
  {
    const double a_prime = addend - result;
    const double b_prime = result + a_prime;
    const double error = std::fma(a, c, a_prime) + (addend - b_prime);
    if (error != 0.0)
      result = std::bit_cast<double>((error > 0.0) == (result > 0.0) ? bits + 1 : bits - 1);
  }
  return static_cast<float>(result);
}

}  // namespace

float GameSqrt(float x)
{
  if (std::isnan(x) || x == std::numeric_limits<float>::infinity())
    return x;
  if (!(x > 0.0f) || !std::isnormal(x))
    return 0.0f;
  const float half = static_cast<float>(0.5 * x);
  double y = ApproximateRsqrt(x);
  for (int i = 0; i < 2; ++i)
  {
    const float square = static_cast<float>(y * Force25Bit(y));
    const float step = -SingleFma(square, half, -1.5);
    y = static_cast<float>(y * step);
  }
  return static_cast<float>(y * x);
}

namespace
{
// A real to an int, saturating at the int's ends; NaN 0.
u32 ToInt(float f)
{
  if (std::isnan(f))
    return 0;
  if (f < -2147483648.0f)
    return Word(std::numeric_limits<s32>::min());
  if (f >= 2147483648.0f)
    return Word(std::numeric_limits<s32>::max());
  return Word(static_cast<s32>(f));
}

// an instance's bit in a rule's words (edges, waiting): its seat's, bit 0 for a rule on no seat
u32 Bit(u32 binding)
{
  return 1u << (binding < SEATS ? binding : 0);
}

// a seat's state at the last run, which its events compare with (mode.h SEAT_STATE), and the mode's own counts
constexpr u32 S_FLAGS = 0x0, S_LAP = 0x4, S_CHECKPOINT = 0x8, S_ENERGY = 0xC, S_KOS = 0x10, S_RESTORING = 0x14,
              S_WRONG = 0x18, S_SURFACE = 0x1C, S_LAP_START = 0x20, S_BEST_LAP = 0x24, S_FINISH = 0x28, S_HITS = 0x2C,
              S_DAMAGE = 0x30, S_WALL = 0x34, S_OUT_AT = 0x38;   // (S_FINISH, S_OUT_AT: the step + 1)
static_assert(S_OUT_AT + 4 <= SEAT_STATE_SIZE);
constexpr u32 SeatState(u32 seat)
{
  return SEAT_STATE + SEAT_STATE_SIZE * seat;
}

// a queued action: its arguments' cells in order (the first, a seat or a set; a vec three cells)
struct Action
{
  u8 id;
  u32 seat;
  std::array<u32, 4> value;
};

// the cells an action pops: its arguments, a vec three
u32 ActionCells(u32 id)
{
  return id == ACTION_PUSH || id == ACTION_SEATVELOCITY ? 4 : static_cast<u32>(std::max(ActionArguments(static_cast<u8>(id)), 0));
}

u32 GameBit(u32 seat)
{
  return 0x80000000u >> seat;
}

// a row of the game's bit order (0x80000000 >> j) as a set of seats (bit j)
u32 SetOfRow(u32 row)
{
  u32 set = 0;
  for (u32 j = 0; j < SEATS; ++j)
    set |= (row & GameBit(j)) ? 1u << j : 0;
  return set;
}

struct Sort
{
  struct Entry
  {
    std::array<u32, MOST_KEYS> keys{};
    u32 seat = 0;
  };
  std::array<Entry, SEATS> entries{};
  u32 count = 0, next = 0;
};

// One run's machine: the stack, the locals, the sort buffers, the queue and the count of operations.
class Machine
{
public:
  Machine(Guest& g, const Package& p, u32 block, u32 module) : m_g(g), m_p(p), m_block(block), m_module(module)
  {
    m_machines = g.Read32(module + net::MACHINES);
    m_count = std::min<u32>(g.Read8(module + net::MACHINE_COUNT), SEATS);
    m_runs = g.Read32(block + RUNS);
    m_ranks.fill(SEATS);
    for (u32 n = 0; n < m_count; ++n)
      if (const u32 seat = g.Read8(module + R_BASE + R_ORDER + n); seat < SEATS)
        m_ranks[seat] = n;
  }

  // an event's second value (ko's credited seat, hit's other seat, out's cause): local 1 of the runs that follow
  void Second(u32 value) { m_second = value; }
  u32 Rank(u32 seat) const { return seat < SEATS ? m_ranks[seat] : SEATS; }
  u32 Progress(u32 seat) const { return m_g.Read32(MachineOf(seat) + game::MACHINE_PROGRESS); }

  u32 Count() const { return m_count; }
  u32 Ops() const { return m_ops; }
  Fault Faulted() const { return m_fault; }
  const std::vector<Action>& Queue() const { return m_queue; }
  u32 MachineOf(u32 seat) const { return m_machines + seat * game::MACHINE_SIZE; }
  bool Out(u32 seat) const { return net::out_test::Out(m_g, m_module, MachineOf(seat)); }
  void Fail(Fault f)
  {
    if (m_fault == FAULT_NONE)
      m_fault = f;
  }

  // Runs the code at `at` of rule `index` for its instance on `binding` (local 0), or with the `live` locals a wait
  // kept (`kept`): a condition's value, or true for a body that came to its end or to a wait.
  bool Run(u32 at, u32 index, u32 binding, const u32* kept = nullptr, u32 live = 0)
  {
    const Rule& rule = m_p.rules[index];
    m_locals.fill(0);
    m_locals[0] = binding;
    m_locals[1] = m_second;
    for (u32 i = 0; i < live && i < LOCALS; ++i)
      m_locals[i] = kept[i];
    m_index = index;
    m_binding = binding;
    m_waited = false;
    m_sp = 0;
    while (m_fault == FAULT_NONE)
    {
      if (at >= m_p.code.size())
        break;
      const u8 op = m_p.code[at];
      const u32 size = static_cast<u32>(OpSize(op));
      if (size == 0 || at + size > m_p.code.size())
        break;
      const u8* o = m_p.code.data() + at + 1;
      at += size;
      if (++m_ops > BUDGET)
      {
        Fail(FAULT_BUDGET);
        return false;
      }
      if (op == OP_END)
        return true;
      if (op == OP_RET)
        return Pop() != 0 && m_fault == FAULT_NONE;
      Do(op, o, rule, &at);
      if (m_waited)
        return m_fault == FAULT_NONE;
    }
    Fail(FAULT_CODE);
    return false;
  }

private:
  void Push(u32 v)
  {
    if (m_sp >= STACK)
      return Fail(FAULT_CODE);
    m_stack[m_sp++] = v;
  }
  u32 Pop()
  {
    if (m_sp == 0)
    {
      Fail(FAULT_CODE);
      return 0;
    }
    return m_stack[--m_sp];
  }
  static u32 U8(const u8* p) { return p[0]; }
  static u32 U16(const u8* p) { return u32(p[0]) << 8 | p[1]; }
  static u32 U32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
  bool IsSeat(u32 seat) const { return seat < m_count; }

  u32 SeatRead(u32 id, u32 seat)
  {
    if (!IsSeat(seat))
      return 0;
    const u32 m = MachineOf(seat);
    const u32 flags = m_g.Read32(m + game::MACHINE_FLAGS);
    const u32 progress = m_g.Read32(m + game::MACHINE_PROGRESS);
    switch (id)
    {
      case SEAT_RACING: return (flags & game::MACHINE_FLAG_FINISHED) == 0 && !Out(seat);
      case SEAT_OUT: return Out(seat);
      case SEAT_FINISHED: return (flags & game::MACHINE_FLAG_FINISHED) != 0;
      case SEAT_SPEED: return m_g.Read32(m + game::MACHINE_SPEED);
      case SEAT_ENERGY: return m_g.Read32(m + game::MACHINE_ENERGY);
      case SEAT_LAP: return m_g.Read32(progress + game::PROGRESS_LAPS);
      case SEAT_INDEX: return seat;
      case SEAT_BROKEN: return (flags & game::MACHINE_FLAG_BROKEN_DOWN) != 0;
      case SEAT_DESTROYED: return (flags & 0x40000000) != 0;
      case SEAT_DISTANCE: return m_g.Read32(progress + game::PROGRESS_DISTANCE);
      case SEAT_TEAM: return m_p.teams[seat];
      case SEAT_LAPLENGTH: return m_g.Read32(progress + game::PROGRESS_LAP_LENGTH);
      case SEAT_RESTORING: return m_g.Read16(m + M_RESTORING) != 0;
      case SEAT_SURFACE: return Surface(m_g.Read32(m + M_SURFACE));
      case SEAT_INPUTACCEL: return m_g.Read32(m + M_ACCEL);
      case SEAT_INPUTBRAKE: return m_g.Read32(m + M_BRAKE);
      case SEAT_INPUTSTEER: return m_g.Read32(m + M_STEER);
      case SEAT_INPUTSTRAFE: return m_g.Read32(m + M_STRAFE);
      case SEAT_INPUTPITCH: return m_g.Read32(m + M_PITCH);
      case SEAT_RACETIME:
      {
        const u32 done = m_g.Read32(m_block + SeatState(seat) + S_FINISH);
        return done != 0 ? done - 1 : m_g.Read32(m_module + game::RACE_CLOCK);
      }
      case SEAT_LAPTIME:
        return m_g.Read32(m_module + game::RACE_CLOCK) - m_g.Read32(m_block + SeatState(seat) + S_LAP_START);
      case SEAT_BESTLAP: return m_g.Read32(m_block + SeatState(seat) + S_BEST_LAP);
      case SEAT_MACHINE: return m_g.Read8(layout::RACE_MACHINES_AT + seat);
      case SEAT_ISCPU: return (m_p.players >> seat & 1) == 0;
      case SEAT_ISPLAYER: return m_p.players >> seat & 1;
      case SEAT_HITS: return m_g.Read32(m_block + SeatState(seat) + S_HITS);
      case SEAT_DAMAGETAKEN: return m_g.Read32(m_block + SeatState(seat) + S_DAMAGE);
      case SEAT_TOUCHING: return SetOfRow(m_g.Read32(m_block + TOUCH + 4 * seat));
      case SEAT_LAPDISTANCE: return m_g.Read32(progress + P_LAP_DISTANCE);
      case SEAT_CHECKPOINT: return m_g.Read32(progress + P_CHECKPOINT);
      case SEAT_CHECKPOINTFRACTION: return m_g.Read32(progress + P_FRACTION);
      case SEAT_LAPSTEPS: return m_g.Read32(progress + P_LAP_STEPS);
      case SEAT_WRONGWAY: return m_g.Read32(progress + P_WRONG_WAY) != 0;
      case SEAT_TOPSPEED: return m_g.Read32(m + M_TOP_SPEED);
      case SEAT_MAXENERGY: return Bits(static_cast<float>(m_g.Read16(m + M_MAX_ENERGY)));
      case SEAT_LASTHITENERGY: return m_g.Read32(m + M_LAST_HIT);
      case SEAT_FALLEN: return (flags & F_FALLEN) != 0;
      case SEAT_SPINNING: return (flags & F_SPIN) != 0;
      case SEAT_AIRBORNE: return (flags & F_AIRBORNE) != 0;
      case SEAT_BOOSTING: return (flags & F_BOOSTING) != 0;
      case SEAT_SIDEATTACK: return (flags & F_SIDE) != 0;
      case SEAT_INPIT: return (m_g.Read32(m + M_SURFACE) & 0x08000000) != 0;
      case SEAT_USEDPIT: return (m_g.Read32(m + M_CONTACT) & 1) != 0;
      case SEAT_STANDING: return Rank(seat) < SEATS ? Rank(seat) + 1 : 0;
      case SEAT_PLACE: return m_g.Read8(m_block + PLACES + seat);
      case SEAT_POINTS: return m_g.Read32(m_block + POINTS + 4 * seat);
      case SEAT_KOS: return m_g.Read8(m + M_KOS);
      case SEAT_GHOST: return m_g.Read32(m_block + NC + 4 * seat) == 0xFFFFFFFFu;
      default: Fail(FAULT_CODE); return 0;
    }
  }
  // a read of type vec: its three words, x first (none: zeros)
  void SeatVec(u32 id, u32 seat)
  {
    if (id != SEAT_POSITION && id != SEAT_FORWARD && id != SEAT_VELOCITY && id != SEAT_UP)
      return Fail(FAULT_CODE);
    if (!IsSeat(seat))
    {
      for (u32 k = 0; k < 3; ++k)
        Push(0);
      return;
    }
    const u32 m = MachineOf(seat);
    // km/h a metre a step (216) over the weight: the momentum's scale, one binary32 division
    const float scale = Real(Bits(216.0f / Real(m_g.Read32(m + M_WEIGHT))));
    for (u32 k = 0; k < 3; ++k)
    {
      switch (id)
      {
        case SEAT_POSITION: Push(m_g.Read32(m + game::MACHINE_POSITION_X + 4 * k)); break;
        case SEAT_FORWARD: Push(m_g.Read32(m + M_NOSE + M_ROW * k) ^ 0x80000000u); break;
        case SEAT_UP: Push(m_g.Read32(m + M_UP + M_ROW * k)); break;
        default: Push(Bits(Real(m_g.Read32(m + M_MOMENTUM + 4 * k)) * scale)); break;
      }
    }
  }

  static u32 Surface(u32 bits)
  {
    constexpr u32 KINDS[] = {0x08000000, 0x10000000, 0x04000000, 0x80000000, 0x20000000, 0x02000000, 0x40000000};
    for (u32 k = 0; k < 7; ++k)
      if (bits & KINDS[k])
        return k + 1;
    return 0;
  }
  u32 TeamWord(u32 seat, u32 word) const { return m_block + mode::TeamWord(m_p.teams[seat], word); }

  // xoshiro128**, its state in the block
  u32 Draw()
  {
    std::array<u32, 4> s;
    for (u32 k = 0; k < 4; ++k)
      s[k] = m_g.Read32(m_block + RANDOM + 4 * k);
    const u32 result = std::rotl(s[1] * 5, 7) * 9;
    const u32 t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = std::rotl(s[3], 11);
    for (u32 k = 0; k < 4; ++k)
      m_g.Write32(m_block + RANDOM + 4 * k, s[k]);
    return result;
  }

  // the instance waits `steps` steps (formats.md section 9: Wait): a free record takes it, with its `live` locals
  void Wait(u32 steps, u32 live, u32 kind, u32 condition, u32 resume)
  {
    if (Int(steps) <= 0)
      return;
    if (live > WAIT_LOCALS || live > m_p.rules[m_index].locals)
      return Fail(FAULT_CODE);
    for (u32 i = 0; i < modes::WAITS; ++i)
    {
      const u32 r = m_block + WAIT_RECORDS + i * WAIT;
      if (m_g.Read32(r) != 0)
        continue;
      m_g.Write32(r, m_index + 1);
      m_g.Write32(r + 4, m_binding);
      m_g.Write32(r + 8, kind | live << 8);
      m_g.Write32(r + 12, m_runs + steps);
      m_g.Write32(r + 16, steps);
      m_g.Write32(r + 20, resume);
      m_g.Write32(r + 24, condition);
      for (u32 k = 0; k < live; ++k)
        m_g.Write32(r + 28 + 4 * k, m_locals[k]);
      const u32 bits = m_block + WAITING + 4 * m_index;
      m_g.Write32(bits, m_g.Read32(bits) | Bit(m_binding));
      m_waited = true;
      return;
    }
    Fail(FAULT_WAITS);
  }
  u32 RaceRead(u32 id)
  {
    switch (id)
    {
      case RACE_STEPS: return m_g.Read32(m_module + game::RACE_CLOCK);
      case RACE_STARTED: return static_cast<s16>(m_g.Read16(m_module + game::PHASE)) >= s16{game::PHASE_RACE};
      case RACE_SEATS: return m_count;
      case RACE_LAPS: return m_g.Read32(layout::RACE_LAPS_AT);
      default: Fail(FAULT_CODE); return 0;
    }
  }

  // b, then a off the stack; a op b on it
  template <typename F>
  void Two(F f)
  {
    const u32 b = Pop(), a = Pop();
    Push(f(a, b));
  }
  template <typename F>
  void One(F f)
  {
    Push(f(Pop()));
  }
  template <typename F>
  void Reals(F f)
  {
    Two([&](u32 a, u32 b) { return Bits(f(Real(a), Real(b))); });
  }
  template <typename F>
  void Compare(F f)
  {
    Two([&](u32 a, u32 b) { return f(Real(a), Real(b)) ? 1u : 0u; });
  }
  template <typename F>
  void Compare32(F f)
  {
    Two([&](u32 a, u32 b) { return f(Int(a), Int(b)) ? 1u : 0u; });
  }
  void Jump(u32 to, u32* at)
  {
    if (to >= m_p.code.size())
      return Fail(FAULT_CODE);
    *at = to;
  }

  void Do(u8 op, const u8* o, const Rule& rule, u32* at)
  {
    switch (op)
    {
      case OP_PUSH: return Push(U32(o));
      case OP_POP: Pop(); return;
      case OP_DUP: { const u32 v = Pop(); Push(v); return Push(v); }
      case OP_LOADLOCAL: return U8(o) < rule.locals ? Push(m_locals[U8(o)]) : Fail(FAULT_CODE);
      case OP_STORELOCAL:
        if (U8(o) >= rule.locals)
          return Fail(FAULT_CODE);
        m_locals[U8(o)] = Pop();
        return;
      case OP_LOADGLOBAL:
        return U16(o) < m_p.globals.size() ? Push(m_g.Read32(m_block + GLOBALS + 4 * U16(o))) : Fail(FAULT_CODE);
      case OP_STOREGLOBAL:
        if (U16(o) >= m_p.globals.size())
          return Fail(FAULT_CODE);
        m_g.Write32(m_block + GLOBALS + 4 * U16(o), Pop());
        return;
      case OP_LOADSEATVAR:
      {
        const u32 seat = Pop();
        if (U16(o) >= m_p.seat_words.size())
          return Fail(FAULT_CODE);
        return Push(IsSeat(seat) ? m_g.Read32(m_block + SeatWord(seat, U16(o))) : 0);
      }
      case OP_STORESEATVAR:
      {
        const u32 value = Pop(), seat = Pop();
        if (U16(o) >= m_p.seat_words.size())
          return Fail(FAULT_CODE);
        if (IsSeat(seat))
          m_g.Write32(m_block + SeatWord(seat, U16(o)), value);
        return;
      }
      case OP_PARAM: return U8(o) < m_p.params.size() ? Push(Word(m_p.params[U8(o)])) : Fail(FAULT_CODE);
      case OP_SEATREAD:
        return U8(o) == SEAT_POSITION || U8(o) == SEAT_FORWARD || U8(o) == SEAT_VELOCITY || U8(o) == SEAT_UP
                   ? SeatVec(U8(o), Pop())
                   : Push(SeatRead(U8(o), Pop()));
      case OP_RACEREAD: return Push(RaceRead(U8(o)));
      case OP_LOADTEAMVAR:
      {
        const u32 seat = Pop();
        if (U16(o) >= m_p.team_words.size())
          return Fail(FAULT_CODE);
        return Push(IsSeat(seat) ? m_g.Read32(TeamWord(seat, U16(o))) : 0);
      }
      case OP_STORETEAMVAR:
      {
        const u32 value = Pop(), seat = Pop();
        if (U16(o) >= m_p.team_words.size())
          return Fail(FAULT_CODE);
        if (IsSeat(seat))
          m_g.Write32(TeamWord(seat, U16(o)), value);
        return;
      }
      case OP_IADD: return Two([](u32 a, u32 b) { return a + b; });
      case OP_ISUB: return Two([](u32 a, u32 b) { return a - b; });
      case OP_IMUL: return Two([](u32 a, u32 b) { return a * b; });
      case OP_IDIV:
        return Two([](u32 a, u32 b) {
          return b == 0 ? 0u : (Int(a) == std::numeric_limits<s32>::min() && Int(b) == -1) ? a : Word(Int(a) / Int(b));
        });
      case OP_IMOD:
        return Two([](u32 a, u32 b) { return b == 0 || Int(b) == -1 ? 0u : Word(Int(a) % Int(b)); });
      case OP_INEG: return One([](u32 a) { return 0u - a; });
      case OP_FADD: return Reals([](float a, float b) { return a + b; });
      case OP_FSUB: return Reals([](float a, float b) { return a - b; });
      case OP_FMUL: return Reals([](float a, float b) { return a * b; });
      case OP_FDIV: return Reals([](float a, float b) { return a / b; });
      case OP_FNEG: return One([](u32 a) { return a ^ 0x80000000u; });
      case OP_FSQRT: return One([](u32 a) { return Bits(GameSqrt(Real(a))); });
      case OP_ITOF: return One([](u32 a) { return Bits(static_cast<float>(Int(a))); });
      case OP_FLOOR: return One([](u32 a) { return ToInt(std::floor(Real(a))); });
      case OP_CEIL: return One([](u32 a) { return ToInt(std::ceil(Real(a))); });
      case OP_ROUND: return One([](u32 a) { return ToInt(std::round(Real(a))); });
      case OP_TRUNC: return One([](u32 a) { return ToInt(std::trunc(Real(a))); });
      case OP_IMIN: return Two([](u32 a, u32 b) { return Word(std::min(Int(a), Int(b))); });
      case OP_IMAX: return Two([](u32 a, u32 b) { return Word(std::max(Int(a), Int(b))); });
      case OP_IABS: return One([](u32 a) { return Int(a) < 0 ? 0u - a : a; });
      case OP_FMIN: return Two([](u32 a, u32 b) { return Real(b) < Real(a) ? b : a; });
      case OP_FMAX: return Two([](u32 a, u32 b) { return Real(b) > Real(a) ? b : a; });
      case OP_FABS: return One([](u32 a) { return a & 0x7FFFFFFFu; });
      case OP_IEQ: return Compare32([](s32 a, s32 b) { return a == b; });
      case OP_INE: return Compare32([](s32 a, s32 b) { return a != b; });
      case OP_ILT: return Compare32([](s32 a, s32 b) { return a < b; });
      case OP_ILE: return Compare32([](s32 a, s32 b) { return a <= b; });
      case OP_IGT: return Compare32([](s32 a, s32 b) { return a > b; });
      case OP_IGE: return Compare32([](s32 a, s32 b) { return a >= b; });
      case OP_FEQ: return Compare([](float a, float b) { return a == b; });
      case OP_FNE: return Compare([](float a, float b) { return a != b; });
      case OP_FLT: return Compare([](float a, float b) { return a < b; });
      case OP_FLE: return Compare([](float a, float b) { return a <= b; });
      case OP_FGT: return Compare([](float a, float b) { return a > b; });
      case OP_FGE: return Compare([](float a, float b) { return a >= b; });
      case OP_NOT: return One([](u32 a) { return a == 0 ? 1u : 0u; });
      case OP_JUMP: return Jump(U32(o), at);
      case OP_JUMPIFZERO:
        if (Pop() == 0)
          Jump(U32(o), at);
        return;
      case OP_JUMPIFNOTZERO:
        if (Pop() != 0)
          Jump(U32(o), at);
        return;
      case OP_WAIT: return Wait(Pop(), U8(o), U8(o + 1), U32(o + 2), *at);
      case OP_SEATBIT: return One([&](u32 a) { return IsSeat(a) ? 1u << a : 0u; });
      case OP_ALLSEATS: return Push(m_count >= 32 ? ~0u : (1u << m_count) - 1);
      case OP_BITAND: return Two([](u32 a, u32 b) { return a & b; });
      case OP_BITOR: return Two([](u32 a, u32 b) { return a | b; });
      case OP_BITANDNOT: return Two([](u32 a, u32 b) { return a & ~b; });
      case OP_RANDOM:
      {
        const u32 n = Pop();
        return Push(Int(n) <= 0 ? 0u : static_cast<u32>((u64{Draw()} * n) >> 32));
      }
      case OP_RANDOMREAL: return Push(std::bit_cast<u32>(static_cast<float>(Draw() >> 8) * 0x1p-24f));
      case OP_SORTBEGIN:
        if (U8(o) >= SORTS)
          return Fail(FAULT_CODE);
        m_sorts[U8(o)].count = m_sorts[U8(o)].next = 0;
        return;
      case OP_SORTADD: return SortAdd(U8(o), U8(o + 1));
      case OP_SORTRUN: return SortRun(U8(o), U8(o + 1), U8(o + 2), U8(o + 3));
      case OP_SORTNEXT:
      {
        if (U8(o) >= SORTS)
          return Fail(FAULT_CODE);
        Sort& s = m_sorts[U8(o)];
        return Push(s.next < s.count ? s.entries[s.next++].seat : 0xFFFFFFFFu);
      }
      case OP_ACTION: return Queue(U8(o));
      default: return Fail(FAULT_CODE);
    }
  }

  void SortAdd(u32 buffer, u32 keys)
  {
    if (buffer >= SORTS || keys > MOST_KEYS || m_sorts[buffer].count >= SEATS)
      return Fail(FAULT_CODE);
    Sort::Entry e;
    e.seat = Pop();
    for (u32 k = keys; k-- > 0;)
      e.keys[k] = Pop();
    Sort& s = m_sorts[buffer];
    s.entries[s.count++] = e;
  }

  void SortRun(u32 buffer, u32 keys, u32 descending, u32 reals)
  {
    if (buffer >= SORTS || keys > MOST_KEYS)
      return Fail(FAULT_CODE);
    Sort& s = m_sorts[buffer];
    m_ops += 5 * s.count * keys;  // (modes.toml: 1 + 5 x entries x keys)
    if (m_ops > BUDGET)
      return Fail(FAULT_BUDGET);
    // -1 a before b, 1 after, 0 the same
    const auto order = [&](const Sort::Entry& a, const Sort::Entry& b) {
      for (u32 k = 0; k < keys; ++k)
      {
        int c = 0;
        if ((reals >> k) & 1)
        {
          const float x = Real(a.keys[k]), y = Real(b.keys[k]);
          if (std::isnan(x) || std::isnan(y))
          {
            if (std::isnan(x) != std::isnan(y))
              return std::isnan(x) ? 1 : -1;  // a NaN last, whichever the direction
            continue;
          }
          c = x < y ? -1 : y < x ? 1 : 0;
        }
        else
        {
          c = Int(a.keys[k]) < Int(b.keys[k]) ? -1 : Int(b.keys[k]) < Int(a.keys[k]) ? 1 : 0;
        }
        if ((descending >> k) & 1)
          c = -c;
        if (c != 0)
          return c;
      }
      return a.seat < b.seat ? -1 : b.seat < a.seat ? 1 : 0;
    };
    std::stable_sort(s.entries.begin(), s.entries.begin() + s.count,
                     [&](const Sort::Entry& a, const Sort::Entry& b) { return order(a, b) < 0; });
    s.next = 0;
  }

  void Queue(u32 id)
  {
    if (id >= ACTION_COUNT)
      return Fail(FAULT_CODE);
    Action a{static_cast<u8>(id), NONE, {}};
    const u32 cells = ActionCells(id);
    for (u32 k = cells; k-- > 1;)
      a.value[k - 1] = Pop();
    if (cells > 0)
      a.seat = Pop();
    if (m_queue.size() >= static_cast<size_t>(QUEUE))
      return Fail(FAULT_QUEUE);
    m_queue.push_back(a);
  }

  Guest& m_g;
  const Package& m_p;
  u32 m_block, m_module, m_machines = 0, m_count = 0, m_ops = 0, m_sp = 0, m_runs = 0;
  u32 m_index = 0, m_binding = NONE;  // the rule instance running
  u32 m_second = NONE;
  std::array<u32, SEATS> m_ranks{};    // a seat's place in the standing at the mode point (R+0xE), 0 first
  bool m_waited = false;              // its code came to a wait
  Fault m_fault = FAULT_NONE;
  std::array<u32, STACK> m_stack{};
  std::array<u32, LOCALS> m_locals{};
  std::array<Sort, SORTS> m_sorts{};
  std::vector<Action> m_queue;
};

// The step stamps: the block's every page changes at every run, so the undo log watches each from the race's first
// steps (undo_log.cpp: a page first seen changing later is a late page).
void Stamp(Guest& g, u32 block, u32 runs)
{
  g.Write32(block + RUNS, runs);
  for (u32 page = PAGE; page < BLOCK; page += PAGE)
    g.Write32(block + page, runs);
}

// The rule's condition for this event and binding: `when` on its edge (kept in the block, a bit a seat), `while`
// every step; both, both; neither, always.
bool Holds(Machine& m, Guest& g, const Rule& rule, u32 index, u32 block, u32 binding, bool edge)
{
  bool go = true;
  if (rule.when != NONE)
  {
    const bool now = m.Run(rule.when, index, binding);
    if (edge)
    {
      const u32 bit = Bit(binding);
      const u32 was = g.Read32(block + EDGES + 4 * index);
      g.Write32(block + EDGES + 4 * index, now ? was | bit : was & ~bit);
      go = now && (was & bit) == 0;
    }
    else
    {
      go = now;
    }
  }
  if (go && rule.during != NONE)
    go = m.Run(rule.during, index, binding);
  return go;
}

// Whether rule `index`'s instance on `binding` waits: then it does not fire, nor are its conditions tested.
bool Waiting(const Guest& g, u32 block, u32 index, u32 binding)
{
  return (g.Read32(block + WAITING + 4 * index) & Bit(binding)) != 0;
}

// The waits (the step's third part): every waiting instance in rule order, then seat order. A condition is tested
// from the step after the wait began: `abort when` true drops the rest of the instance's code, `restart when` true
// begins the wait again. A wait whose step has come is freed and its instance goes on from it, with its locals.
void Waits(Machine& m, Guest& g, const Package& p, u32 block)
{
  const u32 runs = g.Read32(block + RUNS);
  std::array<std::pair<u32, u32>, modes::WAITS> order;  // (rule x 32 + its seat's place, record)
  u32 count = 0;
  for (u32 i = 0; i < modes::WAITS; ++i)
  {
    const u32 r = block + WAIT_RECORDS + i * WAIT;
    if (const u32 rule = g.Read32(r); rule != 0)
      order[count++] = {(rule - 1) * 32 + std::countr_zero(Bit(g.Read32(r + 4))), r};
  }
  std::sort(order.begin(), order.begin() + count);
  for (u32 n = 0; n < count && m.Faulted() == FAULT_NONE; ++n)
  {
    const u32 r = order[n].second;
    const u32 index = g.Read32(r) - 1, binding = g.Read32(r + 4), kind = g.Read32(r + 8) & 0xFF;
    const u32 live = g.Read32(r + 8) >> 8, due = g.Read32(r + 12), length = g.Read32(r + 16);
    if (index >= p.rules.size())
      return m.Fail(FAULT_CODE);
    std::array<u32, WAIT_LOCALS> kept{};
    for (u32 k = 0; k < live && k < WAIT_LOCALS; ++k)
      kept[k] = g.Read32(r + 28 + 4 * k);
    const auto free = [&] {
      g.Write32(r, 0);
      const u32 bits = block + WAITING + 4 * index;
      g.Write32(bits, g.Read32(bits) & ~Bit(binding));
    };
    if (kind != 0 && due - length < runs && m.Run(g.Read32(r + 24), index, binding, kept.data(), live))
    {
      if (kind == 1)
      {
        free();
        continue;
      }
      g.Write32(r + 12, runs + length);
      continue;
    }
    if (static_cast<s32>(due - runs) > 0)
      continue;
    free();
    m.Run(g.Read32(r + 20), index, binding, kept.data(), live);
  }
}

bool Finite(u32 bits)
{
  return (bits & 0x7F800000) != 0x7F800000;
}

// What the events compare: every seat's state at the mode point (each a value the step holds).
struct Now
{
  std::array<u32, SEATS> flags{}, lap{}, checkpoint{}, energy{}, kos{}, restoring{}, wrong{}, surface{}, wall{};
  u32 out = 0;          // the seats out by the game's verdict (bit i)
  bool started = false;
};

Now ReadNow(Machine& m, Guest& g, u32 module)
{
  Now n;
  n.started = static_cast<s16>(g.Read16(module + game::PHASE)) >= s16{game::PHASE_RACE};
  for (u32 seat = 0; seat < m.Count(); ++seat)
  {
    const u32 machine = m.MachineOf(seat), progress = m.Progress(seat);
    n.flags[seat] = g.Read32(machine + game::MACHINE_FLAGS);
    n.lap[seat] = g.Read32(progress + game::PROGRESS_LAPS);
    n.checkpoint[seat] = g.Read32(progress + P_CHECKPOINT);
    n.energy[seat] = g.Read32(machine + game::MACHINE_ENERGY);
    n.kos[seat] = g.Read8(machine + M_KOS);
    n.restoring[seat] = g.Read16(machine + M_RESTORING);
    n.wrong[seat] = g.Read32(progress + P_WRONG_WAY) != 0;
    n.surface[seat] = g.Read32(machine + M_SURFACE);
    // the race map's "touched a wall": pushed by a wall this step, on the ground, not fallen
    const u32 contact = g.Read32(machine + M_CONTACT);
    n.wall[seat] = (n.flags[seat] & F_WALL) && (contact & 0x8) && !(contact & 0x10);
    n.out |= m.Out(seat) ? 1u << seat : 0;
  }
  return n;
}

// The state the next run compares with; the touching rows move on (the pair gate fills them again in the next step).
void Remember(Machine& m, Guest& g, u32 block, const Now& n)
{
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const u32 s = block + SeatState(seat);
    g.Write32(s + S_FLAGS, n.flags[seat]);
    g.Write32(s + S_LAP, n.lap[seat]);
    g.Write32(s + S_CHECKPOINT, n.checkpoint[seat]);
    g.Write32(s + S_ENERGY, n.energy[seat]);
    g.Write32(s + S_KOS, n.kos[seat]);
    g.Write32(s + S_RESTORING, n.restoring[seat]);
    g.Write32(s + S_WRONG, n.wrong[seat]);
    g.Write32(s + S_SURFACE, n.surface[seat]);
    g.Write32(s + S_WALL, n.wall[seat]);
    g.Write8(block + RANKS + seat, static_cast<u8>(m.Rank(seat)));
    g.Write32(block + TOUCH_PREV + 4 * seat, g.Read32(block + TOUCH + 4 * seat));
    g.Write32(block + TOUCH + 4 * seat, 0);
  }
  g.Write32(block + OUT_MASK, n.out);
  g.Write32(block + FLAGS, (g.Read32(block + FLAGS) & ~1u) | (n.started ? 1u : 0u));
}

// Tests (debug "modeTrace"): what a mode's words did, as text, one line a thing; R the run (the block's count, which a
// rollback puts back: a line of run k takes the place of every earlier line of runs k and later), then E a rule that
// fired (its label, seat, second), A an action of the queue (id, seat, cells), S a seat's state every tenth run and
// every run before GO, X the race's words and the results' records at the end. Nothing is read back: no bearing on
// the race.
std::FILE* TraceFile()
{
  static std::FILE* file = [] {
    for (const EmulatorSetting& given : TheOptions().debug.emulator)
      if (given.name == "FZGX_MODE_TRACE" && !given.value.empty())
        return std::fopen(given.value.c_str(), "w");
    return static_cast<std::FILE*>(nullptr);
  }();
  return file;
}

void TraceSeats(Machine& m, Guest& g, u32 block, u32 module, u32 runs, bool started, const Now& n)
{
  std::FILE* t = TraceFile();
  if (!t || (started && runs % 10 != 0))
    return;
  const u32 r = module + R_BASE, slot = g.Read8(r + R_SLOT);
  std::fprintf(t, "T %u clock %u rule %08x timer %u phase %d end %u\n", runs, g.Read32(module + game::RACE_CLOCK),
               g.Read32(r), g.Read16(r + R_TIMER), static_cast<s16>(g.Read16(module + game::PHASE)), g.Read32(block + END));
  for (u32 seat = 0; seat < m.Count(); ++seat)
  {
    const u32 a = m.MachineOf(seat), pr = m.Progress(seat), rec = r + R_RECORDS + R_RECORD * seat;
    std::fprintf(t,
                 "S %u %u was %08x flags %08x v %.1f e %.2f lap %d dist %.0f x %.1f y %.1f z %.1f p %.1f %.1f %.1f steer %.3f "
                 "accel %.2f brake %.2f restoring %u kos %u rank %u out %u place %u time %08x cb %u\n",
                 runs, seat, n.flags[seat], g.Read32(a + game::MACHINE_FLAGS), Real(g.Read32(a + game::MACHINE_SPEED)),
                 Real(g.Read32(a + game::MACHINE_ENERGY)), Int(g.Read32(pr + game::PROGRESS_LAPS)),
                 Real(g.Read32(pr + 0xF8)), Real(g.Read32(a + game::MACHINE_POSITION_X)),
                 Real(g.Read32(a + game::MACHINE_POSITION_X + 4)), Real(g.Read32(a + game::MACHINE_POSITION_X + 8)),
                 Real(g.Read32(a + M_MOMENTUM)), Real(g.Read32(a + M_MOMENTUM + 4)), Real(g.Read32(a + M_MOMENTUM + 8)),
                 Real(g.Read32(a + M_STEER)), Real(g.Read32(a + M_ACCEL)), Real(g.Read32(a + M_BRAKE)),
                 g.Read16(a + M_RESTORING), g.Read8(a + M_KOS), m.Rank(seat), m.Out(seat) ? 1 : 0,
                 g.Read8(rec + 0xE + slot), g.Read32(rec + 0x24 + 4 * slot),
                 g.Read32(a + M_CPU) ? g.Read8(g.Read32(a + M_CPU) + 0x32) : 0);
  }
  std::fflush(t);
}

// One occurrence of a discrete event: every rule on it in the file's order, its instance on `seat` (none: the rule's
// own) not waiting, its conditions holding; `second` is local 1 (ko's credited seat, hit's other, out's cause).
void Fire(Machine& m, Guest& g, const Package& p, u32 block, u32 event, u32 seat, u32 second = NONE)
{
  for (u32 i = 0; i < p.rules.size() && m.Faulted() == FAULT_NONE; ++i)
  {
    const Rule& rule = p.rules[i];
    if (rule.event != event || Waiting(g, block, i, seat))
      continue;
    m.Second(second);
    if (Holds(m, g, rule, i, block, seat, false))
    {
      if (std::FILE* t = TraceFile())
        std::fprintf(t, "E %u %s seat %d second %d\n", g.Read32(block + RUNS), rule.label.c_str(), static_cast<int>(seat),
                     static_cast<int>(second));
      m.Run(rule.body, i, seat);
    }
  }
  m.Second(NONE);
}

u32 OutCause(u32 flags)
{
  for (const auto& [bit, cause] : OUT_CAUSES)
    if (flags & bit)
      return cause;
  return 0;
}

// The step's discrete events, kind by kind in the language's order (language 6, modes.toml), each kind by seat.
void Events(Machine& m, Guest& g, const Package& p, u32 block, u32 module, const Now& n, u32 runs)
{
  const u32 count = m.Count(), clock = g.Read32(module + game::RACE_CLOCK);
  const auto was = [&](u32 seat, u32 at) { return g.Read32(block + SeatState(seat) + at); };
  const auto each = [&](u32 event, auto&& happened) {
    for (u32 seat = 0; seat < count && m.Faulted() == FAULT_NONE; ++seat)
      if (happened(seat))
        Fire(m, g, p, block, event, seat);
  };
  const auto rose = [&](u32 seat, u32 bit) { return (n.flags[seat] & bit) && !(was(seat, S_FLAGS) & bit); };
  const auto fell = [&](u32 seat, u32 bit) { return !(n.flags[seat] & bit) && (was(seat, S_FLAGS) & bit); };
  if (runs == 0)
    Fire(m, g, p, block, EVENT_RACESTART, NONE);
  if (n.started && !(g.Read32(block + FLAGS) & 1))
    Fire(m, g, p, block, EVENT_GO, NONE);
  const u32 newly = n.out & ~g.Read32(block + OUT_MASK);
  for (u32 seat = 0; seat < count && m.Faulted() == FAULT_NONE; ++seat)
    if (newly >> seat & 1)
    {
      g.Write32(block + SeatState(seat) + S_OUT_AT, clock + 1);
      Fire(m, g, p, block, EVENT_OUT, seat, OutCause(n.flags[seat]));
    }
  // ko: the victims whose contact KO began (flag 0x80 rose with M+0x593 = 60), the one seat whose skulls rose
  u32 risers = 0;
  for (u32 seat = 0; seat < count; ++seat)
    risers |= n.kos[seat] > was(seat, S_KOS) ? 1u << seat : 0;
  const u32 by = std::popcount(risers) == 1 ? static_cast<u32>(std::countr_zero(risers)) : NONE;
  for (u32 seat = 0; seat < count && m.Faulted() == FAULT_NONE; ++seat)
    if (rose(seat, F_BROKEN) && g.Read8(m.MachineOf(seat) + M_KO_WAIT) == 60)
      Fire(m, g, p, block, EVENT_KO, seat, by);
  each(EVENT_DESTROYED, [&](u32 seat) { return rose(seat, F_DESTROYED); });
  each(EVENT_FALL, [&](u32 seat) { return rose(seat, F_FALLEN) && !(n.flags[seat] & F_DESTROYED); });
  // hit: a pair's contact answered now and not the step before, by the lower seat, then the higher
  for (u32 a = 0; a < count && m.Faulted() == FAULT_NONE; ++a)
  {
    const u32 now = g.Read32(block + TOUCH + 4 * a), before = g.Read32(block + TOUCH_PREV + 4 * a);
    for (u32 b = a + 1; b < count && m.Faulted() == FAULT_NONE; ++b)
    {
      if (!(now & GameBit(b)) || (before & GameBit(b)))
        continue;
      for (const u32 s : {a, b})
        g.Write32(block + SeatState(s) + S_HITS, g.Read32(block + SeatState(s) + S_HITS) + 1);
      Fire(m, g, p, block, EVENT_HIT, a, b);
    }
  }
  // damaged: the energy fell while the seat was not boosting (a boost's cost is not damage); its sum kept
  each(EVENT_DAMAGED, [&](u32 seat) {
    const float now = Real(n.energy[seat]), before = Real(was(seat, S_ENERGY));
    if (!(now < before) || ((n.flags[seat] | was(seat, S_FLAGS)) & F_BOOSTING))
      return false;
    const u32 at = block + SeatState(seat) + S_DAMAGE;
    g.Write32(at, Bits(Real(g.Read32(at)) + (before - now)));
    return true;
  });
  each(EVENT_WALL, [&](u32 seat) { return n.wall[seat] && !was(seat, S_WALL); });
  each(EVENT_BOOST, [&](u32 seat) { return rose(seat, F_BOOSTING); });
  each(EVENT_PIT, [&](u32 seat) { return (n.surface[seat] & 0x08000000) && !(was(seat, S_SURFACE) & 0x08000000); });
  each(EVENT_CHECKPOINT, [&](u32 seat) { return n.checkpoint[seat] != was(seat, S_CHECKPOINT); });
  each(EVENT_LAP, [&](u32 seat) {
    if (!(Int(n.lap[seat]) > Int(was(seat, S_LAP))))
      return false;
    // the lap's steps, and the best: the mode's own count from the race clock
    const u32 st = block + SeatState(seat), steps = clock - g.Read32(st + S_LAP_START), best = g.Read32(st + S_BEST_LAP);
    if (n.started && (best == 0 || steps < best))
      g.Write32(st + S_BEST_LAP, steps);
    g.Write32(st + S_LAP_START, clock);
    return true;
  });
  each(EVENT_FINISH, [&](u32 seat) {
    if (!rose(seat, F_FINISHED))
      return false;
    g.Write32(block + SeatState(seat) + S_FINISH, clock + 1);
    return true;
  });
  // overtake: a now ahead of b, both racing, and behind it the run before
  for (u32 a = 0; a < count && m.Faulted() == FAULT_NONE; ++a)
  {
    for (u32 b = 0; b < count && m.Faulted() == FAULT_NONE; ++b)
    {
      const bool racing = a != b && !(n.out >> a & 1) && !(n.out >> b & 1) &&
                          !((n.flags[a] | n.flags[b]) & F_FINISHED);
      if (racing && m.Rank(a) < m.Rank(b) && g.Read8(block + RANKS + a) > g.Read8(block + RANKS + b))
        Fire(m, g, p, block, EVENT_OVERTAKE, a, b);
    }
  }
  each(EVENT_RESPAWNED, [&](u32 seat) { return was(seat, S_RESTORING) > 20 && n.restoring[seat] <= 20; });
  each(EVENT_LAND, [&](u32 seat) { return fell(seat, F_AIRBORNE); });
  each(EVENT_BOOSTEND, [&](u32 seat) { return fell(seat, F_BOOSTING); });
  each(EVENT_WRONGWAY, [&](u32 seat) { return n.wrong[seat] && !was(seat, S_WRONG); });
}

u32 Members(const Package& p, u32 team, u32 count)
{
  u32 set = 0;
  for (u32 seat = 0; seat < count; ++seat)
    set |= p.teams[seat] == team ? 1u << seat : 0;
  return set;
}

// The queue, in its order, into the block and the machines; `end` is set by end_race (the end clause runs after).
void Apply(Machine& m, Guest& g, const Package& p, u32 block, u32 module, bool* end)
{
  const u32 count = m.Count();
  const auto mask = [&](u32 at, u32 seat, bool on) {
    g.Write32(block + at, on ? g.Read32(block + at) | 1u << seat : g.Read32(block + at) & ~(1u << seat));
  };
  const auto pairs = [&](u32 a, u32 b) {
    for (u32 i = 0; i < count; ++i)
      for (u32 j = 0; j < count; ++j)
        if (i != j && (a >> i & 1) && (b >> j & 1))
        {
          g.Write32(block + NC + 4 * i, g.Read32(block + NC + 4 * i) | GameBit(j));
          g.Write32(block + NC + 4 * j, g.Read32(block + NC + 4 * j) | GameBit(i));
        }
  };
  for (const Action& a : m.Queue())
  {
    if (std::FILE* t = TraceFile())
      std::fprintf(t, "A %u id %u seat %d %08x %08x %08x\n", g.Read32(block + RUNS), a.id, static_cast<int>(a.seat),
                   a.value[0], a.value[1], a.value[2]);
    // the race's words first: they name no seat
    switch (a.id)
    {
      case ACTION_ENDRACE: *end = true; continue;
      case ACTION_STOPALL:
        g.Write32(module + R_BASE, g.Read32(module + R_BASE) | RULE_STOP);
        continue;
      case ACTION_PADSBACK: g.Write32(block + HANDED, 0); continue;
      case ACTION_NOCONTACT: pairs(a.seat, a.value[0]); continue;
      case ACTION_TEAMNOCONTACT:
      {
        const u32 team = Members(p, a.seat, count);
        pairs(team, team);
        for (u32 i = 0; i < count; ++i)
          for (u32 j = 0; j < count; ++j)
            if (i != j && (team >> i & 1) && (team >> j & 1))
              g.Write32(block + TEAM_NC + 4 * i, g.Read32(block + TEAM_NC + 4 * i) | GameBit(j));
        continue;
      }
      default: break;
    }
    const u32 seat = a.seat;
    if (seat >= count)
      continue;
    const u32 record = block + RECORDS + seat * RECORD, machine = m.MachineOf(seat);
    const u32 flags = g.Read32(machine + game::MACHINE_FLAGS);
    const bool finished = (flags & F_FINISHED) != 0;
    switch (a.id)
    {
      case ACTION_SEATSPEED:
      {
        if (!Finite(a.value[0]))
          return m.Fail(FAULT_REAL);
        const float v = std::clamp(Real(a.value[0]), 0.0f, MOST_SPEED);
        g.Write32(record, g.Read32(record) | HELD);
        g.Write32(record + 12, std::bit_cast<u32>(v + 0.0f));  // (-0 as 0)
        break;
      }
      case ACTION_ELIMINATE:
      case ACTION_RETIRE:
        // retired and the seat's own Restore cleared in one write (the verdict would hide 0x08000000 behind 0x1)
        mask(RESPAWN, seat, false);
        if (!finished)
          g.Write32(machine + game::MACHINE_FLAGS, (flags & ~game::MACHINE_FLAG_RESTORE) | game::MACHINE_FLAG_RETIRED);
        break;
      case ACTION_EXPLODE:
        if (!finished && g.Read16(machine + M_RESTORING) == 0 && !(g.Read32(machine + M_CONTACT) & 0x10) &&
            !(flags & F_DESTROYED))
          g.Write32(record, g.Read32(record) | EXPLODE);
        break;
      case ACTION_RESPAWNS: mask(RESPAWN, seat, true); break;
      case ACTION_RESPAWNSOFF: mask(RESPAWN, seat, false); break;
      case ACTION_HANDTOCPU: mask(HANDED, seat, true); break;
      case ACTION_CPUMODE:
      {
        // story action 14 (mode-open-words 2), on a CPU's seat or one handed to the CPU (a mode writes machine flags
        // the stand-in would let through); never 18, which draws the shared generator 0x801A63C0; never the rock
        // dodgers 5-8 on the courses whose hazards run outside the step (37, 38, 41)
        const s32 n = Int(a.value[0]);
        const u32 course = g.Read32(RACE_COURSE_AT);
        const u32 cb = g.Read32(machine + M_CPU);
        const bool cpu = !(p.players >> seat & 1) || (g.Read32(block + HANDED) >> seat & 1);
        if (!cpu || n < 0 || n > 33 || n == 18 || cb == 0 ||
            (n >= 5 && n <= 8 && (course == 37 || course == 38 || course == 41)))
          break;
        g.Write32(cb, n == 0 ? g.Read32(cb) & ~1u : g.Read32(cb) | 1u);
        g.Write8(cb + 0x32, static_cast<u8>(n));
        break;
      }
      case ACTION_SEATPLACE: g.Write8(block + PLACES + seat, static_cast<u8>(std::clamp<s32>(Int(a.value[0]), 0, SEATS))); break;
      case ACTION_AWARD: g.Write32(block + POINTS + 4 * seat, g.Read32(block + POINTS + 4 * seat) + a.value[0]); break;
      case ACTION_SEATENERGY:
      {
        if (!Finite(a.value[0]))
          return m.Fail(FAULT_REAL);
        const float most = static_cast<float>(g.Read16(machine + M_MAX_ENERGY)) * 1.01f;
        g.Write32(machine + game::MACHINE_ENERGY, Bits(std::clamp(Real(a.value[0]), 0.0f, most) + 0.0f));
        break;
      }
      case ACTION_BOOST: g.Write32(record, g.Read32(record) | BOOST); break;
      case ACTION_PUSH:
      case ACTION_SEATVELOCITY:
      {
        if (!Finite(a.value[0]) || !Finite(a.value[1]) || !Finite(a.value[2]))
          return m.Fail(FAULT_REAL);
        // km/h into momentum: x W / 216, one binary32 division and a product a component
        const float k = Real(g.Read32(machine + M_WEIGHT)) / 216.0f;
        const u32 move = block + MOVES + 16 * seat;
        const bool push = a.id == ACTION_PUSH, adding = push && (g.Read32(move) & 1);
        for (u32 c = 0; c < 3; ++c)
        {
          const float v = Real(a.value[c]) * k;
          g.Write32(move + 4 + 4 * c, Bits(adding ? Real(g.Read32(move + 4 + 4 * c)) + v : v));
        }
        g.Write32(move, push ? (g.Read32(move) & 2 ? 2u : 1u) : 2u);
        break;
      }
      case ACTION_FREEZE: mask(FROZEN, seat, true); break;
      case ACTION_RELEASE: mask(FROZEN, seat, false); break;
      case ACTION_GHOST: g.Write32(block + NC + 4 * seat, 0xFFFFFFFFu); break;
      case ACTION_GHOSTOFF: g.Write32(block + NC + 4 * seat, g.Read32(block + TEAM_NC + 4 * seat)); break;
      case ACTION_DANGERZONE:
      {
        const u32 d = block + DANGER + 16 * seat;
        if (!Finite(a.value[0]) || !Finite(a.value[1]))
          return m.Fail(FAULT_REAL);
        g.Write32(d, a.value[0]);
        g.Write32(d + 4, a.value[1]);
        g.Write32(d + 8, NONE);
        g.Write32(d + 12, DANGER_ON | static_cast<u32>(std::clamp<s32>(Int(a.value[2]), 1, 9)) << 8);
        break;
      }
      case ACTION_DANGERZONEOFF: g.Write32(block + DANGER + 16 * seat + 12, 0); g.Write32(block + DANGER + 16 * seat + 8, NONE); break;
      default: break;
    }
  }
}

// The end clause's order held in the standing (the ranking pass rewrites it every step; the mode point comes after
// it): R+0xE, R+0x2C, P+0x115, CM+0x23C, CM+0x21C from the block's END_ORDER.
void HoldOrder(Guest& g, u32 block, u32 module)
{
  const u32 machines = g.Read32(module + net::MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + net::MACHINE_COUNT), SEATS);
  const u32 r = module + R_BASE, cm = module + CM_BASE;
  for (u32 n = 0; n < count; ++n)
  {
    const u32 seat = g.Read8(block + END_ORDER + n);
    if (seat >= count)
      continue;
    const u32 progress = g.Read32(machines + seat * game::MACHINE_SIZE + game::MACHINE_PROGRESS);
    g.Write8(r + R_ORDER + n, static_cast<u8>(seat));
    g.Write32(r + R_ORDER_P + 4 * n, progress);
    g.Write8(progress + P_STANDING, static_cast<u8>(n));
    g.Write8(cm + CM_ORDER + n, static_cast<u8>(seat));
    g.Write8(cm + CM_RANK + seat, static_cast<u8>(n));
    // a seat the mode placed keeps its place in the results, when the game books its out after the end (a seat
    // eliminated in the run that ended the race)
    const u32 place = r + R_RECORDS + R_RECORD * seat + 0xE + g.Read8(r + R_SLOT);
    if (g.Read8(block + PLACES + seat) != 0 && g.Read8(place) == 0xFE)
      g.Write8(place, static_cast<u8>(n));
  }
}

// The end clause (mode-conflicts-answers 4.3): the mode's order (the seats it placed by their places, then every other
// seat by the standing) into the standing and the results' records; a seat still racing gets its place, the race
// time and what fn_1_2E928 would have written, so no row stays RUNNING; an out seat keeps DISQUALIFIED (0xFE) unless
// the mode placed it (a knockout's cut seats: their place and the time they went out); a finished seat keeps its time. Points into the record's +0x4 and this race's +0x9. The end word 2: the race is over
// at this step's boundary (raceend.py), and every later run holds the order.
void EndClause(Machine& m, Guest& g, u32 block, u32 module)
{
  const u32 count = m.Count(), r = module + R_BASE, clock = g.Read32(module + game::RACE_CLOCK);
  std::array<u32, SEATS> order{};
  u32 n = 0;
  for (u32 place = 1; place <= SEATS; ++place)
    for (u32 seat = 0; seat < count; ++seat)
      if (g.Read8(block + PLACES + seat) == place)
        order[n++] = seat;
  for (u32 rank = 0; rank < count; ++rank)
    for (u32 seat = 0; seat < count; ++seat)
      if (m.Rank(seat) == rank && g.Read8(block + PLACES + seat) == 0)
        order[n++] = seat;
  for (u32 seat = 0; seat < count && n < count; ++seat)   // (a seat the standing does not name, last)
    if (std::find(order.begin(), order.begin() + n, seat) == order.begin() + n)
      order[n++] = seat;
  const u32 slot = g.Read8(r + R_SLOT);
  const auto time = [](u32 steps) {
    const u32 ms = static_cast<u32>(u64{steps} * 1000 / 60);
    return (ms / 60000 & 0xFF) << 20 | (ms / 1000 % 60) << 12 | ms % 1000;
  };
  for (u32 k = 0; k < count; ++k)
  {
    const u32 seat = order[k], machine = m.MachineOf(seat), record = r + R_RECORDS + R_RECORD * seat;
    g.Write8(block + END_ORDER + k, static_cast<u8>(seat));
    const u32 state = g.Read8(record + 0xE + slot), out_at = g.Read32(block + SeatState(seat) + S_OUT_AT);
    const bool placed = g.Read8(block + PLACES + seat) != 0;
    if (state == 0xFF || (state == 0xFE && placed))
    {
      // a seat racing: the race's time; a seat the mode put out and placed: its place and the time it went out
      g.Write8(record + 0xE + slot, static_cast<u8>(k));
      g.Write32(record + 0x24 + 4 * slot, time(state == 0xFE && out_at != 0 ? out_at - 1 : clock));
      g.Write8(record + 0x13 + slot, static_cast<u8>(std::min<u32>(g.Read8(machine + M_KOS), 30)));
      const float top = Real(g.Read32(machine + M_TOP_SPEED));
      g.Write16(record + 0x18 + 2 * slot, static_cast<u16>(top > 9999.0f ? 9999 : top > 0.0f ? static_cast<u32>(top) : 0));
    }
    else if (state != 0xFE)
    {
      g.Write8(record + 0xE + slot, static_cast<u8>(k));
    }
    const u32 points = g.Read32(block + POINTS + 4 * seat);
    if (points != 0)
    {
      g.Write16(record + 0x4, static_cast<u16>(std::min<u32>(points, 0xFFFF)));
      g.Write8(record + 0x9 + slot, static_cast<u8>(std::min<u32>(points, 0xFF)));
    }
  }
  HoldOrder(g, block, module);
  g.Write32(block + END, 2);
}

// After the queue: the seats' own Restore follows the respawns words (never on a finished seat, never cleared in a
// restore's wreck phase: mode-open-words 1.4), the no-contact rows' OR, the danger zones, the rolling start's hold,
// the clock limit's timer and its end, and the end clause when the run asked for it.
void After(Machine& m, Guest& g, const Package& p, u32 block, u32 module, const Now& n, bool end)
{
  const u32 count = m.Count(), respawn = g.Read32(block + RESPAWN);
  u32 any = 0;
  for (u32 seat = 0; seat < count; ++seat)
  {
    const u32 machine = m.MachineOf(seat), flags = g.Read32(machine + game::MACHINE_FLAGS);
    const u32 restoring = g.Read16(machine + M_RESTORING);
    if (!(flags & F_FINISHED) && !(flags & F_RETIRED))
    {
      if ((respawn >> seat & 1) && !(flags & F_RESTORE))
        g.Write32(machine + game::MACHINE_FLAGS, flags | F_RESTORE);
      else if (!(respawn >> seat & 1) && (flags & F_RESTORE) && restoring <= 20)
        g.Write32(machine + game::MACHINE_FLAGS, flags & ~F_RESTORE);
    }
    any |= g.Read32(block + NC + 4 * seat);
    // the danger zone (story chapter 6): armed at `arm`, under `below` a count from 60 x seconds + 60, at 60 explode
    const u32 d = block + DANGER + 16 * seat, state = g.Read32(d + 12);
    if ((state & DANGER_ON) && n.started && !(flags & (F_FINISHED | F_RETIRED)) && !(n.out >> seat & 1) &&
        restoring == 0)
    {
      const float speed = Real(g.Read32(machine + game::MACHINE_SPEED));
      u32 next = state;
      s32 left = Int(g.Read32(d + 8));
      if (!(state & DANGER_ARMED) && speed >= Real(g.Read32(d + 4)))
        next |= DANGER_ARMED;
      if ((next & DANGER_ARMED) && left < 0 && !(state & DANGER_DONE) && speed < Real(g.Read32(d)))
        left = static_cast<s32>(60 * (state >> 8 & 0xFF) + 60);
      if (left >= 0)
        left -= 1;
      if (left == 60 && !(state & DANGER_DONE))
      {
        next |= DANGER_DONE;
        if (!(flags & F_DESTROYED) && !(g.Read32(machine + M_CONTACT) & 0x10))
          g.Write32(block + RECORDS + seat * RECORD, g.Read32(block + RECORDS + seat * RECORD) | EXPLODE);
      }
      g.Write32(d + 8, Word(left));
      g.Write32(d + 12, next);
    }
    // the rolling start's countdown: no start charge, never frozen (the stand-in holds the pedals, the CPU steers)
    if (p.rolling && !n.started)
    {
      g.Write32(machine + M_START_CHARGE, 0);
      g.Write32(machine + game::MACHINE_FLAGS, g.Read32(machine + game::MACHINE_FLAGS) & ~F_FROZEN);
    }
  }
  g.Write32(block + NC_ANY, any);
  // the clock limit: the timer the game shows, from the race clock, never 0 (its zero paths never run); at the limit
  // the race is over
  if (p.clock_limit != 0)
  {
    const u32 clock = g.Read32(module + game::RACE_CLOCK);
    g.Write16(module + R_BASE + R_TIMER,
              static_cast<u16>(std::clamp<std::int64_t>(static_cast<std::int64_t>(p.clock_limit) - clock, 1, 65535)));
    if (n.started && clock >= p.clock_limit)
      end = true;
  }
  if (end && m.Faulted() == FAULT_NONE)
    EndClause(m, g, block, module);
}

// The mode's first run (the first step after the start gate): the setup words that act on the race from its start.
void Begin(Machine& m, Guest& g, const Package& p, u32 block, u32 module)
{
  const u32 count = m.Count();
  const u32 all = count >= 32 ? ~0u : (1u << count) - 1;
  if (p.contact_off)
    for (u32 seat = 0; seat < count; ++seat)
      g.Write32(block + NC + 4 * seat, 0xFFFFFFFFu);
  if (p.restore.value_or(false))
    g.Write32(block + RESPAWN, all);
  g.Write8(module + STORY_LEVEL, 0);   // (the CPU modes' difficulty: never another game's story state)
  g.Write32(block + CLOCK, p.clock_limit);   // (the image's timer site reads it)
  if (p.clock_limit != 0)
  {
    const u32 r = module + R_BASE;
    g.Write32(r, g.Read32(r) | RULE_TIMED);
    g.Write16(r + R_TIME_LIMIT, static_cast<u16>(std::min<u32>(p.clock_limit, 65535)));
    for (u32 at = R_BONUS; at < R_BONUS_END; ++at)
      g.Write8(r + at, 0);
    g.Write16(r + R_TIMER, static_cast<u16>(std::min<u32>(p.clock_limit, 65535)));
  }
  if (p.rolling)
  {
    // fn_1_23500's writes but its reset (rolling-start.md 3): momentum along the nose at v, base speed v / 200,
    // driven, settled, no start charge, the read-out v; and not frozen
    const float v = Real(*p.rolling);
    for (u32 seat = 0; seat < count; ++seat)
    {
      const u32 machine = m.MachineOf(seat);
      const float amount = static_cast<float>(static_cast<double>(v) / 216.0 * Real(g.Read32(machine + M_WEIGHT)));
      for (u32 k = 0; k < 3; ++k)
        g.Write32(machine + M_MOMENTUM + 4 * k, Bits(Real(g.Read32(machine + M_NOSE + M_ROW * k)) * -amount));
      g.Write32(machine + M_BASE_SPEED, Bits(v / 200.0f));
      g.Write32(machine + game::MACHINE_FLAGS, (g.Read32(machine + game::MACHINE_FLAGS) | F_DRIVEN) & ~F_FROZEN);
      g.Write8(machine + M_SETTLED, 90);
      g.Write32(machine + M_START_CHARGE, 0);
      g.Write32(machine + game::MACHINE_SPEED, *p.rolling);
    }
    g.Write32(block + FLAGS, g.Read32(block + FLAGS) | 2);
  }
}
}  // namespace

void Run(Guest& g, const Package& p, u32 block, u32 module)
{
  if (const u32 end = g.Read32(block + END); end != 0)
  {
    // the race is over (the image's race-end clause has it); a race the mode ended keeps the mode's order
    if (end == 2)
      HoldOrder(g, block, module);
    return;
  }
  const FloatControl control;
  const u32 runs = g.Read32(block + RUNS);
  if (runs == 0)
  {
    for (size_t i = 0; i < p.globals.size(); ++i)
      g.Write32(block + GLOBALS + 4 * static_cast<u32>(i), Word(p.globals[i]));
    for (u32 seat = 0; seat < SEATS; ++seat)
      for (size_t w = 0; w < p.seat_words.size(); ++w)
        g.Write32(block + SeatWord(seat, static_cast<u32>(w)), Word(p.seat_words[w]));
    for (u32 team = 0; team < modes::TEAMS; ++team)
      for (size_t w = 0; w < p.team_words.size(); ++w)
        g.Write32(block + TeamWord(team, static_cast<u32>(w)), Word(p.team_words[w]));
    for (u32 k = 0; k < 4; ++k)
      g.Write32(block + RANDOM + 4 * k, p.seed[k]);
  }
  Stamp(g, block, runs + 1);
  Machine m(g, p, block, module);
  const Now n = ReadNow(m, g, module);
  if (runs == 0)
  {
    // the first run's events compare the step with itself (race_start; and out for a seat already out, as before)
    Remember(m, g, block, n);
    g.Write32(block + OUT_MASK, 0);
    g.Write32(block + FLAGS, 0);
    Begin(m, g, p, block, module);
  }
  // the records: the setup words' every step, no speed held until a rule holds it (a frozen seat's at 0); the CPU
  // drives the seats handed to it, and every seat through a rolling start's countdown, the pedals held
  const bool rolling = p.rolling.has_value() && !n.started;
  const u32 handed = g.Read32(block + HANDED), frozen = g.Read32(block + FROZEN);
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const u32 record = block + RECORDS + seat * RECORD;
    g.Write32(record, (p.pedals ? PEDALS : 0) | (p.boost_off ? BOOST_OFF : 0) | (frozen >> seat & 1 ? HELD : 0) |
                          ((handed >> seat & 1) || rolling ? HAND : 0) | (rolling ? ROLL : 0));
    g.Write32(record + 4, p.pedals ? p.accel : 0);
    g.Write32(record + 8, p.pedals ? p.brake : 0);
    g.Write32(record + 12, 0);
    g.Write32(block + MOVES + 16 * seat, 0);
  }
  // the discrete events, in the language's order
  Events(m, g, p, block, module, n, runs);
  // the waits whose time has come
  if (m.Faulted() == FAULT_NONE)
    Waits(m, g, p, block);
  // the continuous rules in the file's order (each seat: by seat)
  for (u32 i = 0; i < p.rules.size() && m.Faulted() == FAULT_NONE; ++i)
  {
    const Rule& rule = p.rules[i];
    if (rule.event == EVENT_EVERYSTEP)
    {
      if (!Waiting(g, block, i, NONE) && Holds(m, g, rule, i, block, NONE, true))
        m.Run(rule.body, i, NONE);
    }
    else if (rule.event == EVENT_EACHSEAT)
    {
      for (u32 seat = 0; seat < m.Count() && m.Faulted() == FAULT_NONE; ++seat)
      {
        if (!Waiting(g, block, i, seat) && Holds(m, g, rule, i, block, seat, true))
          m.Run(rule.body, i, seat);
      }
    }
  }
  bool end = false;
  if (m.Faulted() == FAULT_NONE)
    Apply(m, g, p, block, module, &end);
  if (m.Faulted() == FAULT_NONE)
    After(m, g, p, block, module, n, end);
  TraceSeats(m, g, block, module, runs, n.started && !end, n);
  Remember(m, g, block, n);
  g.Write32(block + OPS, m.Ops());
  g.Write32(block + OPS + 4, std::max(g.Read32(block + OPS + 4), m.Ops()));
  if (m.Faulted() != FAULT_NONE)
  {
    // a mode error: the race ends for everyone at this step, the results as they stand (D6)
    g.Write32(block + FAULT, m.Faulted());
    g.Write32(block + FAULT + 4, g.Read32(module + game::RACE_CLOCK));
    g.Write32(block + END, 1);
  }
}
}  // namespace fzgx::mode

namespace fzgx::mode
{
namespace
{
const char* FaultText(u32 fault)
{
  switch (fault)
  {
    case FAULT_BUDGET: return "the step's operations went past the budget";
    case FAULT_QUEUE: return "the step queued more actions than the api's";
    case FAULT_REAL: return "a real for the game is not finite";
    case FAULT_CODE: return "the code does what it may not";
    case FAULT_WAITS: return "more rules waited at once than the api's";
    default: return "the game had no room for the mode's block";
  }
}
}  // namespace

void Step(Guest& g)
{
  const boot::Match* m = boot::Get();
  const u32 block = g.Read32(layout::MODES + 4);
  if (!m || !m->game_mode || block == 0)
    return;
  Run(g, *m->game_mode, block, g.Read32(net::MODULE_POINTER));
  // the log, once a run of the game (a step run again says nothing new); the block holds what the race is
  static bool said = false;
  if (!said && g.Read32(block + END) == 1)
  {
    said = true;
    Error("fzgx: mode error: \"{}\" ({}) at step {}: {}; the race ends here for everyone", m->game_mode->name,
          m->game_mode->hash.substr(0, 8), g.Read32(block + FAULT + 4), FaultText(g.Read32(block + FAULT)));
  }
}

u32 Record(const Guest& g, u32 seat)
{
  const u32 block = g.Read32(layout::MODES + 4);
  return block == 0 || seat >= SEATS ? 0 : block + RECORDS + seat * RECORD;
}

u32 Move(const Guest& g, u32 seat)
{
  const u32 block = g.Read32(layout::MODES + 4);
  return block == 0 || seat >= SEATS ? 0 : block + MOVES + 16 * seat;
}

u32 RollSave(const Guest& g, u32 seat)
{
  const u32 block = g.Read32(layout::MODES + 4);
  return block == 0 || seat >= SEATS ? 0 : block + ROLL_SAVE + ROLL_SAVE_SIZE * seat;
}
}  // namespace fzgx::mode
