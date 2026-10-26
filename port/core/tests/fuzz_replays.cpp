// Fuzzing of what the core reads from other people: replay files and the
// relay's packets. Built by tests/fuzz.sh with the address and undefined-behaviour sanitizers; a structured mutation
// loop (no coverage feedback: neither libFuzzer nor AFL is installed here), every input made by this program.
//
//   fuzz_core replay SECONDS SEED DIR   replay files: a file this program makes (a header as the core writes it, the
//       frame preamble, the lineup, steps, results) is changed (a value of the header's JSON swapped for a hostile
//       one, a length or a mark overwritten, bytes turned, the file cut or padded), read by both of the core's
//       readers (files_replay.cpp: ReadReplay through fzgx::Init, and ReadRecording), and, when it is taken, played
//       back to its end against a fake game, as the race, sometimes with forced
//       rollbacks or late inputs (the rollback path). With every replay one launch file (fuzz_launch.cpp): a file
//       with every key, changed the same ways, read by files_launch.cpp and, when it is taken, set up as its race.
//   fuzz_core relay SECONDS SEED DIR    the relay's packets: a match race against a relay that is this program, on
//       the loopback. The relay keeps the race going (joined, go, official frames) and sends hostile packets with
//       them: seats and seat masks outside the race, official frames far in the past and the future, counts and
//       lengths that do not fit, clocks from nowhere, and noise.
//
// The rule: the process survives (no sanitizer report, no exception, no step that takes more than 40 s). The input
// being read is on disk before it is read (DIR/current.*), so what stopped the process is there afterwards; the
// last line of the output names the iteration.
//   fuzz_core packs SECONDS SEED DIR    replay files and match starts with stat packs, packs a seat and custom
//       machines (fuzz_packs.cpp says what it makes and the rules it holds the readers to).
// This file: the replay fuzzer and the program's main. fuzz_relay.cpp: the relay fuzzer. fuzz_launch.cpp: the
// launch file. fuzz.h: what they use.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz.h"

namespace
{
const u32 LENGTHS[] = {0, 1, 5, 7, 8, 121, 122, 124, 0x7FFFFFFF, 0x80000000, 0xFFFFFFF7, 0xFFFFFFF8, 0xFFFFFFFC, 0xFFFFFFFF, 1u << 20};
const char* const HOSTILE[] = {"null", "true", "[]", "{}", "\"\"", "0", "-1", "29", "30", "31", "40", "41", "255", "256", "65536",
                               "2147483647", "2147483648", "4294967295", "4294967296", "-2147483649", "1e19", "1e300", "-1e300",
                               "0.5", "\"7\"", "[1]", "{\"r\":300,\"g\":-1,\"b\":\"x\"}", "1073741824", "3221225472"};

}  // namespace

// One value of the JSON (at any depth) swapped for a hostile one, a member dropped, or an array grown.
void Mutate(picojson::value& v, Rng& rng, int& budget)
{
  if (budget <= 0)
    return;
  const auto hostile = [&] {
    picojson::value out;
    picojson::parse(out, std::string(HOSTILE[rng() % std::size(HOSTILE)]));
    if (rng() % 16 == 0)
      out = picojson::value(std::string(1u << (rng() % 16), 'A'));
    return out;
  };
  if (v.is<picojson::object>())
  {
    auto& o = v.get<picojson::object>();
    if (o.empty())
      return;
    auto it = o.begin();
    std::advance(it, rng() % o.size());
    if (rng() % 3 == 0)
    {
      --budget;
      if (rng() % 5 == 0)
        o.erase(it);
      else
        it->second = hostile();
    }
    else
    {
      Mutate(it->second, rng, budget);
    }
  }
  else if (v.is<picojson::array>())
  {
    auto& a = v.get<picojson::array>();
    if (a.empty())
      return;
    const std::size_t at = rng() % a.size();
    if (rng() % 4 == 0)
    {
      --budget;
      if (rng() % 3 == 0)
        a.insert(a.end(), 1u << (rng() % 7), a[at]);
      else if (rng() % 3 == 0)
        a.clear();
      else
        a[at] = hostile();
    }
    else
    {
      Mutate(a[at], rng, budget);
    }
  }
  else if (rng() % 2 == 0)
  {
    --budget;
    v = hostile();
  }
}

namespace
{
struct MadeReplay
{
  std::string bytes;
  u32 steps = 0;
};

MadeReplay MakeReplay(Rng& rng)
{
  const u32 racers = rng() % 4 == 0 ? 30 : 2 + rng() % 3;
  const u32 players = rng() % 3 == 0 ? racers : 1 + rng() % racers;
  std::string seats;
  for (u32 i = 0; i < racers; ++i)
  {
    seats += std::string(i ? "," : "") + "{\"seat\":" + std::to_string(i) + ",\"machine\":" + std::to_string(rng() % 41) +
             (i < players ? ",\"playerId\":\"p" + std::to_string(i) + "\",\"name\":\"Racer " + std::to_string(i) + "\"" : ",\"name\":\"CPU\"") +
             (rng() % 3 == 0 ? ",\"color\":{\"r\":10,\"g\":200,\"b\":255}" : rng() % 3 == 0 ? ",\"stockColor\":2" : "") +
             (rng() % 2 == 0 ? ",\"setting\":70" : "") + "}";
  }
  u32 mask = players == 30 ? 0x3FFFFFFF : (1u << players) - 1;
  u32 seat = rng() % players;
  std::string header = "{\"match\":{\"matchId\":\"m9\",\"seat\":" + std::to_string(seat) + ",\"setup\":{\"course\":21,\"laps\":3," +
                       "\"shuffleSeed\":4660,\"cpuSeed\":305419896," +
                       (rng() % 2 ? "\"rules\":{\"cpuLevel\":3,\"boostFromStart\":true,\"restore\":false}," : "") + "\"seats\":[" + seats +
                       "]}},\"seat\":" + std::to_string(seat) + ",\"mask\":" + std::to_string(mask) +
                       ",\"delay\":2,\"mod_sha1\":\"aba727f7\",\"game\":\"GFZE01\",\"engine\":\"native\"}";
  if (rng() % 2 == 0)
  {
    picojson::value root;
    if (picojson::parse(root, header).empty())
    {
      int budget = 1 + static_cast<int>(rng() % 3);
      for (int tries = 0; tries < 20 && budget > 0; ++tries)
        Mutate(root, rng, budget);
      header = root.serialize();
    }
  }
  if (rng() % 8 == 0)
    mask = rng() % 2 ? LENGTHS[rng() % std::size(LENGTHS)] : mask ^ (1u << (rng() % 32));
  if (rng() % 8 == 0)
    seat = rng() % 2 ? LENGTHS[rng() % std::size(LENGTHS)] : rng() % 40;
  MadeReplay made;
  made.steps = rng() % 6 == 0 ? rng() % 400 : rng() % 60;
  std::string& file = made.bytes;
  file = "FZRP";
  file += static_cast<char>(0);
  file += static_cast<char>(rng() % 40 == 0 ? rng() % 4 : 2);
  Put32(file, rng() % 10 == 0 ? LENGTHS[rng() % std::size(LENGTHS)] : static_cast<u32>(header.size()));
  file += header + "FZRF";
  Put32(file, mask);
  Put32(file, seat);
  if (rng() % 12 != 0)
  {
    std::string lineup(rng() % 10 == 0 ? rng() % 200 : 124, '\0');
    for (u32 i = 0; i < std::min<u32>(racers, static_cast<u32>(lineup.size())); ++i)
      lineup[i] = static_cast<char>(rng() % 10 == 0 ? rng() : rng() % 41);
    for (u32 i = 30; i < std::min<u32>(30 + racers, static_cast<u32>(lineup.size())); ++i)
      lineup[i] = static_cast<char>(rng() % 10 == 0 ? rng() : (i - 30));
    for (std::size_t i = 60; i < lineup.size(); ++i)
      lineup[i] = static_cast<char>(rng() % 6 == 0 ? rng() : 0);
    file += "FZRL";
    Put32(file, rng() % 10 == 0 ? LENGTHS[rng() % std::size(LENGTHS)] : static_cast<u32>(lineup.size()));
    file += lineup;
  }
  const std::size_t row = static_cast<std::size_t>(std::popcount(mask)) * 6 + 4;
  std::string steps(made.steps * row + (rng() % 12 == 0 ? rng() % row : 0), '\0');
  for (char& c : steps)
    c = static_cast<char>(rng() % 3 == 0 ? rng() : 0);
  file += steps;
  if (rng() % 3 != 0)
  {
    const std::string results = "{\"results\":[{\"seat\":0,\"rank\":1,\"finished\":true,\"out\":false,\"laps\":3,\"timeMs\":130655,\"bestLapMs\":64210}]}";
    file += results;
    Put32(file, rng() % 6 == 0 ? LENGTHS[rng() % std::size(LENGTHS)] : static_cast<u32>(results.size()));
    file += rng() % 20 == 0 ? "FZRX" : "FZRS";
  }
  if (rng() % 3 == 0)
  {
    for (u32 changes = 1 + rng() % 4; changes > 0 && !file.empty(); --changes)
    {
      const std::size_t at = rng() % file.size();
      switch (rng() % 4)
      {
      case 0: file[at] = static_cast<char>(file[at] ^ (1 << (rng() % 8))); break;
      case 1: file[at] = static_cast<char>(rng()); break;
      case 2: file[at] = static_cast<char>(rng() % 2 ? 0 : 0xFF); break;
      default:
        if (at + 4 <= file.size())
        {
          std::string word;
          Put32(word, LENGTHS[rng() % std::size(LENGTHS)]);
          file.replace(at, 4, word);
        }
      }
    }
  }
  if (rng() % 8 == 0)
    file.resize(rng() % (file.size() + 1));
  else if (rng() % 12 == 0)
    file.append(rng() % 64, static_cast<char>(rng()));
  return made;
}

}  // namespace

void FuzzReplays(double seconds, u32 seed, const std::string& dir)
{
  Rng rng(seed);
  MakeBundle(dir);
  FuzzHost host;
  Totals n;
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  const std::string path = dir + "/current.fzrp";
  while (std::chrono::steady_clock::now() < end)
  {
    const MadeReplay made = MakeReplay(rng);
    WriteFile(path, made.bytes);
    std::printf("replay %llu (seed %u, %zu bytes)\n", n.inputs, seed, made.bytes.size());
    std::fflush(stdout);
    alarm(40);
    ++n.inputs;
    host.stops = 0;
    Options options;
    options.bundle_dir = dir;
    Init(host, Options{});
    net::Recording recording;
    n.read += net::ReadRecording(path, &recording) ? 1 : 0;
    options.play_file = path;
    const u32 way = rng() % 12;
    if (way == 0)
    {
      options.debug.force_every = 1 + rng() % 5;
      options.debug.force_depths = {1 + rng() % 17, 1 + rng() % 17};
    }
    else if (way == 1)
    {
      options.debug.late_steps = 1 + rng() % 16;
    }
    Init(host, options);
    if (Active())
    {
      ++n.taken;
      FrameTick();
      const u32 from = rng() % 30, to = from + rng() % 400;
      Game game(host, rng, from, to);
      u32 frames = 0;
      while (frames < made.steps + 420 && game.Frame())
        ++frames;
      n.frames += frames;
      n.stopped += host.stops != 0 ? 1 : 0;
    }
    Shutdown();
    FuzzLaunchFile(rng, host, dir, n);
    alarm(0);
  }
  Init(host, Options{});
  std::printf("fuzz replay: %llu files in %.0f s (seed %u): %llu taken by the session's reader, %llu set up and played "
              "(%llu displayed frames, %llu to the replay's own end); %llu launch files, %llu taken, %llu set up as a "
              "race; no fault\n",
              n.inputs, seconds, seed, n.read, n.taken, n.frames, n.stopped, n.launch, n.launch_taken, n.launch_raced);
}

int main(int argc, char** argv)
{
  if (argc != 5 || (std::string(argv[1]) != "replay" && std::string(argv[1]) != "relay" && std::string(argv[1]) != "packs"))
  {
    std::fprintf(stderr, "fuzz_core replay|relay|packs SECONDS SEED DIR\n");
    return 2;
  }
  std::signal(SIGALRM, [](int) {
    const char text[] = "fuzz: the input took too long (a hang)\n";
    (void)!write(2, text, sizeof(text) - 1);
    std::abort();
  });
  const double seconds = std::stod(argv[2]);
  const u32 seed = static_cast<u32>(std::stoul(argv[3]));
  if (std::string(argv[1]) == "replay")
    FuzzReplays(seconds, seed, argv[4]);
  else if (std::string(argv[1]) == "packs")
    FuzzPacks(seconds, seed, argv[4]);
  else
    FuzzRelay(seconds, seed, argv[4]);
  return 0;
}
