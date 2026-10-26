// The replay scrubber's memory: the undo log's entries packed as they are logged, by units of UNIT steps, unpacked a
// unit at a time when a move of the memory needs them, and dropped when the replay's memory is over its budget
// (Options::replay_memory; scrub.cpp).
//
// A step's entry is first made flat and easy to pack: its head, every block's place, then every block's content,
// a block that the step before in the unit also holds (the same place) XORed with that one. A step's
// entry holds what its place held before the step, so two steps' blocks of one place differ by what one step
// changed there: measured on real logs, 68 % of such bytes are zero and the unit packs to a third (zlib's
// fastest: 3.0 times at 30 racers; as kept, 1.9). The bytes then go to the host's packer (Host::Pack: the
// emulator's zstd in the runner); a host without one keeps them flat.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo_log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <tuple>
#include <utility>
#include <vector>

namespace fzgx::net
{
namespace
{
// a step's head: step, blocks, the cache's blocks, has_rng, rng, whether its contents are XORed with the step before
constexpr std::size_t HEAD = 4 + 4 + 4 + 1 + 4 + 1;
constexpr u32 MOST_HOT = 8;                       // units kept unpacked beside the one a move needs

void PutWord(std::vector<u8>& out, u32 value)
{
  const std::size_t at = out.size();
  out.resize(at + 4);
  std::memcpy(out.data() + at, &value, 4);
}

u32 Word(const u8* at)
{
  u32 value;
  std::memcpy(&value, at, 4);
  return value;
}

bool Ascending(const Blocks& blocks)
{
  for (std::size_t i = 1; i < blocks.size(); ++i)
  {
    if (blocks[i].first <= blocks[i - 1].first)
      return false;
  }
  return true;
}

// `blocks`' contents to `out`, each XORed with `before`'s block of the same place when there is one (both lists
// by ascending place). The same call undoes it when `blocks` holds the XORed contents and `before` the plain ones.
template <typename Emit>
void WithBefore(const Blocks& blocks, const Blocks* before, Emit&& emit)
{
  std::size_t j = 0;
  for (const auto& [place, content] : blocks)
  {
    std::array<u8, 32> x = content;
    if (before != nullptr)
    {
      while (j < before->size() && (*before)[j].first < place)
        ++j;
      if (j < before->size() && (*before)[j].first == place)
      {
        for (std::size_t b = 0; b < 32; ++b)
          x[b] ^= (*before)[j].second[b];
      }
    }
    emit(place, x);
  }
}

double Us(std::chrono::steady_clock::time_point since)
{
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - since).count();
}

// The unit's entries out of the log's map (their lists to the spare ones).
void ForgetEntries(Tracking& s, u32 unit)
{
  for (auto it = s.undo.lower_bound(unit); it != s.undo.end() && it->first < unit + UNIT;)
    it = EraseEntry(s, it);
}

// A step's entry made flat: its head, its blocks' places, their contents (XORed with `before`'s when given).
std::vector<u8> Flat(u32 step, const Undo& entry, const Undo* before)
{
  std::vector<u8> flat, contents;
  const std::size_t blocks = entry.blocks.size() + entry.scratch.size();
  flat.reserve(HEAD + blocks * 36);
  contents.reserve(blocks * 32);
  PutWord(flat, step);
  PutWord(flat, static_cast<u32>(entry.blocks.size()));
  PutWord(flat, static_cast<u32>(entry.scratch.size()));
  flat.push_back(entry.has_rng ? 1 : 0);
  flat.insert(flat.end(), entry.rng.begin(), entry.rng.end());
  flat.push_back(before != nullptr ? 1 : 0);
  for (const Blocks* list : {&entry.blocks, &entry.scratch})
  {
    const Blocks* prior = before == nullptr ? nullptr : list == &entry.blocks ? &before->blocks : &before->scratch;
    WithBefore(*list, prior, [&](u32 at, const std::array<u8, 32>& x) {
      PutWord(flat, at);
      contents.insert(contents.end(), x.begin(), x.end());
    });
  }
  flat.insert(flat.end(), contents.begin(), contents.end());
  return flat;
}

// A flat step back into an entry, `before` being the step before's plain entry when it was XORed with it.
u32 FromFlat(const u8* flat, const Undo* before, Undo* entry)
{
  const u32 n[2] = {Word(flat + 4), Word(flat + 8)};
  const u8* place = flat + HEAD;
  const u8* content = place + (std::size_t{n[0]} + n[1]) * 4;
  const bool xored = flat[17] != 0 && before != nullptr;
  Blocks plain;
  int which = 0;
  for (Blocks* list : {&entry->blocks, &entry->scratch})
  {
    plain.resize(n[which]);
    for (u32 b = 0; b < n[which]; ++b, place += 4, content += 32)
    {
      plain[b].first = Word(place);
      std::memcpy(plain[b].second.data(), content, 32);
    }
    list->clear();
    list->reserve(n[which]);
    const Blocks* prior = !xored ? nullptr : which == 0 ? &before->blocks : &before->scratch;
    WithBefore(plain, prior, [&](u32 at, const std::array<u8, 32>& x) { list->emplace_back(at, x); });
    ++which;
  }
  entry->has_rng = flat[12] != 0;
  std::memcpy(entry->rng.data(), flat + 13, 4);
  return Word(flat);
}

// A packed unit's steps back into the log's map (those not there already), in lists taken from the spare ones.
bool Unpack(Host& host, Tracking& s, const PackedUnit& p)
{
  const auto started = std::chrono::steady_clock::now();
  std::vector<u8> flat, prefix;
  Undo held;                     // a step's entry made again that the map had already
  const Undo* before = nullptr;  // the step before's, plain
  for (std::size_t at = 0; at + 9 <= p.bytes.size();)
  {
    const u32 flat_size = Word(p.bytes.data() + at), size = Word(p.bytes.data() + at + 4);
    const bool zipped = p.bytes[at + 8] != 0;
    const u8* record = p.bytes.data() + at + 9;
    flat.resize(flat_size);
    if (zipped ? !host.Unpack(record, size, prefix.data(), prefix.size(), flat.data(), flat.size()) : size != flat_size)
      return false;
    if (!zipped)
      std::memcpy(flat.data(), record, size);
    prefix = flat;
    Undo entry;
    entry.blocks = TakeSpare(s);
    entry.scratch = TakeSpare(s);
    const u32 step = FromFlat(flat.data(), before, &entry);
    if (const auto [it, fresh] = s.undo.try_emplace(step); fresh)
    {
      it->second = std::move(entry);
      s.undo_bytes += EntryBytes(it->second);
      before = &it->second;
    }
    else
    {
      DropEntry(s, held);
      held = std::move(entry);
      before = &held;
    }
    at += 9 + size;
  }
  s.unpack_us += Us(started);
  ++s.unpacks;
  return true;
}
}  // namespace

// The entry of `step` out of the log's map into its unit, packed: XORed with the step before's when that one is
// in the same unit (kept plain beside the units for this), then given to the host's packer. Each step as it comes,
// so that packing costs the same in every front step (by units, a frame in 32 at 30 racers took 10 ms more).
void PackStep(Host& host, Tracking& s, u32 step)
{
  const auto it = s.undo.find(step);
  if (it == s.undo.end())
    return;
  const auto started = std::chrono::steady_clock::now();
  const u32 unit = step - step % UNIT;
  PackedUnit& p = s.units[unit];
  const Undo& entry = it->second;
  const bool sorted = Ascending(entry.blocks) && Ascending(entry.scratch);
  const bool xor_before = sorted && step % UNIT != 0 && s.last_plain_step + 1 == step && s.last_plain_sorted;
  if (step % UNIT == 0)
    s.last_flat.clear();  // (a unit unpacks on its own: nothing of the unit before is referred to)
  if (!p.dropped)
  {
    std::vector<u8> flat = Flat(step, entry, xor_before ? &s.last_plain : nullptr);
    std::vector<u8> packed;
    // (the step before's flat bytes as the packer's prefix: a step's places, and much of its contents, are the
    // step before's again)
    const bool zipped = host.Pack(flat.data(), flat.size(), s.last_flat.data(), s.last_flat.size(), &packed) &&
                        packed.size() < flat.size();
    const std::vector<u8>& record = zipped ? packed : flat;
    s.packed_bytes -= p.bytes.capacity();
    PutWord(p.bytes, static_cast<u32>(flat.size()));
    PutWord(p.bytes, static_cast<u32>(record.size()));
    p.bytes.push_back(zipped ? 1 : 0);
    p.bytes.insert(p.bytes.end(), record.begin(), record.end());
    if (step % UNIT == UNIT - 1)
      p.bytes.shrink_to_fit();
    s.packed_bytes += p.bytes.capacity();
    p.content += (entry.blocks.size() + entry.scratch.size()) * 32;
    s.packed_content += (entry.blocks.size() + entry.scratch.size()) * 32;
    s.last_flat = std::move(flat);
  }
  s.last_plain = entry;
  s.last_plain_step = step;
  s.last_plain_sorted = sorted;
  if (!p.hot)  // (a unit kept unpacked keeps its new step unpacked too)
    EraseEntry(s, it);
  s.pack_us += Us(started);
  ++s.packs;
}

// The entries of steps `to` to `from` - 1 are in the log's map, unpacked where they were packed. False: a unit of
// them was dropped (or would not unpack), and the map is not to be used for them.
bool EnsureEntries(Host& host, Tracking& s, u32 from, u32 to)
{
  if (from <= to)
    return true;
  const u32 first = to - to % UNIT, last = (from - 1) - (from - 1) % UNIT;
  for (u32 unit = first; unit <= last; unit += UNIT)
  {
    const auto it = s.units.find(unit);
    if (it == s.units.end())
      continue;  // (not packed yet: its entries are in the map as the steps left them)
    if (it->second.dropped)
      return false;
    if (it->second.hot)
    {
      s.hot.erase(std::remove(s.hot.begin(), s.hot.end(), unit), s.hot.end());
      s.hot.push_back(unit);
      continue;
    }
    if (!Unpack(host, s, it->second))
      return false;
    it->second.hot = true;
    s.hot.push_back(unit);
  }
  // the unpacked units used longest ago go, but for the ones just asked for
  for (std::size_t i = 0; s.hot.size() > MOST_HOT && i < s.hot.size();)
  {
    const u32 unit = s.hot[i];
    if (unit >= first && unit <= last)
    {
      ++i;
      continue;
    }
    ForgetEntries(s, unit);
    s.units[unit].hot = false;
    s.hot.erase(s.hot.begin() + static_cast<std::ptrdiff_t>(i));
  }
  return true;
}

// The packed unit farthest from `step`, and more than `near` steps from it, is dropped: its bytes freed, its entries
// gone. The bytes freed (0: none left that far).
std::size_t DropFarthestUnit(Tracking& s, u32 step, u32 near)
{
  PackedUnit* farthest = nullptr;
  u32 unit = 0, far = 0;
  for (auto& [first, p] : s.units)
  {
    const u32 distance = first > step ? first - step : step - std::min(step, first + UNIT - 1);
    if (!p.dropped && distance > near && (farthest == nullptr || distance > far))
      std::tie(farthest, unit, far) = std::tuple{&p, first, distance};
  }
  if (farthest == nullptr)
    return 0;
  const std::size_t freed = farthest->bytes.capacity();
  if (farthest->hot)
  {
    ForgetEntries(s, unit);
    s.hot.erase(std::remove(s.hot.begin(), s.hot.end(), unit), s.hot.end());
  }
  s.packed_bytes -= freed;
  s.packed_content -= farthest->content;
  *farthest = PackedUnit{};
  farthest->dropped = true;
  ++s.dropped;
  return freed;
}

// The key farthest from `step` that may go: more than `near` steps from it, not the race's first (`first`) and not one
// at a multiple of `keep_every` steps, which stay so that a step whose log was dropped is never more than that many
// steps from a key below it. Keys are taken at multiples of their spacing, so the kept ones are counted from step 0,
// not from the race's first (a race starts on any step). The bytes freed (0: none may go).
std::size_t DropFarthestKey(Tracking& s, u32 step, u32 near, u32 first, u32 keep_every)
{
  auto farthest = s.keys.end();
  u32 far = 0;
  for (auto it = s.keys.begin(); it != s.keys.end(); ++it)
  {
    const u32 at = it->first;
    const u32 distance = at > step ? at - step : step - at;
    if (at <= first || at % keep_every == 0 || distance <= near)
      continue;
    if (farthest == s.keys.end() || distance > far)
      std::tie(farthest, far) = std::pair{it, distance};
  }
  if (farthest == s.keys.end())
    return 0;
  const std::size_t freed = static_cast<std::size_t>(KeyBytes(farthest->second));
  s.key_bytes -= freed;
  s.keys.erase(farthest);
  ++s.keys_dropped;
  return freed;
}
}  // namespace fzgx::net
