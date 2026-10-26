// A race's stat pack (the match start's "setup.stats"; protocol/formats.md section 2,
// handbook/stat-packs.md): a table of changed words of the game's car data, which the server hands every game of a
// race. Read here into checked values, or refused whole with a sentence that says which rule: a pack is the server's
// word, or a file's (a replay's header holds the match start), and its values go into the physics as they are.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <bit>
#include <cmath>
#include <format>

#include "const_ranges.h"
#include "stat_ranges.h"

namespace fzgx::stats
{
const Rule* RuleAt(u32 offset)
{
  if (offset % 4 != 0)
    return nullptr;
  for (const Rule& rule : RULES)
  {
    const u32 words = rule.kind == Kind::Points ? 12 : 1;
    if (offset >= rule.at && offset < rule.at + words * 4)
      return &rule;
  }
  return nullptr;
}

std::string WordName(u32 offset)
{
  const Rule* rule = RuleAt(offset);
  if (!rule)
    return std::format("+0x{:02X}", offset);
  if (rule->kind != Kind::Points)
    return rule->name;
  const u32 word = (offset - rule->at) / 4;
  return std::format("{}{}{}", rule->name, word / 3, "XYZ"[word % 3]);
}

std::string Text(float value)
{
  std::string text = std::format("{:f}", value);
  text.erase(text.find_last_not_of('0') + 1);
  if (text.back() == '.')
    text.pop_back();
  return text;
}

std::optional<std::string> Refusal(u32 block, u32 offset, u32 bits)
{
  const Rule& rule = *RuleAt(offset);
  if (rule.kind == Kind::Bytes)
  {
    if (((bits >> 16) & 0xFF) > static_cast<u32>(rule.high) || (bits & 0xFFFF) != 0)
      return std::format("is {:08X}: the second byte is at most {} and the last two are 0", bits, Text(rule.high));
    return std::nullopt;
  }
  // On the bits, before anything is compared: a number is the bits of 0, or a normal number. A denormal is read as
  // zero where the race runs, so it is neither "above" a floor nor "not 0" whatever a comparison here says.
  const u32 exponent = (bits >> 23) & 0xFF;
  if (bits != 0 && (exponent == 0 || exponent == 0xFF))
    return std::format("is the bits {:08X}, which is no number a pack may hold (a denormal, -0, an infinity or none at all)", bits);
  const float v = std::bit_cast<float>(bits);
  if (block >= MACHINES)
  {
    // a custom part: what it adds to a custom machine, of either sign; held by its size alone (stat_ranges.h)
    if (rule.below ? !(std::fabs(v) < rule.high) : !(std::fabs(v) <= rule.high))
      return std::format("is {}, and a part's value is {} {} from 0", Text(v), rule.below ? "less than" : "at most", Text(rule.high));
    return std::nullopt;
  }
  const bool z = rule.z_least != 0 && (offset - rule.at) / 4 % 3 == 2;
  if (z && std::fabs(v) < rule.z_least)
    return std::format("is {}, and a machine's hover point's Z is at least {} from 0", Text(v), Text(rule.z_least));
  if (!(v >= rule.low))
    return std::format("is {}, and must be at least {}", Text(v), Text(rule.low));
  if (rule.below ? !(v < rule.high) : !(v <= rule.high))
    return std::format("is {}, and must be {} {}", Text(v), rule.below ? "below" : "at most", Text(rule.high));
  return std::nullopt;
}
}  // namespace fzgx::stats

namespace fzgx::boot
{
namespace
{
// Standard base64 with its padding, and nothing else: no line break, no other alphabet. Nothing: not that.
std::optional<std::vector<u8>> Base64(const std::string& text)
{
  if (text.size() % 4 != 0)
    return std::nullopt;
  const auto six = [](char c) -> int {
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
           c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  std::vector<u8> out;
  out.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4)
  {
    const bool last = i + 4 == text.size();
    const int pad = last && text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0;
    u32 group = 0;
    for (int k = 0; k < 4; ++k)
    {
      const int v = k >= 4 - pad ? 0 : six(text[i + k]);
      if (v < 0)
        return std::nullopt;
      group = group << 6 | static_cast<u32>(v);
    }
    // the bits a padded group leaves over are zero in the one way to write its bytes
    if ((pad == 1 && (group & 0xFF) != 0) || (pad == 2 && (group & 0xFFFF) != 0))
      return std::nullopt;
    out.push_back(static_cast<u8>(group >> 16));
    if (pad < 2)
      out.push_back(static_cast<u8>(group >> 8));
    if (pad < 1)
      out.push_back(static_cast<u8>(group));
  }
  return out;
}

// A pack's name: what a log line and a list can show as it is.
bool NameOfAPack(const std::string& name)
{
  if (name.empty() || name.size() > MAX_STAT_NAME || name.front() == ' ' || name.back() == ' ')
    return false;
  for (const char c : name)
  {
    if (c < 0x20 || c > 0x7E)
      return false;
  }
  return true;
}

// The entries of a pack's bytes, each checked: the car data's and the constants', apart; nothing, and `why` says
// which rule. Every entry stands after the one before it, by block and offset, so the constants (CONST_BLOCK) come
// last, and a double's two slots stand together.
struct Entries
{
  std::vector<StatEntry> cars;
  std::vector<StatEntry> constants;
};

// The constants' entries, judged whole: a double's high slot is followed by its low slot and a low slot follows its
// high slot, and every value is in its range.
bool ConstantsHold(const std::vector<StatEntry>& constants, std::string* why)
{
  for (size_t i = 0; i < constants.size(); ++i)
  {
    const StatEntry& e = constants[i];
    const consts::Constant& c = consts::CONSTANTS[e.at];
    const bool high = c.kind == consts::Kind::Number64High;
    const bool paired = high ? i + 1 < constants.size() && constants[i + 1].at == e.at + 1
                             : c.kind != consts::Kind::Number64Low || (i != 0 && constants[i - 1].at + 1 == e.at);
    if (!paired)
    {
      *why = std::format("the constant {} is a 64-bit number, and the pack holds one of its two words", c.name);
      return false;
    }
    if (c.kind == consts::Kind::Number64Low)
      continue;
    if (const auto refused = consts::Refusal(e.at, e.value, high ? constants[i + 1].value : 0))
    {
      *why = std::format("the constant {} {}", c.name, *refused);
      return false;
    }
  }
  return true;
}

std::optional<Entries> EntriesOf(const std::vector<u8>& bytes, std::string* why)
{
  constexpr size_t MOST = stats::MOST_ENTRIES + consts::SLOTS;
  if (bytes.empty() || bytes.size() % file::STAT_ENTRY != 0 || bytes.size() / file::STAT_ENTRY > MOST)
  {
    *why = std::format("its values are {} bytes, which is not 1 to {} entries of {} bytes", bytes.size(), MOST,
                       file::STAT_ENTRY);
    return std::nullopt;
  }
  Entries out;
  out.cars.reserve(bytes.size() / file::STAT_ENTRY);
  StatEntry last{};
  for (size_t i = 0; i < bytes.size(); i += file::STAT_ENTRY)
  {
    const StatEntry e{bytes[i], bytes[i + 1], paint::Be32(bytes.data() + i + 2)};
    const size_t n = i / file::STAT_ENTRY;
    const bool constant = e.block == file::CONST_BLOCK;
    const bool out_of_order = n != 0 && (e.block < last.block || (e.block == last.block && e.at <= last.at));
    if (constant && out_of_order)
      *why = std::format("entry {} (block {}, slot {}) is there twice or out of order", n, e.block, e.at);
    else if (constant && e.at >= consts::SLOTS)
      *why = std::format("entry {} names the constant slot {}: the game has {}", n, e.at, consts::SLOTS);
    else if (constant)
    {
      out.constants.push_back(e);
      last = e;
      continue;
    }
    else if (e.block >= stats::BLOCKS)
      *why = std::format("entry {} names block {}: the car data has {}", n, e.block, stats::BLOCKS);
    else if (e.at == 0)
      *why = std::format("entry {} would write block {}'s word 0, the place of its name: a pack changes no name", n, e.block);
    else if (!stats::RuleAt(e.at))
      *why = std::format("entry {} names offset 0x{:02X} of block {}: no word a pack may change is there", n, e.at, e.block);
    else if (out_of_order)
      *why = std::format("entry {} (block {}, {}) is there twice or out of order", n, e.block, stats::WordName(e.at));
    else if (const auto refused = stats::Refusal(e.block, e.at, e.value))
      *why = std::format("block {}'s {} {}", e.block, stats::WordName(e.at), *refused);
    else
    {
      out.cars.push_back(e);
      last = e;
      continue;
    }
    return std::nullopt;
  }
  if (!ConstantsHold(out.constants, why))
    return std::nullopt;
  return out;
}
}  // namespace

std::string StatFingerprint(const std::vector<u8>& values)
{
  std::vector<u8> hashed{file::STAT_FORMAT};
  hashed.insert(hashed.end(), values.begin(), values.end());
  return Sha1Hex(hashed.data(), hashed.size());
}

namespace
{
// A pack as the match start writes one (an object of name, fingerprint and values); empty: it is one, else why not.
std::string PackOfJson(const picojson::value& value, StatPack* pack)
{
  if (!value.is<picojson::object>())
    return "it is not an object";
  const auto& json = value.get<picojson::object>();
  const auto text = [&](const char* key) -> const std::string* {
    const auto member = json.find(key);
    return member != json.end() && member->second.is<std::string>() ? &member->second.get<std::string>() : nullptr;
  };
  const std::string* name = text("name");
  const std::string* fingerprint = text("fingerprint");
  const std::string* values = text("values");
  // (the longest text a whole table makes: four characters for three bytes)
  const std::optional<std::vector<u8>> bytes =
      values && values->size() <= ((stats::MOST_ENTRIES + consts::SLOTS) * file::STAT_ENTRY + 2) / 3 * 4
          ? Base64(*values)
          : std::nullopt;
  if (!name || !NameOfAPack(*name))
    return std::format("its name is not 1 to {} characters of plain ASCII, without a space at either end", MAX_STAT_NAME);
  if (!fingerprint || !values)
    return "it has no fingerprint or no values";
  if (!bytes)
    return "its values are not base64, or are more than the whole car data";
  std::string why;
  std::optional<Entries> entries = EntriesOf(*bytes, &why);
  if (!entries)
    return why;
  pack->entries = std::move(entries->cars);
  pack->constants = std::move(entries->constants);
  pack->fingerprint = StatFingerprint(*bytes);
  pack->name = *name;
  if (pack->fingerprint != *fingerprint)
    return std::format("its values' fingerprint is {}, and it says another", pack->fingerprint);
  return "";
}
}  // namespace

bool StatsOfSetup(const picojson::object& setup, const std::string& path, std::optional<StatPack>* out)
{
  out->reset();
  const auto it = setup.find("stats");
  if (it == setup.end() || it->second.is<picojson::null>())
    return true;
  StatPack pack;
  if (const std::string why = PackOfJson(it->second, &pack); !why.empty())
  {
    Error("fzgx: {}: the race's stat pack is refused: {}", path, why);
    return false;
  }
  *out = std::move(pack);
  return true;
}

bool PacksOfSetup(const picojson::object& setup, const std::string& path, std::vector<StatPack>* out)
{
  out->clear();
  const auto it = setup.find("packs");
  if (it == setup.end() || it->second.is<picojson::null>())
    return true;
  if (!it->second.is<picojson::array>() || it->second.get<picojson::array>().size() > MAX_SEAT_PACKS)
  {
    Error("fzgx: {}: the race's packs a seat are refused: not a list of at most {}", path, MAX_SEAT_PACKS);
    return false;
  }
  for (const picojson::value& value : it->second.get<picojson::array>())
  {
    StatPack pack;
    if (const std::string why = PackOfJson(value, &pack); !why.empty())
    {
      Error("fzgx: {}: the race's pack a seat {} is refused: {}", path, out->size() + 1, why);
      return false;
    }
    out->push_back(std::move(pack));
  }
  return true;
}
}  // namespace fzgx::boot
