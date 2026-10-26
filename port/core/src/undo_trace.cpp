// Tests (Debug::undo_trace): the undo log's own account, written to a file. It reads the game's memory and the
// log; what it keeps for itself (in Tracking, from `trace` on) is written only while the file is open, and without
// the key no line of it runs but the tests of `trace`.
//
// It shows two faults, which are not one another's cause:
//   - "heap": an OS heap's cells stopped or started adding up to its size, and before which step. This is what
//     found the freezes of 0.2.1: the picture's thread cleared a frame-buffer copy's destination after a restore
//     had put the memory back, over the main heap's cell headers. No late page was near those breaks.
//   - "late", "stale": the scan way's fault (Tracking::Way::Scan, which a log takes only when the guard cannot be
//     had). A page outside the watched set is compared only before a displayed frame's own step and before the
//     step after it. Changed by a step run again in between, it is found a frame later, and its old content then
//     stands in the undo entry of the last step before the comparison, though it is the content of several steps
//     earlier. A restore that goes back over that entry and not over the step that made the change puts the page
//     back to before the change and the rest of memory to after it. Open for the scan way. With the guard on, a
//     first write is caught when it is made, so every comparison accounts for every page that changed: its late
//     pages have a known step and no restore is stale (the scan way's rule under the guard would name every late
//     page after the guard's first step "which step is not known", and every restore over its entry
//     stale).
//   - "guard": with the guard on, before each step whose comparison it caught a first write for: the pages caught,
//     how many of them are late (first seen changing) and how many were written again after a rest, the faults
//     since the last line, the pages it guards and those watched; when it goes on and off, its counts.
// With "check", "faithful" and "unfaithful": every restore to a step of a span against the memory kept before it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo_log.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "internal.h"

namespace fzgx::net
{
namespace
{
constexpr u32 RAM_START = layout::RAM;
constexpr u32 RAM_END = layout::WINDOW;
constexpr u32 HEAPS = 8;
constexpr u32 MAX_CELLS = 100000;
constexpr u32 COPIES = 24;

void Line(Tracking& s, const std::string& text)
{
  s.trace->WriteBytes(text.data(), text.size());
  s.trace->WriteBytes("\n", 1);
  s.trace->Flush();
}

// The sizes of one list of cells (each: previous, next, size) added up; false when the list leaves memory or its
// links do not agree.
bool AddUp(Guest& g, u32 cell, std::int64_t* total, u32* cells)
{
  u32 previous = 0;
  for (; cell != 0; ++*cells)
  {
    if (cell < RAM_START || cell > RAM_END - 12 || *cells > MAX_CELLS || g.Read32(cell) != previous)
      return false;
    *total += static_cast<std::int32_t>(g.Read32(cell + 8));
    previous = cell;
    cell = g.Read32(cell + 4);
  }
  return true;
}

std::string Words(const u8* bytes)
{
  std::string out;
  for (u32 i = 0; i < 32; i += 4)
    out += std::format("{}{:02x}{:02x}{:02x}{:02x}", i == 0 ? "" : " ", bytes[i], bytes[i + 1], bytes[i + 2],
                       bytes[i + 3]);
  return out;
}
}  // namespace

void OpenUndoTrace(Tracking& s)
{
  s.trace.reset();
  s.late_notes.clear();
  s.heap_state.fill(0);
  s.copies.clear();
  s.bad_dumps = 0;
  s.span_first = s.span_partial = 0;
  s.guard_faults = 0;
  const std::string& path = TheOptions().debug.undo_trace;
  if (path.empty())
    return;
  s.trace = std::make_unique<File>(path, "wb");
  if (!s.trace->IsOpen())
  {
    Error("fzgx: cannot write the undo log's trace {}", path);
    s.trace.reset();
  }
}

void TraceCompared(Tracking& s, u32 t, bool complete)
{
  if (complete)
  {
    s.span_first = t;
    s.span_partial = 0;
  }
  else
  {
    ++s.span_partial;
  }
}

void TraceGuardStep(Tracking& s, u32 t, u32 caught, u32 late, u32 woke, u32 rested, u64 faults, u32 guarded)
{
  if (caught == 0 && rested == 0)
    return;
  Line(s, std::format("guard,before step {},{} pages caught ({} late, {} written again after a rest),{} faults,{} "
                      "rested,{} guarded,{} watched",
                      t, caught, late, woke, faults, rested, guarded, s.watched_list.size()));
}

void TraceGuard(Tracking& s, const std::string& text)
{
  if (s.trace)
    Line(s, "guard," + text);
}

void TraceLatePage(Tracking& s, u32 page, u32 t, bool learning)
{
  // the comparison before step t found it; every page was last compared before step span_first
  const bool several = s.span_partial != 0 && t > 0;
  if (several)
    s.late_notes.push_back({page, s.span_first, t - 1});
  if (!learning || several)
    Line(s, std::format("late,{:08x},found before step {},changed in steps {} to {},its old content in the entry of "
                        "step {}{}",
                        s.region + page, t, several ? s.span_first : t - 1, t - 1, t - 1,
                        several ? ",WHICH STEP IS NOT KNOWN" : ""));
}

std::vector<Tracking::LateNote> TraceStale(Tracking& s, const u8* live, u32 from, u32 t, std::vector<u8>* before)
{
  std::vector<Tracking::LateNote> stale, rest;
  for (const Tracking::LateNote& note : s.late_notes)
  {
    if (note.entry >= from || !s.undo.contains(note.entry))
      continue;  // a restore has taken its entry, or the step is final
    if (note.entry < t)
      rest.push_back(note);  // not gone back over: its turn may come
    else if (note.first < t)
      stale.push_back(note);  // the entry is gone back over, the first step that may have changed the page is not
  }
  s.late_notes = rest;
  for (const Tracking::LateNote& note : stale)
    before->insert(before->end(), live + note.page, live + note.page + PAGE);
  return stale;
}

void TraceRestored(Tracking& s, const u8* live, const std::vector<Tracking::LateNote>& stale,
                   const std::vector<u8>& before, u32 from, u32 t)
{
  for (size_t i = 0; i < stale.size(); ++i)
  {
    const Tracking::LateNote& note = stale[i];
    Line(s, std::format("stale,{:08x},the restore from before step {} to before step {} put the page back to before "
                        "step {}: what steps {} to {} wrote on it is gone, what they wrote elsewhere is kept",
                        s.region + note.page, from, t, note.first, note.first, t - 1));
    u32 shown = 0;
    for (u32 b = 0; b < PAGE && shown < 12; b += 32)
    {
      const u8* was = before.data() + i * PAGE + b;
      if (std::memcmp(was, live + note.page + b, 32) == 0)
        continue;
      ++shown;
      Line(s, std::format("words,{:08x},before the restore,{},after it,{}", s.region + note.page + b, Words(was),
                          Words(live + note.page + b)));
    }
  }
}

void TraceCopy(Tracking& s, const u8* live, u32 t)
{
  const Debug& d = TheOptions().debug;
  if (t < d.undo_trace_from || t >= d.undo_trace_to)
    return;
  s.copies[t].assign(live, live + s.shadow.size());
  while (!s.copies.empty() && s.copies.begin()->first + COPIES < t)
    s.copies.erase(s.copies.begin());
}

void TraceFaithful(Tracking& s, const u8* live, u32 from, u32 t)
{
  const auto it = s.copies.find(t);
  if (it == s.copies.end())
    return;
  const u8* was = it->second.data();
  std::vector<u8> kept(s.shadow.size() / 32, 0);  // per block: a part of it is kept through a restore by design
  for (const auto& [off, len] : s.kept)
  {
    for (u32 b = off / 32; b <= (off + len - 1) / 32; ++b)
      kept[b] = 1;
  }
  u32 pages = 0;
  for (u32 page = 0; page < s.shadow.size(); page += PAGE)
  {
    if (s.untracked[page / PAGE] || std::memcmp(was + page, live + page, PAGE) == 0)
      continue;
    u32 blocks = 0, first = 0;
    for (u32 b = page; b < page + PAGE; b += 32)
    {
      if (kept[b / 32] || std::memcmp(was + b, live + b, 32) == 0)
        continue;
      first = blocks++ == 0 ? b : first;
    }
    if (blocks == 0)
      continue;
    ++pages;
    Line(s, std::format("unfaithful,{:08x},the restore from before step {} to before step {} left {} blocks of this "
                        "{} page other than they were before step {} last ran; the first,{:08x},was,{},is,{}",
                        s.region + page, from, t, blocks, s.watched[page / PAGE] ? "watched" : "unwatched", t,
                        s.region + first, Words(was + first), Words(live + first)));
  }
  if (pages == 0)
    Line(s, std::format("faithful,the restore from before step {} to before step {}", from, t));
}

void TraceHeaps(Guest& g, Tracking& s, const std::string& when)
{
  const u32 table = TheOptions().debug.undo_trace_heaps;
  for (u32 i = 0; table != 0 && i < HEAPS; ++i)
  {
    const u32 heap = table + 12 * i;
    const std::int32_t size = static_cast<std::int32_t>(g.Read32(heap));
    if (size < 0)
      continue;
    std::int64_t total = 0;
    u32 cells = 0;
    const bool linked = AddUp(g, g.Read32(heap + 8), &total, &cells) && AddUp(g, g.Read32(heap + 4), &total, &cells);
    const int state = linked && total == size ? 1 : 2;
    if (state == s.heap_state[i] || (state == 1 && s.heap_state[i] == 0))
    {
      s.heap_state[i] = state;
      continue;
    }
    s.heap_state[i] = state;
    Line(s, std::format("heap,{},{},{} cells of {:#x} bytes in all for a heap of {:#x}{},{}", i,
                        state == 1 ? "adds up again" : "DOES NOT ADD UP", cells, total, size,
                        linked ? "" : " (a list's links are broken)", when));
    if (state == 2 && !s.copies.empty() && s.bad_dumps < 4)
    {
      // with "check": the tracked memory as it is, and as it was before each of the last three steps kept
      const std::string name = std::format("{}.bad{}", TheOptions().debug.undo_trace, ++s.bad_dumps);
      File now(name + ".now.ram", "wb");
      if (now.IsOpen())
        now.WriteBytes(g.Ram(s.region, s.shadow.size()), s.shadow.size());
      u32 n = 0;
      for (auto it = s.copies.rbegin(); it != s.copies.rend() && n < 3; ++it, ++n)
      {
        File was(std::format("{}.before{}.ram", name, it->first), "wb");
        if (was.IsOpen())
          was.WriteBytes(it->second.data(), it->second.size());
      }
      Line(s, std::format("dumped,{},the tracked memory from {:#x}", name, s.region));
    }
  }
}
}  // namespace fzgx::net
