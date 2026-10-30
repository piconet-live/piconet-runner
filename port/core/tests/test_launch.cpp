// Tests of the launch file's reader (files_launch.cpp): every key lands in its setting, a file without a "debug"
// part leaves every test setting off, and a key it does not know, a value of the wrong kind and a value outside its
// range are refused with the key's name.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
const char* const MATCH = R"("match": {"matchId": "m7", "seat": 1, "token": "1f2e3d4c5b6a7988", "futureMember": 1,
                 "relay": {"host": "", "port": 47811},
                 "setup": {"course": 8, "laps": 3, "seats": [{"seat": 0, "machine": 5, "playerId": "p1"}]}})";

// A launch file with `debug` as its "debug" part and `race` as the members of its "match" part beside the match.
std::string Launch(const std::string& debug, const std::string& race = "\"relayHost\": \"127.0.0.1\"")
{
  return std::string(R"({"about": "a test", "bundle": "b", "match": {)") + MATCH + (race.empty() ? "" : ", ") + race + "}" +
         (debug.empty() ? "" : ", \"debug\": " + debug) + "}";
}

// The sentence a text is refused with; empty when it is taken.
std::string Refused(const std::string& text)
{
  Options o;
  o.about = "untouched";
  const std::optional<std::string> problem = ParseLaunchFile(text, &o);
  CHECK(problem.has_value() == (o.about == "untouched"));  // a refused file changes nothing
  return problem.value_or("");
}

bool Says(const std::string& text, const std::string& part)
{
  const std::string problem = Refused(text);
  if (problem.find(part) == std::string::npos)
    std::printf("refused with: \"%s\", expected: %s\n", problem.c_str(), part.c_str());
  return problem.find(part) != std::string::npos;
}
}  // namespace

void TestALaunchFile()
{
  // every key
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "every key", "bundle": "C:\\game\\fzgx", "widescreen": false,
    "windowName": "Piconet", "showFrameRate": true, "rumble": false,
    "taskbar": {"id": "Piconet.Launcher", "program": "C:\\Piconet\\Launcher.App.exe", "name": "Piconet Launcher"},
    "board": "C:\\Piconet\\data\\boards\\game-1.board",
    "match": {)" + std::string(MATCH) + R"(, "relayHost": "relay.example", "delay": 1, "overclock": 4.5,
              "otherSounds": "off", "record": "race.fzrp"},
    "debug": {"testLagMs": 50, "jitterMs": 8, "autopilot": true, "forceRollback": {"every": 3, "depths": [1, 2, 17]},
              "rollbackLog": "rb.csv", "alterStep": 1500, "clock": "386D4381", "forcePatches": true,
              "singleCore": true, "lateSteps": 2, "trace": "trace.csv", "traceStates": [10, 20],
              "skipDraws": 3, "dump": {"step": 7, "prefix": "d"}, "keepScratch": true,
              "undoTrace": {"file": "undo.txt", "heaps": "804B56E0", "check": [100, 140]},
              "fullScreenWithoutFocus": true}})", &o));
  CHECK(o.about == "every key" && o.bundle_dir == "C:\\game\\fzgx" && !o.widescreen && o.launch_file.empty());
  CHECK(o.window_name == "Piconet" && o.show_frame_rate && o.window_name_picture.rgba.empty() && !o.rumble);
  CHECK(o.taskbar.id == "Piconet.Launcher" && o.taskbar.program == "C:\\Piconet\\Launcher.App.exe" &&
        o.taskbar.name == "Piconet Launcher");
  CHECK(o.relay_host == "relay.example" && o.delay == 1 && o.cpu_clock == 4.5f && o.other_sounds == "off");
  CHECK(o.replay_file == "race.fzrp" && o.board == "C:\\Piconet\\data\\boards\\game-1.board" && o.play_file.empty());
  // the match start goes on as it came, with a member the game does not know
  CHECK(o.match.find("\"matchId\":\"m7\"") != std::string::npos && o.match.find("futureMember") != std::string::npos);
  const Debug& d = o.debug;
  CHECK(d.lag_ms == 50 && d.jitter_ms == 8 && d.autopilot && d.force_every == 3 && d.force_depths == std::vector<u32>({1, 2, 17}));
  CHECK(d.rollback_log == "rb.csv" && d.alter_step == 1500 && d.clock == 0x386D4381 && d.force_patches && d.single_core);
  CHECK(d.late_steps == 2 && d.trace_file == "trace.csv" && d.trace_from == 10 && d.trace_to == 20);
  CHECK(d.dump_step == 7 && d.dump_prefix == "d" && d.keep_scratch && d.full_screen_without_focus && d.relay.empty());
  CHECK(d.skip_draws == 3);
  CHECK(d.undo_trace == "undo.txt" && d.undo_trace_heaps == 0x804B56E0 && d.undo_trace_from == 100 &&
        d.undo_trace_to == 140);
  CHECK(DebugSettingsOn(d).size() == 17 && DebugSettingsOn(d).front() == "debug.testLagMs");

  // no "debug" part: every test setting is off, and the game's settings are a player's
  Options plain;
  CHECK(!ParseLaunchFile(Launch(""), &plain));
  CHECK(DebugSettingsOn(plain.debug).empty() && plain.delay == 2 && plain.cpu_clock == 6.0f && plain.widescreen);
  // the game window's strip: no name unless the launcher gives one, and no frame rate unless the player asked
  CHECK(plain.window_name.empty() && !plain.show_frame_rate);
  // the controllers rumble unless the player switched it off
  CHECK(plain.rumble);
  const Debug off;
  const Debug& p = plain.debug;
  CHECK(p.lag_ms == off.lag_ms && p.jitter_ms == 0 && !p.autopilot && p.force_every == 0 && p.force_depths.empty() &&
        p.rollback_log.empty() && p.alter_step == 0 && p.clock == 0 && !p.force_patches && !p.single_core &&
        p.late_steps == 0 && p.trace_file.empty() && p.trace_to == 0 && p.dump_step == 0 && !p.keep_scratch &&
        !p.full_screen_without_focus && p.undo_trace.empty() && p.undo_trace_heaps == 0 && p.undo_trace_to == 0 &&
        p.skip_draws == 0);
  {
    TestHost host;
    Init(host, plain);
    const BootSettings boot = Boot();
    CHECK(boot.dual_core && boot.fixed_clock && boot.clock == 0x386D4380 && boot.cpu_clock == 6.0f);
    Init(host, Options{});
  }
  // an empty "debug" part, and settings named with their off value, are off too
  CHECK(!ParseLaunchFile(Launch("{}"), &plain) && DebugSettingsOn(plain.debug).empty());
  CHECK(!ParseLaunchFile(Launch(R"({"testLagMs": 0, "jitterMs": 0, "autopilot": false, "keepScratch": false})"), &plain) &&
        DebugSettingsOn(plain.debug).empty());
  CHECK(!ParseLaunchFile(Launch(R"({"singleCore": true})"), &plain) && DebugSettingsOn(plain.debug) ==
                                                                          std::vector<std::string>{"debug.singleCore"});
  // the picture shown: the emulator's own unless a test asks for Aurora's (presentation only), and its test pictures
  CHECK(plain.debug.renderer == "dolphin" && plain.debug.aurora_shots.empty() && plain.debug.aurora_log.empty());
  CHECK(!ParseLaunchFile(Launch(R"({"renderer": "aurora", "auroraShots": [600, 1200], "auroraLog": "C:\\runs\\a"})"),
                         &plain) &&
        plain.debug.renderer == "aurora" && plain.debug.aurora_shots == std::vector<u32>({600, 1200}) &&
        plain.debug.aurora_log == "C:\\runs\\a" &&
        DebugSettingsOn(plain.debug) ==
            std::vector<std::string>({"debug.renderer", "debug.auroraShots", "debug.auroraLog"}));
  CHECK(!ParseLaunchFile(Launch(R"({"renderer": "dolphin"})"), &plain) && DebugSettingsOn(plain.debug).empty());
  const std::optional<std::string> wrong = ParseLaunchFile(Launch(R"({"renderer": "vulkan"})"), &plain);
  CHECK(wrong && wrong->find("renderer") != std::string::npos);
  // a playback, and a plain start
  Options play;
  CHECK(!ParseLaunchFile(R"({"about": "a playback", "bundle": "b", "replay": {"file": "r.fzrp",
                             "otherSounds": "full"}, "debug": {"lateSteps": 16}})", &play));
  CHECK(play.play_file == "r.fzrp" && play.other_sounds == "full" && play.match.empty() && play.debug.late_steps == 16);
  CHECK(!ParseLaunchFile(R"({"about": "a playback", "bundle": "b", "replay": {"file": "r.fzrp"}})", &play) &&
        play.other_sounds.empty());
  Options start;
  CHECK(!ParseLaunchFile(" {\"about\": \"offline play\", \"bundle\": \"b\"}\r\n", &start) && start.match.empty() &&
        start.play_file.empty() && start.bundle_dir == "b");
  CHECK(!ParseLaunchFile(R"({"about": "a start with no bundle"})", &start) && start.bundle_dir.empty());

  // from a file: its name is kept for the log; a file that is not there, and one too long
  const std::string dir = Temp();
  WriteText(dir + "/launch.json", Launch(R"({"autopilot": true})"));
  Options read;
  CHECK(!ReadLaunchFile(dir + "/launch.json", &read) && read.launch_file == dir + "/launch.json" && read.debug.autopilot);
  CHECK(ReadLaunchFile(dir + "/none.json", &read).value_or("").find("cannot be read") != std::string::npos);
  WriteText(dir + "/launch.json", Launch("{}") + std::string(1 << 20, ' '));
  CHECK(ReadLaunchFile(dir + "/launch.json", &read).value_or("").find("longer than 1 MiB") != std::string::npos);
  WriteText(dir + "/launch.json", Launch(R"({"autopilot": 1})"));
  const std::string said = ReadLaunchFile(dir + "/launch.json", &read).value_or("");
  CHECK(said.find("the launch file is refused: \"debug.autopilot\" is not true or false") == 0 &&
        said.find(dir + "/launch.json") != std::string::npos);
  std::filesystem::remove_all(dir);
}

// "rumble" (the launcher's Rumble switch): true or false, at the top only; true when absent
void TestTheRumbleKey()
{
  Options o;
  CHECK(!ParseLaunchFile(Launch(""), &o) && o.rumble);
  CHECK(!ParseLaunchFile(R"({"about": "x", "rumble": false})", &o) && !o.rumble);
  CHECK(Says(R"({"about": "x", "rumble": "off"})", "\"rumble\" is not true or false"));
  CHECK(Says(Launch(R"({"rumble": false})"), "\"debug.rumble\" is not a setting of a launch file"));
  CHECK(Says(Launch("", "\"rumble\": false"), "\"match.rumble\" is not a setting of a launch file"));
}

void TestALaunchFileThatIsRefused()
{
  // not a launch file at all
  CHECK(Says("", "not plain JSON"));
  CHECK(Says("[]", "not plain JSON"));
  CHECK(Says("{\"about\": \"x\"} {}", "text after the object"));
  CHECK(Says("{\"about\": \"x\" // a comment\n}", "not plain JSON"));
  CHECK(Says("{\"about\": \"x\",}", "not plain JSON"));
  CHECK(Says(std::string(40, '[') + std::string(40, ']'), "nested more than 32 levels"));
  CHECK(Says("{}", "\"about\" is missing"));
  CHECK(Says(R"({"about": ""})", "\"about\" is empty"));
  CHECK(Says(R"({"about": 7})", "\"about\" is not text"));
  // a key it does not know, at every level it reads; never inside the coordinator's match start
  CHECK(Says(R"({"about": "x", "abuot": "y"})", "\"abuot\" is not a setting of a launch file"));
  CHECK(Says(R"({"about": "x", "autopilot": true})", "\"autopilot\" is not a setting of a launch file"));
  CHECK(Says(Launch(R"({"lateStep": 2})"), "\"debug.lateStep\" is not a setting of a launch file"));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 1, "depths": [1], "depth": 2}})"), "\"debug.forceRollback.depth\" is not"));
  CHECK(Says(Launch(R"({"dump": {"step": 1, "prefix": "p", "after": true}})"), "\"debug.dump.after\" is not"));
  CHECK(Says(Launch("", "\"relayHost\": \"h\", \"relayPort\": 1"), "\"match.relayPort\" is not a setting of a launch file"));
  CHECK(Says(R"({"about": "x", "bundle": "b", "replay": {"file": "r", "mode": "race"}})", "\"replay.mode\" is not"));
  CHECK(Refused(Launch("")).empty());
  // the two test settings a match file could hold: in "debug" only (no test setting rides in through a match)
  CHECK(Says(Launch("", "\"testLagMs\": 50"), "\"match.testLagMs\" is a test setting: it is \"debug.testLagMs\" now"));
  CHECK(Says(Launch("", "\"autopilot\": true"), "\"match.autopilot\" is a test setting: it is \"debug.autopilot\" now"));
  CHECK(Says(Launch("", "\"autopilot\": false"), "\"match.autopilot\" is a test setting"));
  // a value of the wrong kind
  CHECK(Says(Launch(R"({"autopilot": "yes"})"), "\"debug.autopilot\" is not true or false"));
  CHECK(Says(Launch(R"({"testLagMs": "50"})"), "\"debug.testLagMs\" is not a whole number from 0 to 10000"));
  CHECK(Says(Launch(R"({"testLagMs": 0.5})"), "\"debug.testLagMs\" is not a whole number"));
  CHECK(Says(Launch(R"({"rollbackLog": true})"), "\"debug.rollbackLog\" is not text"));
  CHECK(Says(Launch(R"({"rollbackLog": ""})"), "\"debug.rollbackLog\" is empty"));
  CHECK(Says(Launch(R"({"undoTrace": {"heaps": "804B56E0"}})"), "\"debug.undoTrace\" needs \"file\""));
  CHECK(Says(Launch(R"({"undoTrace": {"file": "u", "heaps": "1000"}})"), "is no address in the game's memory"));
  CHECK(Says(Launch(R"({"undoTrace": {"file": "u", "check": [9, 9]}})"), "is not [FROM, TO] with TO after FROM"));
  CHECK(Says(Launch(R"({"forceRollback": "1:3"})"), "\"debug.forceRollback\" is not an object"));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 1, "depths": 3}})"), "\"debug.forceRollback.depths\" is not a list"));
  CHECK(Says(Launch(R"({"traceStates": [1]})"), "\"debug.traceStates\" is not a list of 2 to 2 whole numbers"));
  CHECK(Says(Launch(R"({"clock": 946684800})"), "\"debug.clock\" is not text"));
  CHECK(Says(Launch("[]"), "\"debug\" is not an object"));
  CHECK(Says(R"({"about": "x", "widescreen": "on"})", "\"widescreen\" is not true or false"));
  CHECK(Says(R"({"about": "x", "showFrameRate": 1})", "\"showFrameRate\" is not true or false"));
  CHECK(Says(R"({"about": "x", "windowName": 7})", "\"windowName\" is not text"));
  CHECK(Says(R"({"about": "x", "windowName": ""})", "\"windowName\" is empty"));
  CHECK(Says(R"({"about": "x", "windowName": ")" + std::string(65, 'n') + "\"}", "\"windowName\" is longer than 64 bytes"));
  CHECK(Refused(R"({"about": "x", "windowName": ")" + std::string(64, 'n') + "\"}").empty());
  // the name as a picture: its pixels are exactly width x height x 4 bytes, as base64, and it is small
  CHECK(Says(R"({"about": "x", "windowNamePicture": "p.png"})", "\"windowNamePicture\" is not an object"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"width": 1, "height": 1, "rgba": "AAAA"}})", "\"windowNamePicture.rgba\" is not width x height x 4 bytes"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"width": 1, "height": 1, "rgba": "AAAAA*=="}})", "\"windowNamePicture.rgba\" is not"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"width": 513, "height": 1, "rgba": "AAAAAA=="}})", "\"windowNamePicture.width\" is not a whole number from 1 to 512"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"width": 1, "height": 65, "rgba": "AAAAAA=="}})", "\"windowNamePicture.height\" is not a whole number from 1 to 64"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"height": 1, "rgba": "AAAAAA=="}})", "\"windowNamePicture.rgba\" is not width"));
  CHECK(Says(R"({"about": "x", "windowNamePicture": {"width": 1, "height": 1, "rgba": "AAAAAA==", "file": "p"}})", "\"windowNamePicture.file\" is not a setting"));
  // the two are the file's own, as "widescreen" is: no test setting, and not a member of its parts
  CHECK(Says(Launch(R"({"showFrameRate": true})"), "\"debug.showFrameRate\" is not a setting of a launch file"));
  CHECK(Says(Launch("", "\"windowName\": \"n\""), "\"match.windowName\" is not a setting of a launch file"));
  CHECK(Says(R"({"about": "x", "bundle": 7})", "\"bundle\" is not text"));
  CHECK(Says(R"({"about": "x", "bundle": "b", "match": "m.json"})", "\"match\" is not an object"));
  CHECK(Says(R"({"about": "x", "bundle": "b", "match": {"relayHost": "h"}})", "\"match.match\" is missing"));
  CHECK(Says(R"({"about": "x", "bundle": "b", "match": {"match": []}})", "\"match.match\" is not an object"));
  CHECK(Says(Launch("", "\"relayHost\": 7"), "\"match.relayHost\" is not text"));
  CHECK(Says(Launch("", "\"record\": \"\""), "\"match.record\" is empty"));
  CHECK(Says(R"({"about": "x", "bundle": "b", "replay": {"otherSounds": "full"}})", "\"replay.file\" is missing"));
  // a value outside its range: refused, never bent into the range
  CHECK(Says(Launch(R"({"testLagMs": 10001})"), "\"debug.testLagMs\" is not a whole number from 0 to 10000"));
  CHECK(Says(Launch(R"({"testLagMs": -1})"), "\"debug.testLagMs\""));
  CHECK(Says(Launch(R"({"jitterMs": 1e9})"), "\"debug.jitterMs\""));
  CHECK(Says(Launch(R"({"lateSteps": 0})"), "\"debug.lateSteps\" is not a whole number from 1 to 20"));
  CHECK(Says(Launch(R"({"lateSteps": 21})"), "\"debug.lateSteps\" is not a whole number from 1 to 20"));
  CHECK(Says(Launch(R"({"alterStep": 0})"), "\"debug.alterStep\""));
  // the player's camera view: 1 to 3 only
  CHECK(Says(Launch("", "\"camera\": 0"), "\"match.camera\" is not a whole number from 1 to 3"));
  CHECK(Says(Launch("", "\"camera\": 4"), "\"match.camera\" is not a whole number from 1 to 3"));
  CHECK(Refused(Launch("", "\"camera\": 3")).empty());
  CHECK(Says(Launch(R"({"alterStep": 4294967296})"), "\"debug.alterStep\""));
  // a number too large to be one is refused before the parser, which ends the program on it, sees it
  CHECK(Says(Launch(R"({"jitterMs": 1e999})"), "a number too large to be one"));
  CHECK(Says(Launch("", "\"delay\": -1e999"), "a number too large to be one"));
  CHECK(Says("{\"about\": \"x\", \"match\": {\"match\": {\"laps\": " + std::string(400, '9') + "}}}", "a number too large"));
  CHECK(Refused(R"({"about": "in a text 1e999 and -1e999 are no numbers"})").empty());
  CHECK(Says(Launch(R"({"forceRollback": {"every": 0, "depths": [1]}})"), "\"debug.forceRollback.every\""));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 65536, "depths": [1]}})"), "\"debug.forceRollback.every\""));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 1, "depths": [1, 21]}})"), "\"debug.forceRollback.depths\""));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 1, "depths": []}})"), "\"debug.forceRollback.depths\""));
  CHECK(Says(Launch(R"({"forceRollback": {"every": 1}})"), "\"debug.forceRollback\" needs"));
  CHECK(Says(Launch(R"({"forceRollback": {"depths": [3]}})"), "\"debug.forceRollback\" needs"));
  CHECK(Says(Launch(R"({"clock": "0"})"), "\"debug.clock\" is not a real-time clock value"));
  CHECK(Says(Launch(R"({"clock": "386D43800"})"), "\"debug.clock\" is not a real-time clock value"));
  CHECK(Says(Launch(R"({"clock": "noon"})"), "\"debug.clock\" is not a real-time clock value"));
  CHECK(Says(Launch(R"({"trace": "t", "traceStates": [20, 20]})"), "\"debug.traceStates\" is [FROM, TO]"));
  CHECK(Says(Launch(R"({"traceStates": [1, 2]})"), "needs \"trace\""));
  CHECK(Says(Launch(R"({"dump": {"step": 3}})"), "\"debug.dump\" needs"));
  CHECK(Says(Launch(R"({"dump": {"step": 0, "prefix": "p"}})"), "\"debug.dump.step\""));
  CHECK(Says(Launch("", "\"delay\": 3"), "\"match.delay\" is not a whole number from 0 to 2"));
  CHECK(Says(Launch("", "\"overclock\": 17"), "\"match.overclock\" is not a number from 1 to 16"));
  CHECK(Says(Launch("", "\"otherSounds\": \"loud\""), "\"match.otherSounds\" is not one of full, quieter, off"));
  // every replay plays as the race: there is no key that chooses the game's replay mode
  CHECK(Says(R"({"about": "x", "bundle": "b", "replay": {"file": "r", "view": "race"}})", "\"replay.view\""));
  // a start is a race or a playback, and both need the bundle
  CHECK(Says(R"({"about": "x", "bundle": "b", "replay": {"file": "r"}, "match": {)" + std::string(MATCH) + "}}",
             "\"replay\" and \"match\" are both there"));
  CHECK(Says(R"({"about": "x", "replay": {"file": "r"}})", "\"bundle\" is missing"));
  CHECK(Says(R"({"about": "x", "match": {)" + std::string(MATCH) + "}}", "\"bundle\" is missing"));
}

// The emulator's side of "debug": the test settings of the runner's patched emulator, which it once read from the
// environment. Each key lands under the name the emulator knows it by; without the key there is nothing to hand on,
// so nothing but a launch file can turn one on.
void TestALaunchFileOfTheEmulatorsSide()
{
  // a player's start: nothing for the emulator's side, and the four things the runner must then refuse or leave unread
  Options plain;
  CHECK(!ParseLaunchFile(Launch(""), &plain));
  CHECK(plain.debug.emulator.empty() && !plain.debug.jit && !plain.debug.cheats && !plain.debug.mods &&
        !plain.debug.automation && !plain.debug.load_state && !plain.debug.user_settings);
  CHECK(!ParseLaunchFile(Launch(R"({"jit": false, "cheats": false, "mods": false, "automation": false, "loadState": false,
                                    "userSettings": false, "timebaseByCharge": false, "windowNoActivate": false,
                                    "windowControls": false})"), &plain));
  CHECK(plain.debug.emulator.empty() && DebugSettingsOn(plain.debug).empty());

  // every key, and the name and value the emulator gets
  Options o;
  CHECK(!ParseLaunchFile(Launch(R"({"jit": true, "cheats": true, "mods": true, "automation": true, "loadState": true,
    "userSettings": true, "pad": "0:0,1500:0100:142:128:128:255", "poke": "803E1EC0:1@300,803750B4:04@300-99999", "pokeTask": "803E1EC0:1",
    "pokeTaskByFrame": true, "timebaseByCharge": true, "noHookFlush": true, "noBusyWait": true, "noGatherInline": true,
    "noIcacheFast": true, "keepHostCall": true, "noReinterpretReuse": true, "fallbackRanges": "80003100-80004000,8059EF6C-805A0000",
    "lockstep": true, "lockstepStart": "0x100", "lockstepLimit": "5000", "lockstepFilter": "fn_8006", "lockstepNoDedup": true,
    "lockstepMaxReport": "20", "lockstepStepCap": "64", "lockstepTrace": "0x8006FE18", "lockstepRepeat": "0x80006388",
    "lockstepWhitelist": "0x80001000-0x80002000",
    "probe": "C:\\runs\\r\\probe.csv", "probeNoHash": true, "paceLog": "pace.csv", "taskTimers": true, "taskDumps": "300,600",
    "taskList": true, "dumpFields": "120,240", "dumpRam": true, "sample": "samples.txt", "sampleTasks": true,
    "samplePhysics": true, "census": true, "accessTrace": true, "noSlowLog": true, "voiceLog": "ax.bin",
    "dispatchSamples": true, "dispatchTrace": "dispatch.txt", "windowNoActivate": true, "windowControls": true})"), &o));
  const Debug& d = o.debug;
  CHECK(d.jit && d.cheats && d.mods && d.automation && d.load_state && d.user_settings && d.emulator.size() == 41);
  const auto value = [&d](const std::string& key, const std::string& name) {
    for (const EmulatorSetting& s : d.emulator)
    {
      if (s.key == key)
        return s.name == name ? s.value : "(under the name " + s.name + ")";
    }
    return std::string("(not given)");
  };
  CHECK(value("pad", "FZGX_PAD") == "0:0,1500:0100:142:128:128:255");
  CHECK(value("poke", "FZGX_POKE8") == "803E1EC0:1@300,803750B4:04@300-99999" && value("pokeTask", "FZGX_POKE8_TASK") == "803E1EC0:1");
  CHECK(value("pokeTaskByFrame", "FZGX_POKE8_TASK_BY") == "frame" && value("timebaseByCharge", "FZGX_TIMEBASE_BY_CHARGE") == "1");
  CHECK(value("noHookFlush", "FZGX_NO_HOOK_FLUSH") == "1" && value("noBusyWait", "FZGX_NO_BUSYWAIT") == "1");
  CHECK(value("noGatherInline", "FZGX_GATHER_INLINE") == "0" && value("noIcacheFast", "FZGX_ICACHE_FAST") == "0");
  CHECK(value("keepHostCall", "FZGX_KEEP_HOSTCALL") == "1" && value("noReinterpretReuse", "FZGX_REINTERPRET_REUSE") == "0");
  CHECK(value("fallbackRanges", "STATICRECOMP_FALLBACK_RANGES") == "80003100-80004000,8059EF6C-805A0000");
  CHECK(value("lockstep", "STATICRECOMP_LOCKSTEP") == "1" && value("lockstepStart", "STATICRECOMP_LOCKSTEP_START") == "0x100");
  CHECK(value("lockstepLimit", "STATICRECOMP_LOCKSTEP_LIMIT") == "5000" && value("lockstepFilter", "STATICRECOMP_LOCKSTEP_FILTER") == "fn_8006");
  CHECK(value("lockstepNoDedup", "STATICRECOMP_LOCKSTEP_NODEDUP") == "1" && value("lockstepMaxReport", "STATICRECOMP_LOCKSTEP_MAXREPORT") == "20");
  CHECK(value("lockstepStepCap", "STATICRECOMP_LOCKSTEP_STEPCAP") == "64" && value("lockstepTrace", "STATICRECOMP_LOCKSTEP_TRACE") == "0x8006FE18");
  CHECK(value("lockstepRepeat", "STATICRECOMP_LOCKSTEP_REPEAT") == "0x80006388");
  CHECK(value("lockstepWhitelist", "STATICRECOMP_LOCKSTEP_WHITELIST") == "0x80001000-0x80002000");
  CHECK(value("probe", "FZGX_PROBE") == "C:\\runs\\r\\probe.csv" && value("probeNoHash", "FZGX_PROBE_HASH") == "0");
  CHECK(value("paceLog", "FZGX_PACE_LOG") == "pace.csv" && value("taskTimers", "FZGX_TASKPROF") == "1");
  CHECK(value("taskDumps", "FZGX_TASKDUMP") == "300,600" && value("taskList", "FZGX_TASKLIST") == "1");
  CHECK(value("dumpFields", "FZGX_DUMP_FIELDS") == "120,240" && value("dumpRam", "FZGX_DUMP_RAM") == "1");
  CHECK(value("sample", "FZGX_SAMPLE") == "samples.txt" && value("sampleTasks", "FZGX_SAMPLE_TASKS") == "1");
  CHECK(value("samplePhysics", "FZGX_SAMPLE_PHYSICS") == "1" && value("census", "FZGX_CENSUS") == "1");
  CHECK(value("accessTrace", "FZGX_TRACE") == "1" && value("noSlowLog", "FZGX_SLOW_LOG") == "0");
  CHECK(value("voiceLog", "FZGX_AXLOG") == "ax.bin" && value("dispatchSamples", "STATICRECOMP_DISPATCH_SAMPLES") == "1");
  CHECK(value("dispatchTrace", "STATICRECOMP_TRACE_FILE") == "dispatch.txt" && value("windowNoActivate", "FZGX_WINDOW_NOACTIVATE") == "1");
  CHECK(value("windowControls", "FZGX_WINDOW_CONTROLS") == "1");
  // the log's first line names each: 6 of the runner's and the 41
  const std::vector<std::string> on = DebugSettingsOn(d);
  CHECK(on.size() == 47 && on.front() == "debug.jit" && on[5] == "debug.userSettings" && on[6] == "debug.pad" &&
        on.back() == "debug.windowControls");
  // the other kind of two settings that share a name
  CHECK(!ParseLaunchFile(Launch(R"({"accessTraceNoInterrupts": true, "slowLogEverySecond": true, "pad": "neutral"})"), &o));
  CHECK(o.debug.emulator.size() == 3 && o.debug.emulator[0].value == "neutral" && o.debug.emulator[1].name == "FZGX_TRACE" &&
        o.debug.emulator[1].value == "noirq" && o.debug.emulator[2].name == "FZGX_SLOW_LOG" && o.debug.emulator[2].value == "all");

  // refused: the variable's own name is no key, a value of the wrong kind, a script with a stray character, two
  // settings of one thing
  CHECK(Says(Launch(R"({"FZGX_PAD": "0:0"})"), "\"debug.FZGX_PAD\" is not a setting of a launch file"));
  CHECK(Says(Launch(R"({"jitt": true})"), "\"debug.jitt\" is not a setting of a launch file"));
  CHECK(Says(Launch(R"({"jit": 1})"), "\"debug.jit\" is not true or false"));
  CHECK(Says(Launch(R"({"cheats": "on"})"), "\"debug.cheats\" is not true or false"));
  CHECK(Says(Launch(R"({"automation": "C:\\auto"})"), "\"debug.automation\" is not true or false"));
  CHECK(Says(Launch(R"({"loadState": "race300.sav"})"), "\"debug.loadState\" is not true or false"));
  CHECK(Says(Launch(R"({"mods": []})"), "\"debug.mods\" is not true or false"));
  CHECK(Says(Launch(R"({"pad": true})"), "\"debug.pad\" is not text"));
  CHECK(Says(Launch(R"({"pad": ""})"), "\"debug.pad\" is empty"));
  CHECK(Says(Launch(R"({"pad": "0:0;1500:0100"})"), "\"debug.pad\" holds a character that is none of"));
  CHECK(Says(Launch(R"({"pad": "netural"})"), "(and is not \"neutral\")"));
  CHECK(Says(Launch(R"({"poke": "803E1EC0=1"})"), "\"debug.poke\" holds a character"));
  CHECK(Says(Launch(R"({"taskDumps": [300, 600]})"), "\"debug.taskDumps\" is not text"));
  CHECK(Says(Launch(R"({"taskDumps": "300 600"})"), "\"debug.taskDumps\" holds a character"));
  CHECK(Says(Launch(R"({"fallbackRanges": "all"})"), "\"debug.fallbackRanges\" holds a character"));
  CHECK(Says(Launch(R"({"probe": 1})"), "\"debug.probe\" is not text"));
  CHECK(Says(Launch(R"({"timebaseByCharge": "1"})"), "\"debug.timebaseByCharge\" is not true or false"));
  CHECK(Says(Launch(R"({"accessTrace": true, "accessTraceNoInterrupts": true})"),
             "\"debug.accessTraceNoInterrupts\" and \"accessTrace\" are two settings of one thing"));
  CHECK(Says(Launch(R"({"noSlowLog": true, "slowLogEverySecond": true})"), "are two settings of one thing"));
  // nor does one ride in through the match
  CHECK(Says(Launch("", "\"pad\": \"0:0\""), "\"match.pad\" is not a setting of a launch file"));
}

// What a player's game does not have and a test may ask for: the window's controls that change the game (saved
// states, Pause, fast forward) and the user folder's own emulator settings. A launch file without the two keys is a
// player's: the window has no such control, and the runner sets the settings that decide a race whatever the user
// folder says. Each is on only by its own key, and no other spelling passes as one.
void TestTheWindowsControlsAndTheUsersSettings()
{
  Options player;
  CHECK(!ParseLaunchFile(Launch(""), &player));
  CHECK(!player.debug.user_settings && player.debug.emulator.empty());
  // each of the two alone: the window's controls are the emulator's to turn on, the settings the runner's to leave
  Options one;
  CHECK(!ParseLaunchFile(Launch(R"({"windowControls": true})"), &one));
  CHECK(one.debug.emulator.size() == 1 && one.debug.emulator[0].name == "FZGX_WINDOW_CONTROLS" &&
        one.debug.emulator[0].value == "1" && !one.debug.user_settings);
  CHECK(DebugSettingsOn(one.debug) == std::vector<std::string>{"debug.windowControls"});
  CHECK(!ParseLaunchFile(Launch(R"({"userSettings": true})"), &one));
  CHECK(one.debug.emulator.empty() && one.debug.user_settings);
  CHECK(DebugSettingsOn(one.debug) == std::vector<std::string>{"debug.userSettings"});
  // neither rides in with another test setting, nor through the match
  CHECK(!ParseLaunchFile(Launch(R"({"autopilot": true, "automation": true, "probe": "p.csv", "windowNoActivate": true})"), &one));
  CHECK(!one.debug.user_settings && one.debug.emulator.size() == 2);
  for (const EmulatorSetting& given : one.debug.emulator)
    CHECK(given.name != "FZGX_WINDOW_CONTROLS");
  CHECK(Says(Launch("", "\"windowControls\": true"), "\"match.windowControls\" is not a setting of a launch file"));
  CHECK(Says(Launch("", "\"userSettings\": true"), "\"match.userSettings\" is not a setting of a launch file"));
  // refused by name: a value of the wrong kind, and a key that is none
  CHECK(Says(Launch(R"({"userSettings": "Dolphin.ini"})"), "\"debug.userSettings\" is not true or false"));
  CHECK(Says(Launch(R"({"windowControls": "F1"})"), "\"debug.windowControls\" is not true or false"));
  CHECK(Says(Launch(R"({"windowMenu": true})"), "\"debug.windowMenu\" is not a setting of a launch file"));
  CHECK(Says(Launch(R"({"hotkeys": true})"), "\"debug.hotkeys\" is not a setting of a launch file"));
}

// What the runner's command line asked for and only a launch file's "debug" part allows.
void TestARunnerOptionALaunchFileMustAllow()
{
  Options player, test;
  CHECK(!ParseLaunchFile(Launch(""), &player));
  CHECK(!ParseLaunchFile(Launch(R"({"jit": true})"), &test));
  // nothing asked: nothing refused, with or without a "debug" part
  CHECK(!RefusedRunnerOption(player.debug, RunnerAsked{}) && !RefusedRunnerOption(test.debug, RunnerAsked{}));
  // the JIT: a game without a native module is the tests' reference, never a player's game
  RunnerAsked jit;
  jit.interpreter = true;
  const std::string said = RefusedRunnerOption(player.debug, jit).value_or("");
  CHECK(said.find("--allow-interpreter is refused") == 0 && said.find("\"jit\": true") != std::string::npos);
  CHECK(!RefusedRunnerOption(test.debug, jit));
  // another test setting does not allow it
  Options other;
  CHECK(!ParseLaunchFile(Launch(R"({"autopilot": true, "timebaseByCharge": true})"), &other));
  CHECK(RefusedRunnerOption(other.debug, jit).has_value());
  // a mod folder, the automation folder, a saved state: each refused by its own name, each allowed by its own key
  struct Case
  {
    bool RunnerAsked::*asked;
    const char* option;
    const char* key;
  };
  for (const Case& c : {Case{&RunnerAsked::mods, "--mods", "mods"}, Case{&RunnerAsked::automation, "--automation-dir", "automation"},
                        Case{&RunnerAsked::load_state, "--load-state", "loadState"}})
  {
    RunnerAsked asked;
    asked.*c.asked = true;
    const std::string refused = RefusedRunnerOption(player.debug, asked).value_or("");
    CHECK(refused.find(std::string(c.option) + " is refused") == 0 &&
          refused.find(std::string("\"") + c.key + "\": true") != std::string::npos);
    CHECK(RefusedRunnerOption(test.debug, asked).has_value());   // "jit" allows none of them
    Options allowed;
    CHECK(!ParseLaunchFile(Launch(std::string("{\"") + c.key + "\": true}"), &allowed));
    CHECK(!RefusedRunnerOption(allowed.debug, asked));
    CHECK(RefusedRunnerOption(allowed.debug, jit).has_value());
  }
  // the tests' start: all of them asked and all allowed
  Options all;
  CHECK(!ParseLaunchFile(Launch(R"({"jit": true, "mods": true, "automation": true, "loadState": true})"), &all));
  RunnerAsked everything;
  everything.interpreter = everything.mods = everything.automation = everything.load_state = true;
  CHECK(!RefusedRunnerOption(all.debug, everything) && RefusedRunnerOption(player.debug, everything).has_value());
}

// Every named launch file of port/configs (the tests' and the gates' set-ups, filled in by
// port/gates/runs/launch-file.py) is one the reader takes: a key that has gone fails here, not in a gate run.
void TestTheNamedLaunchFiles()
{
  const std::filesystem::path configs = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "configs";
  int files = 0, with_debug = 0;
  for (const auto& entry : std::filesystem::directory_iterator(configs))
  {
    if (entry.path().extension() != ".json")
      continue;
    std::ifstream file(entry.path(), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // the places a run fills in
    for (std::size_t at = text.find('{', 1); at != std::string::npos; at = text.find('{', at + 1))
    {
      const std::size_t end = text.find('}', at);
      if (end != std::string::npos && end > at + 1 && text.find_first_not_of("abcdefghijklmnopqrstuvwxyz_", at + 1) == end)
        text.replace(at, end - at + 1, "filled");
    }
    Options o;
    const std::optional<std::string> problem = ParseLaunchFile(text, &o);
    if (problem)
      std::printf("%s: %s\n", entry.path().filename().string().c_str(), problem->c_str());
    CHECK(!problem);
    ++files;
    with_debug += DebugSettingsOn(o.debug).empty() ? 0 : 1;
  }
  CHECK(files >= 15 && with_debug >= 11);
}
