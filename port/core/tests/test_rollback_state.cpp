// The list of rollback's state (port/facts/rollback-state.tsv) against what the undo log does: every byte of main
// memory, the locked cache and the data window is changed after a saved step, the step is restored, and what came
// back is compared with the list, row by row. A region the list pairs with something a rollback does not put back
// (the graphics chip, the sound chip, the picture's time) must not come back; a region a step reads must.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include <map>
#include <sstream>

namespace
{
struct StateRow
{
  std::string id, space, within, step, restored, checksum, paired, writer, fix;
  u32 start = 0, end = 0;  // as the list gives them
  bool ranged = false;     // the list gives a range
  int line = 0;
};

std::vector<StateRow> ReadTheList(const std::string& path)
{
  std::vector<StateRow> rows;
  std::ifstream file(path);
  std::string text;
  for (int line = 1; std::getline(file, text); ++line)
  {
    if (text.empty() || text[0] == '#' || text.rfind("id\t", 0) == 0)
      continue;
    std::vector<std::string> c;
    std::stringstream cells(text);
    for (std::string cell; std::getline(cells, cell, '\t');)
      c.push_back(cell);
    if (c.size() != 13)
    {
      std::printf("  rollback-state.tsv:%d: %zu columns, not 13\n", line, c.size());
      return {};
    }
    StateRow row{c[0], c[1], c[4], c[5], c[6], c[7], c[8], c[9], c[10]};
    row.ranged = !c[2].empty() && !c[3].empty();
    if (row.ranged)
    {
      row.start = static_cast<u32>(std::stoul(c[2], nullptr, 16));
      row.end = static_cast<u32>(std::stoul(c[3], nullptr, 16));
    }
    row.line = line;
    rows.push_back(row);
  }
  return rows;
}

bool OneOf(const std::string& value, std::initializer_list<const char*> allowed)
{
  return std::any_of(allowed.begin(), allowed.end(), [&](const char* a) { return value == a; });
}

// A writer column: its words joined with +, each known and named once; whether one of them writes once a frame.
struct Writers
{
  bool ok = true, once_a_frame = false;
};

Writers ReadTheWriters(const std::string& text)
{
  Writers w;
  std::vector<std::string> seen;
  std::stringstream words(text);
  for (std::string word; std::getline(words, word, '+');)
  {
    if (!OneOf(word, {"step", "last-step", "frame", "drawing", "host", "none", "unknown"}) ||
        std::find(seen.begin(), seen.end(), word) != seen.end())
      w.ok = false;
    seen.push_back(word);
    w.once_a_frame |= OneOf(word, {"last-step", "frame", "drawing"});
  }
  if (seen.empty() || text.back() == '+' || (seen.size() > 1 && std::find(seen.begin(), seen.end(), "none") != seen.end()))
    w.ok = false;
  return w;
}

// One of the three spaces the undo log could reach, as bytes: before the step, and after the restore.
struct Space
{
  u32 base = 0;
  std::vector<u8> before, after;
  std::vector<int> owner;  // per byte: the row it belongs to (the last one that names it), -1 none
  u32 Size() const { return static_cast<u32>(before.size()); }
};

// TestRollbacksStateList's memory and list, and its steps in their order.
struct StateList
{
  std::vector<StateRow> rows;
  TestHost host;
  ModData data;
  Guest g{host, data};
  std::mt19937 rng{20261007};

  // Where this test puts what the game finds at run time (rows of space "found"): inside the heap, on no page's
  // boundary. A row of that kind the test has no place for fails: give it one here.
  const u32 bss = MODULE + net::BSS;
  const u32 machines = 0x80C00010;
  const std::map<std::string, std::pair<u32, u32>> found = {
      {"machines", {machines, machines + 30 * 0x620}},
      {"sound-heap", {0x80A00123, 0x80A2F0F7}},
      {"sound-heap-descriptor", {0x80405018, 0x80405024}},
      {"face-board-heap", {0x81235FA0, 0x81275FA0}},
      {"face-board-heap-descriptor", {0x80405030, 0x8040503C}},
      {"mine-countdowns", {0x81180040, 0x811800E0}},
      {"car-presentation", {0x80C10C7A, 0x80C10C7C}},
      {"partial-guard-mines", {0x81000020, 0x81000420}},
      {"partial-guard-sound-queue", {0x8019407C, 0x80194080}},
      {"allocator-blocks", {0x80574C40, 0x8058AD18}},
      {"viewport-record", {0x813B5708, 0x813B5720}},
  };
  std::map<std::string, Space> spaces;

  // The list itself: its words, its rules 2 to 4, and that the rows without a parent fill each space with no gap.
  std::map<std::string, int> by_id;
  std::map<std::string, u32> filled;  // per space: where the rows without a parent have got to
  std::vector<std::pair<u32, u32>> where;  // each row's bytes, as offsets into its space
  std::vector<std::string> in_space;
  bool list_ok = true;
  net::Rollback t;

  // Noise in every byte of the three spaces, and the spaces as they are before the step.
  void FillTheMemory()
  {
    FakeGame(host, g, rng);
    host.scratch.assign(0x40000, 0);  // the emulator's locked cache: 256 KiB
    for (u8& b : host.scratch)
      b = static_cast<u8>(rng());
    for (u32 a = layout::WINDOW; a < layout::WINDOW_END; a += 4)
      g.Write32(a, rng());
    spaces["ram"].base = layout::RAM;
    spaces["ram"].before.assign(host.ram.begin(), host.ram.end());
    spaces["cache"].base = 0;
    spaces["cache"].before = host.scratch;
    spaces["window"].base = layout::WINDOW;
    spaces["window"].before = data.Snapshot();
    for (auto& [name, space] : spaces)
      space.owner.assign(space.Size(), -1);
  }

  void Wrong(const StateRow& row, const char* what)
  {
    std::printf("  rollback-state.tsv:%d: %s: %s\n", row.line, row.id.c_str(), what);
    list_ok = false;
  }

  // A row's name, its words, and the list's rules 2 to 4.
  void TheRowsWords(std::size_t i)
  {
    StateRow& row = rows[i];
    if (by_id.count(row.id))
      Wrong(row, "the name is used twice");
    by_id[row.id] = static_cast<int>(i);
    if (!OneOf(row.space, {"ram", "bss", "cache", "window", "found", "outside"}) ||
        !OneOf(row.step, {"yes", "no", "writes", "unknown"}) || !OneOf(row.restored, {"yes", "no", "session", "unknown"}) ||
        !OneOf(row.checksum, {"yes", "part", "no"}) ||
        !OneOf(row.paired, {"none", "graphics", "video", "sound", "disc", "picture", "emulator", "host", "unknown"}))
      Wrong(row, "a word the list does not have");
    // rule 2: state paired with something a rollback does not put back, put back itself
    if (row.paired != "none" && row.paired != "unknown" && row.restored == "yes")
      Wrong(row, "paired with something a rollback does not put back, and put back: one side only");
    // rule 3: state a step reads, not put back
    if (row.step == "yes" && row.restored == "no")
      Wrong(row, "a step reads it and no rollback puts it back");
    // rule 4
    if (row.checksum != "no" && row.restored != "yes")
      Wrong(row, "the checksum covers it and the undo log does not put it back");
    // rules 5 and 6: who writes it, and what a once-a-frame writer's state needs
    const Writers writers = ReadTheWriters(row.writer);
    if (!writers.ok || !OneOf(row.fix, {"-", "settled", "into-step", "rng-guard", "untrack", "open"}))
      Wrong(row, "a writer or a fix the list does not have");
    if (writers.once_a_frame && (row.step == "yes" || row.restored == "yes") && row.fix == "-")
      Wrong(row, "written once a frame and read by a step or put back by the undo log, with no fix");
    if (row.fix != "-" && !writers.once_a_frame)
      Wrong(row, "a fix for a row no once-a-frame code writes");
    if (row.fix == "untrack" && !OneOf(row.step, {"no", "writes"}))
      Wrong(row, "to be left out of the undo log, and a step may read it");
    if (row.fix == "rng-guard" && row.step != "yes")
      Wrong(row, "a generator guard for a row no step reads");
  }

  // Where a row's bytes are.
  struct Placed
  {
    const StateRow* parent = nullptr;
    std::string space;
    u32 start = 0, end = 0;
  };

  // A row's parent, space and range; none: the list is wrong there, and it is said.
  std::optional<Placed> WhereTheRowIs(std::size_t i)
  {
    StateRow& row = rows[i];
    const StateRow* parent = nullptr;
    if (!row.within.empty())
    {
      const auto it = by_id.find(row.within);
      if (it == by_id.end() || it->second == static_cast<int>(i))
      {
        Wrong(row, "its parent row is not above it");
        return std::nullopt;
      }
      parent = &rows[it->second];
    }
    std::string space = row.space;
    u32 start = row.start, end = row.end;
    if (row.space == "bss")
    {
      space = "ram";
      start += bss;
      end += bss;
    }
    else if (row.space == "found")
    {
      const auto place = found.find(row.id);
      if (place == found.end() || parent == nullptr)
      {
        Wrong(row, "found at run time: this test has no place for it, or it has no parent row");
        return std::nullopt;
      }
      space = in_space[by_id[row.within]];
      std::tie(start, end) = place->second;
    }
    else if (!row.ranged)
    {
      Wrong(row, "no range");
      return std::nullopt;
    }
    return Placed{parent, space, start, end};
  }

  // The row laid into its space: inside it, after the row before or inside its parent; its bytes are its own.
  void LayTheRow(std::size_t i, const Placed& placed)
  {
    const auto& [parent, space, start, end] = placed;
    StateRow& row = rows[i];
    Space& s = spaces[space];
    if (start >= end || start < s.base || end - s.base > s.Size())
    {
      Wrong(row, "its range is not inside its space");
      return;
    }
    in_space[i] = space;
    where[i] = {start - s.base, end - s.base};
    if (parent == nullptr)
    {
      if (start - s.base != filled[space])
        Wrong(row, "a gap or an overlap before it: the rows without a parent fill their space in order");
      filled[space] = end - s.base;
    }
    else
    {
      const auto [from, to] = where[by_id[row.within]];
      if (in_space[by_id[row.within]] != space || start - s.base < from || end - s.base > to)
        Wrong(row, "not inside its parent row");
    }
    std::fill(s.owner.begin() + (start - s.base), s.owner.begin() + (end - s.base), static_cast<int>(i));
  }

  // The list itself: every row, and that the rows without a parent fill each space to its end.
  bool TheListItself()
  {
    where.resize(rows.size());
    in_space.resize(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
      TheRowsWords(i);
    StateRow& row = rows[i];
    if (row.space == "outside")
    {
      if (row.restored != "no")
        Wrong(row, "outside every memory the undo log reaches, and said to be put back");
      continue;
    }
      if (const std::optional<Placed> placed = WhereTheRowIs(i))
        LayTheRow(i, *placed);
    }
    for (const auto& [name, space] : spaces)
    {
      if (filled[name] != space.Size())
      {
        std::printf("  rollback-state.tsv: the rows of space %s end at %#x, the space at %#x\n", name.c_str(),
                    filled[name], space.Size());
        list_ok = false;
      }
    }
    return list_ok;
  }

  // The image's list of memory never rolled back, the machines, and the checksum before the step (given).
  u32 SetUpTheStep()
  {
    // The image's list of memory never rolled back, as the rows say it: every part of the restored main memory that
    // its row says is not put back.
    u32 ranges = 0;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
      if (in_space[i] != "ram" || rows[i].within.empty() || rows[i].restored != "no")
        continue;
      g.Write32(net::UNTRACKED + 4 + 8 * ranges, layout::RAM + where[i].first);
      g.Write32(net::UNTRACKED + 8 + 8 * ranges, layout::RAM + where[i].second);
      ++ranges;
    }
    CHECK(ranges > 0 && ranges <= net::MAX_UNTRACKED);
    g.Write32(net::UNTRACKED, ranges);
    // the machines where the checksum looks for them
    g.Write32(MODULE + net::MACHINES, machines);
    g.Write8(MODULE + net::MACHINE_COUNT, 30);
    for (u32 i = 0; i < 30; ++i)
      g.Write32(machines + i * 0x620 + 0x49C, 0x80D00000 + i * 0x200);
    spaces["ram"].before.assign(host.ram.begin(), host.ram.end());
    spaces["window"].before = data.Snapshot();
    const u32 sum = net::StateChecksum(g, MODULE);
    return sum;
  }

  void OneStepAndBack()
  {
    // One step that changes every byte there is, and back to before it.
    t.Start(g);
    t.SaveStep(g, 0, true);
    for (u8& b : host.ram)
      b = static_cast<u8>(~b);
    for (u8& b : host.scratch)
      b = static_cast<u8>(~b);
    for (u32 a = layout::WINDOW; a < layout::WINDOW_END; a += 4)
      g.Write32(a, ~g.Read32(a));
    t.SaveStep(g, 1, true);
    t.RestoreTo(g, 1, 0);
    spaces["ram"].after.assign(host.ram.begin(), host.ram.end());
    spaces["cache"].after = host.scratch;
    spaces["window"].after = data.Snapshot();
  }

  void EveryRowCameBackAsListed()
  {
    // Rule 1, and rule 2 as the undo log behaves: each row's own bytes (those no later row names) all came back, or
    // none did, as the row says.
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
      const StateRow& row = rows[i];
      if (in_space[i].empty())
        continue;
      const Space& s = spaces[in_space[i]];
      u32 own = 0, back = 0, kept = 0, first_back = 0, first_kept = 0;
      for (u32 at = where[i].first; at < where[i].second; ++at)
      {
        if (s.owner[at] != static_cast<int>(i))
          continue;
        ++own;
        if (s.after[at] == s.before[at] && back++ == 0)
          first_back = at;
        if (s.after[at] == static_cast<u8>(~s.before[at]) && kept++ == 0)
          first_kept = at;
      }
      if (row.id == "step-word")
      {
        CHECK(g.Read32(net::STEP) == 0);  // the step the restore went back to
        continue;
      }
      const bool as_listed = row.restored == "yes" ? back == own : row.restored == "unknown" ? true : kept == own;
      CHECK(as_listed);
      if (!as_listed)
        std::printf("  rollback-state.tsv:%d: %s is listed as restored \"%s\": of its %u bytes a rollback put back %u "
                    "(the first at %#x) and left %u (the first at %#x)\n",
                    row.line, row.id.c_str(), row.restored.c_str(), own, back, s.base + first_back, kept,
                    s.base + first_kept);
      const bool one_side = row.paired != "none" && row.paired != "unknown" && back != 0;
      CHECK(!one_side);
      if (one_side)
        std::printf("  rollback-state.tsv:%d: %s is paired with \"%s\", which no rollback puts back, and a rollback put "
                    "back %u of its %u bytes (the first at %#x): one side only\n",
                    row.line, row.id.c_str(), row.paired.c_str(), back, own, s.base + first_back);
    }
  }
};
}  // namespace

void TestRollbacksStateList()
{
  const std::string path =
      (std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "facts" / "rollback-state.tsv").string();
  StateList list;
  list.rows = ReadTheList(path);
  CHECK(list.rows.size() >= 30);  // the list was found and read
  if (list.rows.empty())
    return;

  list.FillTheMemory();
  const bool list_ok = list.TheListItself();
  CHECK(list_ok);
  if (!list_ok)
    return;

  const u32 sum = list.SetUpTheStep();
  list.OneStepAndBack();
  list.EveryRowCameBackAsListed();
  // Rule 4 as the checksum behaves: it reads nothing a rollback left changed.
  CHECK(net::StateChecksum(list.g, MODULE) == sum);
  list.t.Stop();
}
