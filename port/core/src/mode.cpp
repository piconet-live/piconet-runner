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
#include "mode_hud.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
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

struct Action
{
  u8 id;
  u32 seat;
  u32 value;
};

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
  }

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
      default: Fail(FAULT_CODE); return 0;
    }
  }
  // a read of type vec: its three words, x first (none: zeros)
  void SeatVec(u32 id, u32 seat)
  {
    const u32 at = id == SEAT_POSITION ? game::MACHINE_POSITION_X : 0;
    if (at == 0)
      return Fail(FAULT_CODE);
    for (u32 k = 0; k < 3; ++k)
      Push(IsSeat(seat) ? m_g.Read32(MachineOf(seat) + at + 4 * k) : 0);
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
      case OP_SEATREAD: return U8(o) == SEAT_POSITION ? SeatVec(U8(o), Pop()) : Push(SeatRead(U8(o), Pop()));
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
    Action a{static_cast<u8>(id), 0, 0};
    if (ActionArguments(static_cast<u8>(id)) == 2)
      a.value = Pop();
    a.seat = Pop();
    // a display word writes the block's display page at once, as a variable is written: no game field, nothing a
    // read of the step sees, so it takes no place in the queue (a display on every seat each step fits)
    if (hud::IsDisplay(a.id))
    {
      if (a.seat < m_count)
        hud::Apply(m_g, m_block, a.id, a.seat, a.value);
      return;
    }
    if (m_queue.size() >= static_cast<size_t>(QUEUE))
      return Fail(FAULT_QUEUE);
    m_queue.push_back(a);
  }

  Guest& m_g;
  const Package& m_p;
  u32 m_block, m_module, m_machines = 0, m_count = 0, m_ops = 0, m_sp = 0, m_runs = 0;
  u32 m_index = 0, m_binding = NONE;  // the rule instance running
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

// The queue, in its order, into the block's records and the machines.
void Apply(Machine& m, Guest& g, u32 block)
{
  for (const Action& a : m.Queue())
  {
    if (a.seat >= m.Count())
      continue;
    const u32 record = block + RECORDS + a.seat * RECORD;
    if (a.id == ACTION_SEATSPEED)
    {
      if (!Finite(a.value))
        return m.Fail(FAULT_REAL);
      const float v = std::clamp(Real(a.value), 0.0f, MOST_SPEED);
      g.Write32(record, g.Read32(record) | HELD);
      g.Write32(record + 12, std::bit_cast<u32>(v + 0.0f));  // (-0 as 0)
    }
    else if (a.id == ACTION_ELIMINATE)
    {
      const u32 machine = m.MachineOf(a.seat);
      const u32 flags = g.Read32(machine + game::MACHINE_FLAGS);
      if ((flags & game::MACHINE_FLAG_FINISHED) == 0)
        g.Write32(machine + game::MACHINE_FLAGS,
                  (flags & ~game::MACHINE_FLAG_RESTORE) | game::MACHINE_FLAG_RETIRED);
    }
  }
}
}  // namespace

void Run(Guest& g, const Package& p, u32 block, u32 module)
{
  if (g.Read32(block + END) != 0)
    return;  // the race is over (the image's race-end clause has it)
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
  hud::BeginRun(g, block);
  Machine m(g, p, block, module);
  // the records: the setup words' every step, no speed held until a rule holds it
  for (u32 seat = 0; seat < SEATS; ++seat)
  {
    const u32 record = block + RECORDS + seat * RECORD;
    g.Write32(record, (p.pedals ? PEDALS : 0) | (p.boost_off ? BOOST_OFF : 0));
    g.Write32(record + 4, p.pedals ? p.accel : 0);
    g.Write32(record + 8, p.pedals ? p.brake : 0);
    g.Write32(record + 12, 0);
  }
  // the discrete events: the seats out since the last run, by seat, each rule on it in the file's order
  u32 out = 0;
  for (u32 seat = 0; seat < m.Count(); ++seat)
    out |= m.Out(seat) ? 1u << seat : 0;
  const u32 newly = out & ~g.Read32(block + OUT_MASK);
  g.Write32(block + OUT_MASK, out);
  for (u32 seat = 0; seat < m.Count() && m.Faulted() == FAULT_NONE; ++seat)
  {
    if ((newly >> seat & 1) == 0)
      continue;
    for (u32 i = 0; i < p.rules.size() && m.Faulted() == FAULT_NONE; ++i)
    {
      const Rule& rule = p.rules[i];
      if (rule.event == EVENT_OUT && !Waiting(g, block, i, seat) && Holds(m, g, rule, i, block, seat, false))
        m.Run(rule.body, i, seat);
    }
  }
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
  if (m.Faulted() == FAULT_NONE)
    Apply(m, g, block);
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
}  // namespace fzgx::mode
