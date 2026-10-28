// The fuzzer of the race set-ups of 0.3.0 (fuzz_replays.cpp says what the fuzzers do; tests/fuzz.sh builds them):
//   fuzz_core packs SECONDS SEED DIR   replay files of every version this core plays (2 to 8) whose header's match
//       carries what a server or a replay's maker chooses since stat packs: the race's pack ("setup.stats"), the
//       packs a seat ("setup.packs" and a seat's "pack"), custom machines (parts, pilot, colour words), the rules
//       (the mines' coming back among them), and the lineup's custom id. Each pack is made with its own fingerprint,
//       so that its entries reach the ranges: valid values, values at and past every range's ends, blocks, offsets
//       and constant slots at and past the tables' ends, entries out of order or twice, a double's word alone. The
//       header is then changed as the replay fuzzer changes one, and the file read by both readers and, when taken,
//       played against the fake game (fuzz.h) with car data laid out as the game's loader leaves it, through the go.
//       The same match is also read as a match start (files_match.cpp ReadMatch). Every fourth: fuzz_board.cpp.
// The rule, beside the process surviving: what a taken file or match hands the game is inside what the game has.
// Every machine byte of the race block is a machine (0 to 40) or the custom id on a seat with a custom entry; every
// custom entry's parts are of their kind and its pilot a machine; every seat's pack is one the race has; every
// entry of every pack is a word a pack may change, in its range; and after the playback the car data differs from
// what was laid out only in the words the race's pack names, each holding the pack's value.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz.h"

#include "const_ranges.h"
#include "files.h"
#include "stat_ranges.h"

namespace
{
// A file whose every choice is one the game takes (half of them): its race reaches the playback and the go. The
// others make a hostile choice now and then.
bool s_clean = false;

// A hostile choice, one in `n` (never in a clean file).
bool Off(Rng& rng, u32 n)
{
  return !s_clean && rng() % n == 0;
}

[[noreturn]] void Broken(const std::string& what)
{
  std::fprintf(stderr, "fuzz packs: BROKEN RULE: %s\n", what.c_str());
  std::fflush(stderr);
  std::abort();
}

u32 Bits(float v) { return std::bit_cast<u32>(v); }

float Between(Rng& rng, float low, float high)
{
  return low + (high - low) * static_cast<float>(rng() % 100001) / 100000.0f;
}

const u32 HOSTILE_BITS[] = {0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001, 0x007FFFFF, 0x3F800000,
                            0xBF800000, 0x4E6E6B28, 0xCE6E6B28, 0x461C4000, 0x461C4400, 0x40883127, 0x40883128,
                            0x3DCCCCCD, 0x3C23D70A, 0x3C23D709, 0xFFFFFFFF, 0x00010000, 0x03000000, 0xFF040000};

// A value for a block's word: in its range mostly, else at or past an end, else any bits.
u32 ValueFor(Rng& rng, u32 block, u32 at)
{
  const stats::Rule* rule = stats::RuleAt(at);
  const u32 way = s_clean ? 4 + rng() % 4 : rng() % 8;
  if (!rule || way == 0)
    return HOSTILE_BITS[rng() % std::size(HOSTILE_BITS)];
  if (way == 1)
    return rng();
  if (rule->kind == stats::Kind::Bytes)
    return (rng() % 256) << 24 | (rng() % (way == 2 ? 256 : 4)) << 16 | (way == 3 ? rng() % 2 : 0);
  const bool part = block >= stats::MACHINES;
  float low = part ? -rule->high : rule->low, high = rule->high;
  if (way == 2)
    return Bits(rng() % 2 ? high : low);
  if (way == 3)
    return Bits(rng() % 2 ? std::nextafter(high, 1e30f) : std::nextafter(low, -1e30f));
  float v = Between(rng, low, rule->below ? std::nextafter(high, -1e30f) : high);
  if (rule->kind == stats::Kind::Points && !part && (at - rule->at) / 4 % 3 == 2 && std::fabs(v) < rule->z_least)
    v = rule->z_least;
  return v == 0 ? 0 : Bits(v);
}

// A pack's entries as they travel (6 bytes each).
std::vector<u8> PackBytes(Rng& rng)
{
  std::vector<std::array<u32, 3>> entries;  // block, offset, value
  const u32 count = rng() % 10 == 0 ? rng() % 400 : 1 + rng() % 12;
  for (u32 i = 0; i < count; ++i)
  {
    u32 block = rng() % stats::BLOCKS, at = 4 + 4 * (rng() % stats::WORDS);
    if (Off(rng, 12))
    {
      const u32 blocks[] = {0, 40, 41, 115, 116, 117, 200, 239, 241, 255};
      block = blocks[rng() % std::size(blocks)];
    }
    if (Off(rng, 12))
    {
      const u32 ats[] = {0, 1, 2, 3, 0x48, 0x4A, 0xB0, 0xB4, 0xB8, 0xFC, 0xFF};
      at = ats[rng() % std::size(ats)];
    }
    entries.push_back({block, at, ValueFor(rng, block, at)});
  }
  // the code constants: a 32-bit one, the speed cap's two words, a slot past the list
  if (rng() % 3 == 0)
  {
    const u32 k = s_clean ? std::array<u32, 4>{0, 1, 3, 4}[rng() % 4] : rng() % 6;
    if (k == 0 || k == 3 || k == 4)
      entries.push_back({file::CONST_BLOCK, k, !Off(rng, 3) ? Bits(Between(rng, 0, k == 0 ? 1.7f : k == 3 ? 60 : 45)) :
                                                           HOSTILE_BITS[rng() % std::size(HOSTILE_BITS)]});
    else if (k == 1)
    {
      const double cap = !Off(rng, 3) ? 1000.0 + rng() % 100000 : rng() % 2 ? 999.0 : -1.0;
      const u64 bits = std::bit_cast<u64>(cap);
      entries.push_back({file::CONST_BLOCK, 1, static_cast<u32>(bits >> 32)});
      if (!Off(rng, 6))
        entries.push_back({file::CONST_BLOCK, 2, static_cast<u32>(bits)});
    }
    else if (k == 2)
      entries.push_back({file::CONST_BLOCK, 2, rng()});
    else
      entries.push_back({file::CONST_BLOCK, 5 + rng() % 251, rng()});
  }
  if (!Off(rng, 8))
  {
    std::sort(entries.begin(), entries.end());
    if (!Off(rng, 4))
      entries.erase(std::unique(entries.begin(), entries.end(),
                                [](const auto& a, const auto& b) { return a[0] == b[0] && a[1] == b[1]; }),
                    entries.end());
  }
  std::vector<u8> bytes;
  for (const auto& e : entries)
    bytes.insert(bytes.end(), {static_cast<u8>(e[0]), static_cast<u8>(e[1]), static_cast<u8>(e[2] >> 24),
                               static_cast<u8>(e[2] >> 16), static_cast<u8>(e[2] >> 8), static_cast<u8>(e[2])});
  return bytes;
}

std::string Base64(const std::vector<u8>& bytes)
{
  static const char* const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  for (size_t i = 0; i < bytes.size(); i += 3)
  {
    const size_t have = std::min<size_t>(3, bytes.size() - i);
    const u32 group = bytes[i] << 16 | (have > 1 ? bytes[i + 1] << 8 : 0) | (have > 2 ? bytes[i + 2] : 0);
    text += letters[group >> 18];
    text += letters[(group >> 12) & 63];
    text += have > 1 ? letters[(group >> 6) & 63] : '=';
    text += have > 2 ? letters[group & 63] : '=';
  }
  return text;
}

std::string Pack(Rng& rng, const std::string& name)
{
  const std::vector<u8> bytes = PackBytes(rng);
  std::string fingerprint = boot::StatFingerprint(bytes);
  if (Off(rng, 20))
    fingerprint[0] = fingerprint[0] == 'a' ? 'b' : 'a';
  return "{\"name\":\"" + name + "\",\"fingerprint\":\"" + fingerprint + "\",\"values\":\"" + Base64(bytes) + "\"}";
}

struct Made
{
  std::string file;
  std::string match;  // the header's match, for the match start's reader
  u32 steps = 0;
};

// Seat i of a race: its JSON (without the closing brace), its lineup byte, and whether it races a custom machine or
// names a pack of its own.
std::string Seat(Rng& rng, u32 i, u32 players, u32 packs, u8& lineup_byte, bool& any_custom, bool& any_seat_pack)
{
  const u32 machine = rng() % 41;
  lineup_byte = static_cast<u8>(machine);
  std::string seat = "{\"seat\":" + std::to_string(Off(rng, 16) ? rng() % 32 : i) + ",\"machine\":" + std::to_string(machine);
  if (i < players)
    seat += ",\"playerId\":\"p" + std::to_string(i) + "\",\"name\":\"R" + std::to_string(i) + "\"";
  if (rng() % 3 == 0)
  {
    // a custom machine: of its kinds mostly, a part or pilot past them now and then
    const bool off = Off(rng, 6);
    const u32 body = off ? rng() % 80 : rng() % 25, cockpit = off ? rng() % 80 : 25 + rng() % 25,
              booster = off ? rng() % 80 : 50 + rng() % 25, pilot = off ? rng() % 50 : rng() % 41;
    seat += ",\"custom\":{\"body\":" + std::to_string(body) + ",\"cockpit\":" + std::to_string(cockpit) +
            ",\"booster\":" + std::to_string(booster) + ",\"pilot\":" + std::to_string(pilot);
    if (rng() % 2 == 0)
    {
      seat += ",\"colors\":[";
      for (int c = 0; c < 3; ++c)
        seat += std::string(c ? "," : "") + std::to_string(rng() % 4 == 0 ? 0u : (rng() & 0xFFFFFF00u) | (Off(rng, 8) ? rng() % 256 : 1u));
      seat += "]";
    }
    seat += "}";
    lineup_byte = Off(rng, 8) ? static_cast<u8>(rng()) : static_cast<u8>(boot::CUSTOM_MACHINE);
    any_custom = true;
  }
  if (rng() % 3 == 0)
  {
    seat += ",\"pack\":" + std::to_string(Off(rng, 8) ? rng() % 8 : rng() % (packs + 1));
    any_seat_pack = true;
  }
  if (rng() % 2 == 0)
    seat += ",\"setting\":" + std::to_string(rng() % 101);
  return seat;
}

Made MakeFile(Rng& rng)
{
  s_clean = rng() % 2 == 0;
  const u32 racers = rng() % 4 == 0 ? 30 : 1 + rng() % 4;
  const u32 players = 1 + rng() % racers;
  const bool stats = rng() % 2 == 0;
  const u32 packs = rng() % 3 == 0 ? rng() % 6 : 0;
  const bool mines = rng() % 4 == 0;
  bool any_custom = false, any_seat_pack = packs != 0;
  std::array<u8, 30> lineup{};
  std::string seats;
  for (u32 i = 0; i < racers; ++i)
    seats += std::string(i ? "," : "") + Seat(rng, i, players, packs, lineup[i], any_custom, any_seat_pack) + "}";
  std::string setup = "\"course\":" + std::string(Off(rng, 10) ? std::to_string(rng() % 60) : "16") + ",\"laps\":" +
                      std::to_string(rng() % 4) + ",\"shuffleSeed\":4660,\"cpuSeed\":305419896,";
  if (stats)
    setup += "\"stats\":" + Pack(rng, "Race Pack") + ",";
  if (packs != 0 || rng() % 10 == 0)
  {
    setup += "\"packs\":[";
    for (u32 k = 0; k < packs; ++k)
      setup += std::string(k ? "," : "") + Pack(rng, "Seat Pack " + std::to_string(k));
    setup += "],";
  }
  setup += "\"rules\":{\"cpuLevel\":" + std::to_string(rng() % 5) + ",\"boostFromStart\":true,\"finishLimitSeconds\":" +
           std::to_string(rng() % 700) + (mines ? ",\"mineRegenMs\":" + std::to_string(rng() % 6000) : "") + "},";
  setup += "\"seats\":[" + seats + "]";
  Made made;
  made.match = "{\"matchId\":\"m-packs\",\"seat\":0,\"setup\":{" + setup + "}}";
  u32 mask = players == 30 ? 0x3FFFFFFF : (1u << players) - 1;
  std::string header = "{\"match\":" + made.match + ",\"seat\":0,\"mask\":" + std::to_string(mask) +
                       ",\"delay\":2,\"game\":\"GFZE01\",\"engine\":\"native\"}";
  if (Off(rng, 3))
  {
    picojson::value root;
    if (picojson::parse(root, header).empty())
    {
      int budget = 1 + static_cast<int>(rng() % 2);
      for (int tries = 0; tries < 20 && budget > 0; ++tries)
        Mutate(root, rng, budget);
      header = root.serialize();
    }
  }
  // the version the writer gives such a race (replay.cpp), or another now and then
  u32 version = any_seat_pack ? 6 : mines ? 5 : any_custom ? 4 : stats ? 3 : 2;
  if (Off(rng, 8))
    version = rng() % 9;
  made.steps = rng() % 50;
  std::string& f = made.file;
  f = "FZRP";
  f += static_cast<char>(version >> 8);
  f += static_cast<char>(version);
  Put32(f, static_cast<u32>(header.size()));
  f += header + "FZRF";
  Put32(f, mask);
  Put32(f, 0);
  std::string block(124, '\0');
  for (u32 i = 0; i < 30; ++i)
    block[i] = static_cast<char>(lineup[i]);
  if (Off(rng, 10))
    block[rng() % 30] = static_cast<char>(rng());
  f += "FZRL";
  Put32(f, 124);
  f += block;
  const std::size_t row = static_cast<std::size_t>(std::popcount(mask)) * 6 + 4;
  for (std::size_t i = 0; i < made.steps * row; ++i)
    f += static_cast<char>(rng() % 3 == 0 ? rng() : 0);
  return made;
}

// The car data as the game's loader leaves it (stat_packs.h LoadCarData): every block's word 0 its name's place,
// every other word a value of its own.
// A machine's word: inside its row, a little of its own by block; a part's a third of it (so the sum of three is a
// machine's), the corners 1 (the body's are raced as they are).
u32 Laid(u32 block, u32 at)
{
  const stats::Rule* rule = stats::RuleAt(at);
  if (!rule)
    return 0;
  if (rule->kind == stats::Kind::Bytes)
    return 0x02000000u | (block % 4) << 16;
  const bool part = block >= stats::MACHINES;
  if (rule->kind == stats::Kind::Points)
    return Bits(1.0f + static_cast<float>(block) / 1000.0f);
  if (at == 0x44 && part)
    return Bits(1.0f);  // (a body value is body x cockpit)
  const float v = (rule->low + (std::min(rule->high, rule->low + 10) - rule->low) * 0.3f) *
                  (1.0f + static_cast<float>(block) / 1000.0f);
  return Bits(part ? v / 3 : v);
}

void LoadCarData(Guest& g)
{
  g.Write32(net::MODULE_POINTER, MODULE);
  g.Write32(MODULE, game::MAIN_REL_ID);
  for (u32 block = 0; block < stats::BLOCKS; ++block)
  {
    const u32 at = game::CAR_DATA + stats::BlockAt(block);
    g.Write32(at, block < stats::MACHINES ? game::CAR_DATA_MAPPED + file::CAR_NAMES_AT + 13 * (stats::MACHINES - 1 - block)
                                           : game::CAR_DATA_MAPPED + file::CAR_PART_NAMES_AT);
    for (u32 offset = 4; offset < stats::BLOCK_SIZE; offset += 4)
      g.Write32(at + offset, Laid(block, offset));
  }
}

void HoldPack(const boot::StatPack& pack, const char* which)
{
  for (const boot::StatEntry& e : pack.entries)
  {
    if (e.block >= stats::BLOCKS || e.at == 0 || e.at % 4 != 0 || e.at >= stats::BLOCK_SIZE || !stats::RuleAt(e.at) ||
        stats::Refusal(e.block, e.at, e.value))
      Broken(std::format("{} pack {} holds block {} offset {} value {:08X}", which, pack.name, e.block, e.at, e.value));
  }
  for (std::size_t i = 0; i < pack.constants.size(); ++i)
  {
    const boot::StatEntry& e = pack.constants[i];
    if (e.block != file::CONST_BLOCK || e.at >= consts::SLOTS)
      Broken(std::format("{} pack {} holds constant block {} slot {}", which, pack.name, e.block, e.at));
    if (consts::CONSTANTS[e.at].kind == consts::Kind::Number64High &&
        (i + 1 >= pack.constants.size() || pack.constants[i + 1].at != e.at + 1 ||
         consts::Refusal(e.at, e.value, pack.constants[i + 1].value)))
      Broken(std::format("{} pack {}: a double's high word without its low word, or out of range", which, pack.name));
    if (consts::CONSTANTS[e.at].kind == consts::Kind::Number32 && consts::Refusal(e.at, e.value))
      Broken(std::format("{} pack {}: constant {} is {:08X}", which, pack.name, e.at, e.value));
  }
}

// What a taken race hands the game is inside what the game has.
void HoldMatch(const boot::Match& m, const char* which)
{
  for (u32 seat = 0; seat < 30; ++seat)
  {
    const u8 machine = m.race[32 + seat];
    const bool custom = boot::IsCustom(m.race, seat);
    if (machine > boot::MAX_MACHINE && !(machine == boot::CUSTOM_MACHINE && custom))
      Broken(std::format("{}: seat {} races machine {}", which, seat, machine));
    if (custom)
    {
      const u8* e = m.race.data() + boot::RACE_CUSTOM + seat * boot::CUSTOM_ENTRY;
      if (e[4] > 24 || e[12] < 25 || e[12] > 49 || e[20] < 50 || e[20] > 74 || e[28] > boot::MAX_MACHINE)
        Broken(std::format("{}: seat {}'s custom entry is parts {} {} {} pilot {}", which, seat, e[4], e[12], e[20], e[28]));
      for (int c = 0; c < 3; ++c)
      {
        const u32 word = paint::Be32(e + 8 + 8 * c);
        if (word != 0 && (word & 0xFF) != file::PART_PAINT_SLOT)
          Broken(std::format("{}: seat {}'s colour word {:08X}", which, seat, word));
      }
    }
    if (m.seat_pack[seat] < -1 || m.seat_pack[seat] > static_cast<int>(m.packs.size()))
      Broken(std::format("{}: seat {} races pack {} of {}", which, seat, m.seat_pack[seat], m.packs.size()));
  }
  if (paint::Be32(m.race.data() + 8) > boot::MAX_MACHINE && paint::Be32(m.race.data() + 8) != boot::CUSTOM_MACHINE)
    Broken(std::format("{}: port 0's machine {}", which, paint::Be32(m.race.data() + 8)));
  if (paint::Be32(m.race.data() + 12) > 29 || paint::Be32(m.race.data() + 16) > boot::MAX_LAPS)
    Broken(std::format("{}: opponents {} laps {}", which, paint::Be32(m.race.data() + 12), paint::Be32(m.race.data() + 16)));
  if (m.seat < 0 || m.seat > 29 || (m.player_mask >> 30) != 0)
    Broken(std::format("{}: seat {} mask {:08X}", which, m.seat, m.player_mask));
  if (m.stats)
    HoldPack(*m.stats, which);
  if (m.packs.size() > boot::MAX_SEAT_PACKS)
    Broken(std::format("{}: {} packs a seat", which, m.packs.size()));
  for (const boot::StatPack& p : m.packs)
    HoldPack(p, which);
  if (m.seat_block.size() != table::SEAT_BLOCK_SIZE)
    Broken(std::format("{}: the seats' block is {} bytes", which, m.seat_block.size()));
}

// A race let go: every custom seat races a machine the ranges hold, its sums and the words it takes from one part
// (the boost's factor of the weight and the 0x48 bytes left out).
void HoldCustomAtTheGo(const Guest& g, const boot::Match& m)
{
  for (u32 seat = 0; seat < 30; ++seat)
  {
    if (!boot::IsCustom(m.race, seat))
      continue;
    const u8* e = m.race.data() + boot::RACE_CUSTOM + seat * boot::CUSTOM_ENTRY;
    const auto word = [&](u32 part, u32 at) {
      return std::bit_cast<float>(g.Read32(game::CAR_DATA + stats::BlockAt(stats::MACHINES + part) + at));
    };
    for (u32 at = 4; at < stats::BLOCK_SIZE; at += 4)
    {
      if (at == file::CAR_BYTES_AT)
        continue;
      const bool body = at == 0x10 || at == 0x14 || at >= 0x54, cockpit = at == 0x4C || at == 0x50;
      const float v = body ? word(e[4], at) : cockpit ? word(e[12], at) : at == 0x44 ? word(e[4], at) * word(e[12], at)
                                                                                     : word(e[4], at) + word(e[12], at) + word(e[20], at);
      if (const auto why = stats::Refusal(0, at, std::bit_cast<u32>(v)))
        Broken(std::format("seat {} raced a custom machine whose {} {}", seat, stats::WordName(at), *why));
    }
  }
}

// After the playback: the car data differs from what was laid out only where the race's pack says.
void HoldTable(const Guest& g, const boot::Match& m)
{
  for (u32 block = 0; block < stats::BLOCKS; ++block)
  {
    for (u32 at = 4; at < stats::BLOCK_SIZE; at += 4)
    {
      u32 expected = Laid(block, at);
      if (m.stats)
      {
        for (const boot::StatEntry& e : m.stats->entries)
        {
          if (e.block == block && e.at == at)
            expected = e.value;
        }
      }
      if (g.Read32(game::CAR_DATA + stats::BlockAt(block) + at) != expected)
        Broken(std::format("the car data's block {} word {:02X} holds {:08X}, and the pack names {:08X}", block, at,
                           g.Read32(game::CAR_DATA + stats::BlockAt(block) + at), expected));
    }
  }
}
}  // namespace

void FuzzPacks(double seconds, u32 seed, const std::string& dir)
{
  Rng rng(seed);
  MakeBundle(dir);
  FuzzHost host;
  Totals n;
  unsigned long long matches = 0, went = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  const std::string path = dir + "/current.fzrp";
  while (std::chrono::steady_clock::now() < end)
  {
    const Made made = MakeFile(rng);
    WriteFile(path, made.file);
    WriteFile(dir + "/current-match.json", made.match);
    std::printf("packs %llu (seed %u, %zu bytes)\n", n.inputs, seed, made.file.size());
    std::fflush(stdout);
    alarm(40);
    ++n.inputs;
    host.stops = 0;
    Init(host, Options{});
    net::Recording recording;
    n.read += net::ReadRecording(path, &recording) ? 1 : 0;
    Options options;
    options.bundle_dir = dir;
    options.play_file = path;
    options.replay_scrubber = rng() % 3 == 0;  // the scrubber's two moments and its keys, in a third of the playbacks
    Init(host, options);
    if (Active())
    {
      ++n.taken;
      const boot::Match* m = boot::Get();
      if (!m)
        Broken("a playback is active with no race");
      HoldMatch(*m, "replay");
      Game game(host, rng, 0, 400);
      LoadCarData(game.g);
      FrameTick();
      u32 frames = 0;
      while (frames < made.steps + 200 && game.Frame())
        ++frames;
      if (game.g.Read32(table::RUNNING_AT) != 0)
      {
        ++went;
        if (m->stats && !m->stats->entries.empty())
          HoldCustomAtTheGo(game.g, *m);
      }
      HoldTable(game.g, *m);
      n.frames += frames;
    }
    Shutdown();
    // the same match as a match start, with a relay
    {
      picojson::value root;
      if (picojson::parse(root, made.match).empty() && root.is<picojson::object>())
      {
        auto& o = root.get<picojson::object>();
        picojson::object relay;
        relay["host"] = picojson::value(std::string("127.0.0.1"));
        relay["port"] = picojson::value(47811.0);
        o["relay"] = picojson::value(relay);
        o["token"] = picojson::value(std::string("1f2e3d4c5b6a7988"));
        Options plain;
        plain.bundle_dir = dir;
        Init(host, plain);
        if (const std::optional<boot::Match> m = boot::ReadMatch(root.serialize(), "fuzz"))
        {
          ++matches;
          HoldMatch(*m, "match");
        }
        Shutdown();
      }
    }
    if (n.inputs % 4 == 0)
      FuzzABoard(rng, host, dir);
    alarm(0);
  }
  Init(host, Options{});
  std::printf("fuzz packs: %llu files in %.0f s (seed %u): %llu taken by the session's reader, %llu set up and played "
              "(%llu displayed frames, %llu went past the go); %llu taken as a match start; no fault\n",
              n.inputs, seconds, seed, n.read, n.taken, n.frames, went, matches);
}
