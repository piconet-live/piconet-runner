// Per-machine rollback (lanes_partial.h): the guard, and "writes".
//
// The guard: the words outside a machine's records that a machine's task writes (read in the code;
// port/facts/rollback-state.tsv, the rows partial-guard-*). A put-back
// gives back a job's records and locked cache, not these, so
//   - a job that wrote one of them in its last run runs in every later run of the step (as a block copy does);
//   - the course objects' first words are also read on every machine's path (the mine walk, fn_1_163BC, reads every
//     mine's flags): once they differ from what a job found in the step's last run, it and every later job run.
// The other words are read only by the code that writes them (a generator's step, an effect's place in its pool, a
// sound command's), which a put-back job did not reach in its last run, so their values at a job's start do not
// decide it.
//
// "writes": the tracer that shows the list is whole. Main memory is kept as the batch found it; after each job every
// word that differs is noted unless it is in the job's machine's records, the game thread's stack below the task
// runner's frame (and that frame's return slot), or the task runner's node word: a line a word (O), and at the
// game's end a count by region.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanes_partial.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace fzgx_port::lanes::partial
{
namespace
{
const fzgx::Debug& D()
{
  return s_state.options.debug;
}

// The guard's words, by name (the study's table): main memory's, or the data window's (the mute routine's counts)
struct GuardWord
{
  const char* name;
  u32 address;
  u32 size;  // 1, 2 or 4
  bool window;
  bool pointer;  // the address holds a pointer, the word is at it + `offset`
  u32 offset;
};
constexpr GuardWord WORDS[GUARD_WORDS] = {
    // the effects' serial count (.bss 0x6C850, a halfword): every effect made, in either pool (fn_1_58F50, fn_1_59078)
    {"effects-serial", game::REL_BSS_AT + 0x6C850, 2, false, false, 0},
    // the effects' generator (.data 0x1D628: fn_1_584AC and the effects' own steps of it)
    {"effects-generator", game::REL_DATA_AT + 0x1D628, 4, false, false, 0},
    // the executable's generator (0x801A63C0)
    {"executable-generator", game::DOL_RNG, 4, false, false, 0},
    // the computer drivers' generator (.data 0x3D230)
    {"cpu-generator", game::REL_DATA_AT + 0x3D230, 4, false, false, 0},
    // the sound queue's next place ([0x801A6C80] + 0x443): every command pushed (fn_80067898, fn_80067344)
    {"sound-queue-next", game::SOUND_QUEUE, 1, false, true, 0x443},
    // the mute routine's counts (control block +32 dropped, +36 every command: mute.py)
    {"mute-dropped", table::CONTROL + 32, 4, true, false, 0},
    {"mute-commands", table::CONTROL + 36, 4, true, false, 0},
};
// The course objects the mine walk (fn_1_163BC) passes over: the course header's pointer at .bss 0x3BE0; in the header
// +0x54 the records, 0x40 bytes each, whose first word holds a mine's "hit" (0x20000000) and "spent" (0x40000000);
// the walk goes from the record [+0x50] to the one before [+0x48] (0x16700-0x16B68) and stops at the first that is
// not a mine (its collider [[record +8] +0xC] none, or without flag 0x4000 in its first word: 0x16718-0x16730), so
// it writes no other: the guard holds that run of mines (course data: found once a course)
constexpr u32 COURSE_HEADER = game::REL_BSS_AT + 0x3BE0, OBJECT_END = 0x48, OBJECT_FIRST = 0x50, OBJECTS = 0x54;
constexpr u32 OBJECT_SIZE = 0x40;
constexpr u32 MOST_OBJECTS = 4096;

bool InRam(const CPUState* cpu, u32 address, u32 size)
{
  return address >= table::RAM && address - table::RAM <= cpu->ram_size - size;
}

u32 Big(const u8* p, u32 size)
{
  u32 v = 0;
  for (u32 i = 0; i < size; ++i)
    v = v << 8 | p[i];
  return v;
}

struct Seen
{
  u64 count = 0;
  u32 first_step = 0, last_value = 0;
  std::string kind, where;
};
struct Writes
{
  std::vector<u8> shadow;
  std::map<u32, Seen> words;
  u64 lines = 0, jobs = 0;
};
Writes s_writes;

// The run of mines the walk passes over, for the course whose header, first and end record and records are these.
struct Mines
{
  u32 header = 0, first = 0, end = 0, at = 0, count = 0;
};
Mines s_mines;

u32 MinesFrom(const CPUState* cpu, u32 header, u32 first, u32 end, u32 at)
{
  Mines& m = s_mines;
  if (m.header == header && m.first == first && m.end == end && m.at == at)
    return m.count;
  m = {header, first, end, at, 0};
  for (u32 i = first; i < end; ++i, ++m.count)
  {
    const u32 record = at + i * OBJECT_SIZE;
    const u32 collider_at = Big(cpu->ram + (record + 8 - table::RAM), 4);
    if (!InRam(cpu, collider_at, 0x10))
      break;
    const u32 collider = Big(cpu->ram + (collider_at + 0xC - table::RAM), 4);
    if (collider == 0 || !InRam(cpu, collider, 4) || (Big(cpu->ram + (collider - table::RAM), 4) & 0x4000) == 0)
      break;
  }
  return m.count;
}
}  // namespace

// The machine and part an address is in (the "writes" notes and the check's lines)
int MachineOf(const Layout& layout, u32 address)
{
  for (u32 i = 0; i < CARS; ++i)
  {
    for (const Span& s : layout[i].spans)
    {
      if (address - s.address < s.size)
        return static_cast<int>(i);
    }
  }
  return -1;
}

std::string Where(const Layout& layout, u32 address)
{
  char text[64];
  for (u32 i = 0; i < CARS; ++i)
  {
    for (const Span& s : layout[i].spans)
    {
      if (address - s.address < s.size)
      {
        std::snprintf(text, sizeof(text), "m%u %s+0x%X", i, KindName(s.kind), s.offset + address - s.address);
        return text;
      }
    }
  }
  if (address - CAR_MANAGER < CAR_MANAGER_SIZE)
    std::snprintf(text, sizeof(text), "carmgr+0x%X", address - CAR_MANAGER);
  else if (address - CPU_GLOBALS < CPU_GLOBALS_SIZE)
    std::snprintf(text, sizeof(text), "cpuglobals+0x%X", address - CPU_GLOBALS);
  else
    std::snprintf(text, sizeof(text), "global %08X", address);
  return text;
}

bool GuardOn()
{
  return D().partial_guard;
}

void ReadGuard(const CPUState* cpu, Guard* g)
{
  for (u32 i = 0; i < GUARD_WORDS; ++i)
  {
    const GuardWord& w = WORDS[i];
    u32 address = w.address;
    if (w.pointer)
      address = InRam(cpu, address, 4) ? Big(cpu->ram + (address - table::RAM), 4) + w.offset : 0;
    if (w.window)
      g->words[i] = static_cast<u32>(fzgx::Data().Read(address, w.size));
    else
      g->words[i] = InRam(cpu, address, w.size) ? Big(cpu->ram + (address - table::RAM), w.size) : 0;
  }
  g->objects.clear();
  const u32 header = InRam(cpu, COURSE_HEADER, 4) ? Big(cpu->ram + (COURSE_HEADER - table::RAM), 4) : 0;
  if (!InRam(cpu, header, OBJECTS + 4))
    return;
  const u32 end = Big(cpu->ram + (header + OBJECT_END - table::RAM), 4);
  const u32 first = Big(cpu->ram + (header + OBJECT_FIRST - table::RAM), 4);
  const u32 at = Big(cpu->ram + (header + OBJECTS - table::RAM), 4);
  if (first >= end || end > MOST_OBJECTS || !InRam(cpu, at, end * OBJECT_SIZE))
    return;
  const u32 count = MinesFrom(cpu, header, first, end, at);
  g->objects.resize(count);
  const u8* p = cpu->ram + (at - table::RAM) + first * OBJECT_SIZE;
  for (u32 i = 0; i < count; ++i)
    std::memcpy(&g->objects[i], p + i * OBJECT_SIZE, 4);
}

bool SameObjects(const Guard& a, const Guard& b)
{
  return a.objects == b.objects;
}

bool SameGuard(const Guard& a, const Guard& b)
{
  return a.words == b.words && a.objects == b.objects;
}

std::string GuardDifference(const Guard& a, const Guard& b)
{
  std::string text;
  char part[96];
  for (u32 i = 0; i < GUARD_WORDS; ++i)
  {
    if (a.words[i] == b.words[i])
      continue;
    std::snprintf(part, sizeof(part), "%s%s:%X->%X", text.empty() ? "" : ";", WORDS[i].name, a.words[i], b.words[i]);
    text += part;
  }
  for (std::size_t i = 0; i < std::max(a.objects.size(), b.objects.size()); ++i)
  {
    const u32 x = i < a.objects.size() ? a.objects[i] : 0, y = i < b.objects.size() ? b.objects[i] : 0;
    if (x == y && i < a.objects.size() && i < b.objects.size())
      continue;
    std::snprintf(part, sizeof(part), "%smine %zu:%X->%X", text.empty() ? "" : ";", i, Big(reinterpret_cast<const u8*>(&x), 4),
                  Big(reinterpret_cast<const u8*>(&y), 4));
    text += part;
  }
  return text;
}

bool SameRecords(const CPUState* cpu, const Machine& m, const std::vector<u8>& copy)
{
  if (copy.size() != m.bytes)
    return false;
  u32 at = 0;
  for (const Span& s : m.spans)
  {
    if (std::memcmp(cpu->ram + (s.address - table::RAM), copy.data() + at, s.size) != 0)
      return false;
    at += s.size;
  }
  return true;
}

bool WritesOn(u32 step)
{
  return D().partial_mode == "writes" && (D().partial_to == 0 || (step >= D().partial_from && step <= D().partial_to));
}

void WritesBegin(const CPUState* cpu)
{
  s_writes.shadow.assign(cpu->ram, cpu->ram + cpu->ram_size);
}

void WritesAfter(const CPUState* cpu, const char* kind, u32 machine)
{
  Writes& t = s_writes;
  if (t.shadow.size() != cpu->ram_size)
    return;
  t.jobs += kind != nullptr ? 1 : 0;
  const u32 step = Step();
  const u32 thread = Read32(table::RAM + 0xE4);
  const u32 low = thread - table::RAM < cpu->ram_size - 0x310 ? Read32(thread + 0x308) : 0;
  const u32 high = s_b.entry.gpr[1] + 8;  // the task runner's frame's return slot (sp + 4) included
  const u32 node_word = s_work.manager + MANAGER_NODE;
  for (u32 off = 0; off < cpu->ram_size; off += PAGE)
  {
    const u8* live = cpu->ram + off;
    u8* was = t.shadow.data() + off;
    if (std::memcmp(live, was, PAGE) == 0)
      continue;
    for (u32 w = 0; kind != nullptr && w < PAGE; w += 4)
    {
      if (std::memcmp(live + w, was + w, 4) == 0)
        continue;
      const u32 address = table::RAM + off + w;
      const int owner = MachineOf(s_b.layout, address);
      if (owner == static_cast<int>(machine) || (address >= low && address < high) || address == node_word)
        continue;
      Seen& seen = t.words[address];
      if (seen.count++ == 0)
      {
        seen.first_step = step;
        seen.kind = kind;
        seen.where = owner >= 0 ? "other machine's records: " + Where(s_b.layout, address) : Where(s_b.layout, address);
      }
      seen.last_value = Big(live + w, 4);
      if (t.lines++ < 4000)
      {
        char text[200];
        std::snprintf(text, sizeof(text), "O,%u,%s,%u,%08X,%s,%08X->%08X", step, kind, machine, address, seen.where.c_str(),
                      Big(was + w, 4), seen.last_value);
        Line(text);
      }
    }
    std::memcpy(was, live, PAGE);
  }
}

void WritesSummary()
{
  Writes& t = s_writes;
  if (D().partial_mode != "writes")
    return;
  char text[240];
  std::snprintf(text, sizeof(text), "X,writes: %llu jobs traced, %zu words written outside a job's own records",
                static_cast<unsigned long long>(t.jobs), t.words.size());
  Line(text);
  s_core_host.Log(fzgx::LogLevel::Notice, std::string("fzgx: partial rollback ") + (text + 2));
  // by region: runs of words no more than 0x40 apart, with what wrote them first
  u32 start = 0, last = 0;
  u64 count = 0;
  std::string first;
  const auto flush = [&] {
    if (count == 0)
      return;
    std::snprintf(text, sizeof(text), "X,writes region %08X-%08X: %llu writes, first %s", start, last + 3,
                  static_cast<unsigned long long>(count), first.c_str());
    Line(text);
  };
  for (const auto& [address, seen] : t.words)
  {
    if (count != 0 && address - last <= 0x40)
    {
      last = address;
      count += seen.count;
      continue;
    }
    flush();
    start = last = address;
    count = seen.count;
    first = "step " + std::to_string(seen.first_step) + " " + seen.kind + " " + seen.where;
  }
  flush();
}
}  // namespace fzgx_port::lanes::partial
