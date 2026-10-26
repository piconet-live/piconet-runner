// The launch file under the fuzzer (fuzz_replays.cpp says what the fuzzers do; it calls this once for every replay):
// a launch file with every key, changed (a value at any depth swapped for a hostile one, a member dropped, a list
// grown, bytes turned, the text cut), read by files_launch.cpp and, when it is taken, set up as its race or playback
// by fzgx::Init, which reads its match (files_match.cpp). The input is DIR/current.json before it is read.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz.h"

namespace
{
const char* const LAUNCH = R"({"about": "a fuzzed start", "bundle": "BUNDLE", "widescreen": true,
  "match": {"match": {"matchId": "m7", "seat": 1, "token": "1f2e3d4c5b6a7988", "relay": {"host": "127.0.0.1", "port": 47811},
                      "setup": {"course": 8, "laps": 3, "shuffleSeed": 4660, "cpuSeed": 305419896,
                                "rules": {"cpuLevel": 2, "boostFromStart": true, "restore": false},
                                "seats": [{"seat": 0, "machine": 5, "playerId": "p1", "name": "one", "setting": 70,
                                           "color": {"r": 255, "g": 102, "b": 0}},
                                          {"seat": 1, "machine": 12, "playerId": "p2", "name": "two", "stockColor": 2},
                                          {"seat": 2, "machine": 30, "cpu": true}]}},
            "relayHost": "127.0.0.1", "delay": 2, "overclock": 6.0, "otherSounds": "quieter", "record": "RECORD"},
  "board": "BOARD",
  "debug": {"testLagMs": 50, "jitterMs": 8, "autopilot": true, "forceRollback": {"every": 3, "depths": [1, 2, 17]},
            "rollbackLog": "ROLLBACKLOG", "alterStep": 1500, "clock": "386D4381", "forcePatches": true, "singleCore": true,
            "lateSteps": 2, "trace": "TRACE", "traceStates": [10, 20], "dump": {"step": 7, "prefix": "DUMP"},
            "keepScratch": true}})";
}  // namespace

void FuzzLaunchFile(Rng& rng, FuzzHost& host, const std::string& dir, Totals& n)
{
  picojson::value file;
  picojson::parse(file, std::string(LAUNCH));
  auto& members = file.get<picojson::object>();
  members["bundle"] = picojson::value(dir);
  if (rng() % 4 == 0)
  {
    // a playback instead of the race, of the replay the caller just made
    members.erase("match");
    picojson::object replay;
    replay["file"] = picojson::value(dir + "/current.fzrp");
    members["replay"] = picojson::value(replay);
  }
  if (rng() % 3 == 0)
    members.erase("debug");
  if (rng() % 8 == 0)
    members[rng() % 2 ? "autopilot" : "tesLagMs"] = picojson::value(true);  // a key that is none
  int budget = static_cast<int>(rng() % 4);
  for (int tries = 0; tries < 12 && budget > 0; ++tries)
    Mutate(file, rng, budget);
  std::string text = file.serialize();
  if (rng() % 8 == 0 && !text.empty())
    text[rng() % text.size()] = static_cast<char>(rng());
  if (rng() % 16 == 0)
    text.resize(rng() % (text.size() + 1));
  if (rng() % 32 == 0)
    text = std::string(rng() % 70, '[') + text;
  const std::string path = dir + "/current.json";
  WriteFile(path, text);
  ++n.launch;
  Options options;
  if (ReadLaunchFile(path, &options))
    return;
  ++n.launch_taken;
  // what a session's start would open is kept inside DIR, whatever the file names
  options.replay_file.clear();
  options.debug.rollback_log.clear();
  options.debug.trace_file.clear();
  options.debug.dump_step = 0;
  if (!options.board.empty())
    options.board = dir + "/current.board";  // (no such file: the board is said and left)
  Init(host, options);
  n.launch_raced += Active() ? 1 : 0;
  (void)Boot();
  (void)DebugSettingsOn(options.debug);
  Shutdown();
}
