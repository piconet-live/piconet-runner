// The reader of a match start's game mode (setup.mode: protocol/formats.md section 9), for mode.h. What it lets
// through the interpreter takes as it is: the sections whole, every rule's offsets inside the code, the parameters
// in their ranges. The bytecode's own operands are checked as it runs (a mode error), not here.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mode.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include "util.h"

namespace fzgx::mode
{
namespace
{
// The package's bytes, big-endian, read from the front; a read past the end makes it bad, and every later read 0.
struct Reader
{
  const u8* at;
  const u8* end;
  bool bad = false;

  bool Has(size_t n)
  {
    if (static_cast<size_t>(end - at) < n)
      bad = true;
    return !bad;
  }
  u8 U8()
  {
    return Has(1) ? *at++ : 0;
  }
  u16 U16()
  {
    if (!Has(2))
      return 0;
    const u16 v = static_cast<u16>(at[0] << 8 | at[1]);
    at += 2;
    return v;
  }
  u32 U32()
  {
    if (!Has(4))
      return 0;
    const u32 v = u32(at[0]) << 24 | u32(at[1]) << 16 | u32(at[2]) << 8 | u32(at[3]);
    at += 4;
    return v;
  }
  std::string Text()
  {
    const u16 n = U16();
    if (!Has(n))
      return {};
    std::string s(reinterpret_cast<const char*>(at), n);
    at += n;
    return s;
  }
};

constexpr const char* SECTIONS[] = {"HEAD", "PARM", "SETP", "VARS", "RULE", "CODE", "SRC "};
constexpr u8 TYPE_REAL = 1;

struct Param
{
  u8 type = 0;
  s32 least = 0, most = 0;
};

bool InRange(const Param& p, s32 value)
{
  if (p.type != TYPE_REAL)
    return p.least <= value && value <= p.most;
  const float v = std::bit_cast<float>(value);
  return std::bit_cast<float>(p.least) <= v && v <= std::bit_cast<float>(p.most);  // (a NaN is in no range)
}

bool Fail(std::string* why, std::string text)
{
  *why = std::move(text);
  return false;
}

// The package's sections into `p`; the parameters' values (`given`) checked against PARM.
bool ReadPackage(Package& p, const std::vector<s32>& given, std::string* why)
{
  Reader r{p.bytes.data(), p.bytes.data() + p.bytes.size()};
  if (!r.Has(4) || std::memcmp(r.at, "FZMP", 4) != 0)
    return Fail(why, "is not a mode package (no FZMP)");
  r.at += 4;
  if (r.U16() != 1)
    return Fail(why, "is of a package format this build does not read");
  if (r.U16() != p.api)
    return Fail(why, "names another api than its package");
  std::vector<s32> setup_args;
  for (const char* tag : SECTIONS)
  {
    if (!r.Has(8) || std::memcmp(r.at, tag, 4) != 0)
      return Fail(why, std::format("has no {} section where it should be", tag));
    r.at += 4;
    const u32 length = r.U32();
    if (!r.Has(length))
      return Fail(why, std::format("has a {} section past its end", tag));
    Reader s{r.at, r.at + length};
    r.at += length;
    const std::string_view name(tag);
    if (name == "HEAD")
    {
      p.name = s.Text();
    }
    else if (name == "PARM")
    {
      const u16 count = s.U16();
      if (given.size() != count)
        return Fail(why, std::format("gives {} parameters, and the mode has {}", given.size(), count));
      for (u16 i = 0; i < count && !s.bad; ++i)
      {
        s.Text();
        s.Text();
        s.U8();
        Param q;
        q.type = s.U8();
        q.least = static_cast<s32>(s.U32());
        q.most = static_cast<s32>(s.U32());
        s.U32();
        s.U32();
        for (u16 c = s.U16(); c > 0 && !s.bad; --c)
          s.Text();
        if (!s.bad && !InRange(q, given[i]))
          return Fail(why, std::format("gives parameter {} a value outside its range", i));
      }
      p.params = given;
    }
    else if (name == "SETP")
    {
      for (u16 count = s.U16(); count > 0 && !s.bad; --count)
      {
        const u8 id = s.U8();
        setup_args.clear();
        for (u8 n = s.U8(); n > 0 && !s.bad; --n)
        {
          const u8 source = s.U8();
          const s32 value = static_cast<s32>(s.U32());
          if (source == 1 && (value < 0 || static_cast<size_t>(value) >= p.params.size()))
            return Fail(why, "has a setup word whose parameter is not there");
          setup_args.push_back(source == 1 ? p.params[static_cast<size_t>(value)] : value);
        }
        const auto arg = [&](size_t k) { return k < setup_args.size() ? setup_args[k] : 0; };
        if (id == modes::SETUP_LAPS || id == modes::SETUP_LAPSBYCOURSE)
        {
          for (size_t k = 0; k < setup_args.size(); ++k)
            if ((k == 0 || k % 2 == 0) && (setup_args[k] < 0 || setup_args[k] > 99))
              return Fail(why, "names laps the game does not have");
          if (id == modes::SETUP_LAPS ? setup_args.size() != 1 : setup_args.size() % 2 != 1)
            return Fail(why, "has a laps word whose values are not its own");
          p.laps = static_cast<u32>(arg(0));
          for (size_t k = 1; k + 1 < setup_args.size(); k += 2)
            p.course_laps.emplace_back(static_cast<u32>(setup_args[k]), static_cast<u32>(setup_args[k + 1]));
        }
        else if (id == modes::SETUP_PEDALSLOCKED)
        {
          const float accel = std::bit_cast<float>(arg(0)), brake = std::bit_cast<float>(arg(1));
          if (!(accel >= 0.0f && accel <= 1.0f && brake >= 0.0f && brake <= 1.0f))
            return Fail(why, "locks the pedals outside 0 to 1");
          p.pedals = true;
          p.accel = static_cast<u32>(arg(0));
          p.brake = static_cast<u32>(arg(1));
        }
        else if (id == modes::SETUP_BOOSTOFF)
        {
          p.boost_off = true;
        }
        else if (id == modes::SETUP_FINISHLIMIT || id == modes::SETUP_MINEREGEN || id == modes::SETUP_CPULEVEL ||
                 id == modes::SETUP_CLOCKLIMIT)
        {
          // (the ranges are modes.toml's: the compiler holds a value to them, a package from elsewhere is refused)
          const s32 v = arg(0);
          const bool in = id == modes::SETUP_FINISHLIMIT ? v >= 0 && v <= 600
                          : id == modes::SETUP_MINEREGEN ? v >= 50 && v <= 5000
                          : id == modes::SETUP_CPULEVEL  ? v >= 0 && v <= 3
                                                         : v >= 60 && v <= 65535;
          if (setup_args.size() != 1 || !in)
            return Fail(why, std::format("has a setup word ({}) whose value is not its own", id));
          const u32 value = static_cast<u32>(v);
          if (id == modes::SETUP_FINISHLIMIT)
            p.finish_limit = value;
          else if (id == modes::SETUP_MINEREGEN)
            p.mine_regen = value;
          else if (id == modes::SETUP_CPULEVEL)
            p.cpu_level = value;
          else
            p.clock_limit = value;
        }
        else if (id == modes::SETUP_MINEREGENOFF)
        {
          p.mine_regen = 0;
        }
        else if (id == modes::SETUP_RESTOREON || id == modes::SETUP_RESTOREOFF)
        {
          p.restore = id == modes::SETUP_RESTOREON;
        }
        else if (id == modes::SETUP_CONTACTOFF)
        {
          p.contact_off = true;
        }
        else if (id == modes::SETUP_ROLLINGSTART)
        {
          const float v = std::bit_cast<float>(arg(0));
          if (setup_args.size() != 1 || !(v >= 0.0f && v <= MOST_SPEED))
            return Fail(why, "starts rolling at a speed outside 0 to 9000 km/h");
          p.rolling = static_cast<u32>(arg(0));
        }
        else if (id == modes::SETUP_SOLO)
        {
          p.solo = true;
        }
        else if (id == modes::SETUP_PAUSE)
        {
          p.pause = true;
        }
        else if (id == modes::SETUP_SAVESTATE)
        {
          p.save_state = true;
        }
        else if (id == modes::SETUP_REWIND || id == modes::SETUP_RESUMECOUNTDOWN)
        {
          const s32 v = arg(0);
          if (setup_args.size() != 1 || (id == modes::SETUP_REWIND ? v < 60 || v > 3600 : v < 0 || v > 300))
            return Fail(why, std::format("has a setup word ({}) whose value is not its own", id));
          (id == modes::SETUP_REWIND ? p.rewind : p.resume_countdown) = static_cast<u32>(v);
        }
        else if (id != modes::SETUP_CPUSALLOWED)  // (the coordinator's)
        {
          return Fail(why, std::format("has a setup word this build does not know ({})", id));
        }
      }
    }
    else if (name == "VARS")
    {
      const u16 globals = s.U16(), seat_words = s.U16();
      if (globals > modes::GLOBALS || seat_words > modes::SEAT_WORDS)
        return Fail(why, "keeps more words than the api's");
      for (u16 i = 0; i < globals; ++i)
        p.globals.push_back(static_cast<s32>(s.U32()));
      for (u16 i = 0; i < seat_words; ++i)
        p.seat_words.push_back(static_cast<s32>(s.U32()));
      if (s.at < s.end)  // a mode with words a team: their count and start values (none: nothing, as before teams)
      {
        const u16 team_words = s.U16();
        if (team_words == 0 || team_words > modes::TEAM_WORDS)
          return Fail(why, "keeps more words a team than the api's");
        for (u16 i = 0; i < team_words; ++i)
          p.team_words.push_back(static_cast<s32>(s.U32()));
      }
    }
    else if (name == "RULE")
    {
      const u16 count = s.U16();
      if (count > modes::RULES)
        return Fail(why, "has more rules than the api's");
      for (u16 i = 0; i < count && !s.bad; ++i)
      {
        Rule rule;
        rule.label = s.Text();
        rule.event = s.U8();
        s.U8();
        rule.locals = s.U16();
        rule.when = s.U32();
        rule.during = s.U32();
        rule.body = s.U32();
        if (rule.event >= modes::EVENT_COUNT || rule.locals < 1 || rule.locals > modes::LOCALS)
          return Fail(why, std::format("has a rule this build cannot run (\"{}\")", rule.label));
        p.rules.push_back(std::move(rule));
      }
    }
    else if (name == "CODE")
    {
      p.code.assign(s.at, s.end);
      s.at = s.end;
    }
    if (s.bad)
      return Fail(why, std::format("has a {} section that does not read", tag));
  }
  // the board's lines, in a mode that has them; a package's end otherwise, as every package before the board
  if (r.at < r.end)
  {
    if (!r.Has(8) || std::memcmp(r.at, "BORD", 4) != 0)
      return Fail(why, "has something past its last section");
    r.at += 4;
    const u32 length = r.U32();
    if (!r.Has(length) || length != static_cast<size_t>(r.end - r.at))
      return Fail(why, "has a BORD section that does not end the package");
    Reader s{r.at, r.end};
    std::array<u32, 3> in_scope{};
    for (u16 count = s.U16(); count > 0 && !s.bad; --count)
    {
      BoardLine line;
      line.scope = s.U8();
      line.type = s.U8();
      line.word = s.U16();
      line.label = s.Text();
      const size_t words = line.scope == 0 ? p.globals.size() : line.scope == 1 ? p.seat_words.size() : p.team_words.size();
      if (s.bad || line.scope > 2 || line.type > 4 || line.word >= words || line.label.empty() ||
          line.label.size() > static_cast<size_t>(modes::BOARD_LABEL) || ++in_scope[line.scope] > static_cast<u32>(modes::BOARD))
        return Fail(why, "has a board line this build cannot show");
      p.board.push_back(std::move(line));
    }
    if (s.bad || s.at != s.end)
      return Fail(why, "has a BORD section that does not read");
  }
  for (const Rule& rule : p.rules)
  {
    for (const u32 at : {rule.when, rule.during, rule.body})
      if (at != NONE && at >= p.code.size())
        return Fail(why, std::format("has a rule whose code is not there (\"{}\")", rule.label));
  }
  return true;
}
}  // namespace

std::optional<Package> Read(const picojson::value& mode, std::string* why)
{
  if (!mode.is<picojson::object>())
  {
    *why = "is not an object";
    return std::nullopt;
  }
  const picojson::object& o = mode.get<picojson::object>();
  const auto text = [&](const char* key) {
    const auto it = o.find(key);
    return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string{};
  };
  Package p;
  const auto api = o.find("api");
  const auto params = o.find("params");
  if (api == o.end() || !api->second.is<double>() || params == o.end() || !params->second.is<picojson::array>())
  {
    *why = "names no api or no parameters";
    return std::nullopt;
  }
  const double number = api->second.get<double>();
  if (!(number >= 1 && number <= modes::API) || number != static_cast<double>(static_cast<u32>(number)))
  {
    *why = std::format("is of api {}, and this build runs api {} and older", number, modes::API);
    return std::nullopt;
  }
  p.api = static_cast<u32>(number);
  std::vector<s32> given;
  for (const picojson::value& v : params->second.get<picojson::array>())
  {
    const double d = v.is<double>() ? v.get<double>() : 0.5;
    if (d < -2147483648.0 || d > 4294967295.0 || d != static_cast<double>(static_cast<std::int64_t>(d)))
    {
      *why = "has a parameter that is not a whole number";
      return std::nullopt;
    }
    given.push_back(static_cast<s32>(static_cast<u32>(static_cast<std::int64_t>(d))));
  }
  std::optional<std::vector<u8>> bytes = Base64Bytes(text("package"));
  if (!bytes || bytes->empty() || bytes->size() > static_cast<size_t>(modes::PACKAGE_BYTES))
  {
    *why = "has no package, or one larger than the api's";
    return std::nullopt;
  }
  if (const auto teams = o.find("teams"); teams != o.end() && !teams->second.is<picojson::null>())
  {
    const bool list = teams->second.is<picojson::array>();
    const picojson::array& given_teams = list ? teams->second.get<picojson::array>() : picojson::array{};
    if (!list || given_teams.size() > p.teams.size())
    {
      *why = "names its seats' teams otherwise than a list, a team a seat";
      return std::nullopt;
    }
    for (size_t i = 0; i < given_teams.size(); ++i)
    {
      const double t = given_teams[i].is<double>() ? given_teams[i].get<double>() : -1;
      if (!(t >= 0 && t < modes::TEAMS) || t != static_cast<double>(static_cast<u32>(t)))
      {
        *why = std::format("gives seat {} a team that is not 0 to {}", i, modes::TEAMS - 1);
        return std::nullopt;
      }
      p.teams[i] = static_cast<u8>(t);
    }
  }
  p.bytes = std::move(*bytes);
  p.hash = Sha256Hex(p.bytes.data(), p.bytes.size());
  if (p.hash != text("hash"))
  {
    *why = "names a hash that is not its package's";
    return std::nullopt;
  }
  if (!ReadPackage(p, given, why))
    return std::nullopt;
  Prepare(p);
  return p;
}

std::array<u32, 4> Seed(const std::string& match_id, const std::string& hash)
{
  std::vector<u8> text(match_id.begin(), match_id.end());
  for (size_t i = 0; i + 1 < hash.size(); i += 2)
    text.push_back(static_cast<u8>(std::stoul(hash.substr(i, 2), nullptr, 16)));
  const std::string digest = Sha256Hex(text.data(), text.size());
  std::array<u32, 4> state{};
  for (size_t i = 0; i < state.size(); ++i)
  {
    // SplitMix32, one step from the word
    u32 z = static_cast<u32>(std::stoul(digest.substr(8 * i, 8), nullptr, 16)) + 0x9E3779B9u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    state[i] = z ^ (z >> 16);
  }
  if ((state[0] | state[1] | state[2] | state[3]) == 0)
    state[0] = 1;  // (xoshiro's one state it never leaves)
  return state;
}
}  // namespace fzgx::mode
