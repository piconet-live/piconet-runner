// The launch file (--config FILE; protocol/formats.md section 2): every setting of one start of the game, read
// here into fzgx::Options and nowhere else. Its members are described once, in protocol/launch.toml: the generated
// reader (launch_file.g.h) takes each member of a plain kind into its place; this file takes the objects and the
// members of a form of their own, and holds every rule between members. Plain JSON, one object: "about", "bundle",
// the picture's and the game window's settings, then "match" (a race: what the launcher wrote as the match file) or
// "replay" (a playback) or neither (a plain start; "controllerSetup": one that opens the game's own controller
// options), and "debug" (the test settings; without it every one of them is off).
// A key this reader does not know, a value of the wrong kind and a value outside its range are refused with the
// key's name, and the game does not start: a setting that was mistyped or has gone must not pass as "off". Only the
// coordinator's match start itself ("match.match") may hold members the game does not know: it is the server's
// message as it came, and files_match.cpp checks what the game takes from it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"
#include "files_launch_part.h"
#include "launch_file.g.h"

#include <algorithm>
#include <cstdio>

namespace fzgx
{
namespace
{
using launch::Part;
using launch::ReadEmulatorSide;
using launch::ReadLanesOff;
using launch::ReadPartialKeys;
using launch::ReadRedraw;
using launch::ReadStandStill;
using launch::RefuseRemovedKeys;


// "debug.slowSteps": the frames a hit a rollback reveals is slowed in (hits.cpp), FROM-TO.
void ReadSlowSteps(Part& p, Debug* d)
{
  std::string slow;
  p.Words("slowSteps", &slow);
  if (!slow.empty() && (std::sscanf(slow.c_str(), "%u-%u", &d->slow_from, &d->slow_to) != 2 || d->slow_from == 0 ||
                        d->slow_to < d->slow_from))
    p.Refuse("slowSteps", "is not FROM-TO, two steps, the first not 0 and not past the second");
}

// "debug": the test settings (fzgx::Debug says what each does). The generated reader takes the plain ones; here the
// objects and the members of a form of their own, in the schema's order.
void ReadDebug(Part& file, Debug* d, std::string* problem)
{
  const picojson::object* members = file.Object("debug");
  if (!members)
    return;
  Part p(*members, file.Under("debug"), problem);
  launch_file::ReadDebug(p, d);
  if (const picojson::object* force = p.Object("forceRollback"))
  {
    Part f(*force, p.Under("forceRollback"), problem);
    launch_file::ReadForceRollback(f, d);
    if (d->force_every == 0 || d->force_depths.empty())
      p.Refuse("forceRollback", "needs \"every\" (frames) and \"depths\" (steps, each at most 20)");
    f.Done();
  }
  std::string clock;
  p.Words("clock", &clock);
  if (!clock.empty() && (clock.size() > 8 || !TryParse(clock, &d->clock, 16) || d->clock == 0))
    p.Refuse("clock", "is not a real-time clock value: 1 to 8 hexadecimal digits, not zero");
  ReadRedraw(p, d, problem);
  RefuseRemovedKeys(p);
  std::vector<u32> states;
  p.Wholes("traceStates", &states, 0, 0xFFFFFFFF, 2, 2);
  if (states.size() == 2)
  {
    d->trace_from = states[0];
    d->trace_to = states[1];
    if (d->trace_to <= d->trace_from || d->trace_file.empty())
      p.Refuse("traceStates", "is [FROM, TO] with TO after FROM, and needs \"trace\"");
  }
  if (const picojson::object* dump = p.Object("dump"))
  {
    Part f(*dump, p.Under("dump"), problem);
    launch_file::ReadDump(f, d);
    if (d->dump_step == 0 || d->dump_prefix.empty())
      p.Refuse("dump", "needs \"step\" and \"prefix\"");
    f.Done();
  }
  if (d->replay_script.find_first_not_of("0123456789:,pjhesfkrnvuibwc") != std::string::npos)
    p.Refuse("replayScript",
             "is not FRAME:WHAT[:N],... (p, j:STEP, h, e, s:STEP, f:N, k:N, r, n, v, u, i, b:PAD, w:RACER, c:KIND)");
  ReadSlowSteps(p, d);
  if (!d->undo_log.empty() && d->undo_log != "guard" && d->undo_log != "scan" && d->undo_log != "both")
    p.Refuse("undoLog", "is \"guard\", \"scan\" or \"both\"");
  if (const picojson::object* trace = p.Object("undoTrace"))
  {
    Part f(*trace, p.Under("undoTrace"), problem);
    launch_file::ReadUndoTrace(f, d);
    std::string heaps;
    f.Words("heaps", &heaps);
    if (d->undo_trace.empty())
      p.Refuse("undoTrace", "needs \"file\"");
    if (!heaps.empty() && (heaps.size() > 8 || !TryParse(heaps, &d->undo_trace_heaps, 16) ||
                           d->undo_trace_heaps < layout::RAM ||
                           d->undo_trace_heaps > layout::WINDOW - 0x100))
      p.Refuse("undoTrace", "has \"heaps\" that is no address in the game's memory (hexadecimal)");
    std::vector<u32> check;
    f.Wholes("check", &check, 0, 0xFFFFFFFF, 2, 2);
    if (check.size() == 2)
    {
      d->undo_trace_from = check[0];
      d->undo_trace_to = check[1];
      if (d->undo_trace_to <= d->undo_trace_from || d->undo_trace_to - d->undo_trace_from > 4096)
        p.Refuse("undoTrace", "has \"check\" that is not [FROM, TO] with TO after FROM by 4,096 steps at most");
    }
    f.Done();
  }
  ReadStandStill(p, d, problem);
  ReadPartialKeys(p, d, problem);
  ReadEmulatorSide(p, d);
  p.Done();
}

// "match": a race, as the launcher wrote the match file, with where it is recorded and where its live feed goes.
// The two test settings a match file could hold are "debug" settings now and are refused here by name.
void ReadRace(Part& file, Options* o, std::string* problem)
{
  const picojson::object* members = file.Object("match");
  if (!members)
    return;
  Part p(*members, file.Under("match"), problem);
  for (const char* moved : {"testLagMs", "autopilot"})
  {
    if (members->count(moved) != 0)
      p.Refuse(moved, std::string("is a test setting: it is \"debug.") + moved + "\" now");
  }
  // (the delay is 0, 1 or 2, the player's choice: more is refused, whoever wrote the file. A playback reads no delay
  // from a launch file: its recording's header names the one it was recorded with, boot::MAX_DELAY at most)
  launch_file::ReadMatch(p, o);
  if (const picojson::object* match = p.Object("match"))
    o->match = picojson::value(*match).serialize();
  p.Done();
}

// "windowNamePicture": the strip's name as the launcher draws it (its lettering comes from the player's disc, which
// the runner does not read for it): {"width", "height", "rgba"}, the pixels as base64, at most 512 by 64.
void ReadNamePicture(Part& file, WindowPicture* out, std::string* problem)
{
  const picojson::object* members = file.Object("windowNamePicture");
  if (!members)
    return;
  Part p(*members, file.Under("windowNamePicture"), problem);
  struct
  {
    u32 width = 0, height = 0;
    std::string rgba;
  } given;
  launch_file::ReadWindowNamePicture(p, &given);
  p.Done();
  WindowPicture picture;
  picture.width = given.width;
  picture.height = given.height;
  std::optional<std::vector<u8>> pixels = Base64Bytes(given.rgba);
  if (!pixels || pixels->size() != std::size_t{picture.width} * picture.height * 4)
    return p.Refuse("rgba", "is not width x height x 4 bytes as base64");
  picture.rgba = std::move(*pixels);
  *out = std::move(picture);
}

// "taskbar": the launcher's button on the Windows taskbar, which the game window joins: {"id", "program", "name"},
// all three. They go to Windows as they are (the id of a taskbar button, the command and the name of a pinned
// one), so each is held to what it can be: an id of the characters Windows' own form has (no space); a program
// file of this computer by its whole path (a drive letter, so no network path and no path that depends on where
// the game was started), ending in .exe, with no quotation mark in it; a name of one line. That the file is there
// is ReadLaunchFile's to check, which reads the disk.
bool ProgramPath(const std::string& path)
{
  const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
  const std::size_t n = path.size();
  return n >= 8 && letter(path[0]) && path[1] == ':' && path[2] == '\\' && path[n - 4] == '.' &&
         (path[n - 3] | 0x20) == 'e' && (path[n - 2] | 0x20) == 'x' && (path[n - 1] | 0x20) == 'e';
}

void ReadTaskbar(Part& file, TaskbarGroup* out, std::string* problem)
{
  const picojson::object* members = file.Object("taskbar");
  if (!members)
    return;
  Part p(*members, file.Under("taskbar"), problem);
  TaskbarGroup group;
  const auto one_line = [](const std::string& text) {
    return std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F || c == '"'; });
  };
  launch_file::ReadTaskbar(p, &group);
  p.Done();
  const bool id_form = std::all_of(group.id.begin(), group.id.end(), [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
  });
  if (!id_form || group.id.size() > launch_file::TASKBAR_ID_MOST_BYTES)
    p.Refuse("id", "is not an application id: at most 128 letters, digits, '.', '-' and '_'");
  if (!one_line(group.program) || group.program.size() > launch_file::TASKBAR_PROGRAM_MOST_BYTES || !ProgramPath(group.program))
    p.Refuse("program", "is not a program file of this computer by its whole path: a drive letter, one line of at "
                        "most 1024 bytes ending in .exe, no quotation mark");
  if (!one_line(group.name) || group.name.size() > launch_file::TASKBAR_NAME_MOST_BYTES)
    p.Refuse("name", "is not a name: one line of at most " + std::to_string(launch_file::TASKBAR_NAME_MOST_BYTES) + " bytes");
  *out = std::move(group);
}

// "replay": a replay's playback. `scrubber_named`: the file says whether the scrubber is on.
void ReadPlayback(Part& file, Options* o, std::string* problem, bool* scrubber_named)
{
  const picojson::object* members = file.Object("replay");
  if (!members)
    return;
  Part p(*members, file.Under("replay"), problem);
  *scrubber_named = members->count("scrubber") != 0;
  o->replay_scrubber = true;  // on unless the file says false
  launch_file::ReadReplay(p, o);
  p.Done();
}
}  // namespace

std::optional<std::string> ParseLaunchFile(const std::string& text, Options* out)
{
  picojson::value root;
  std::string error;
  if (boot::TooDeep(text))
    return "it is nested more than 32 levels deep";
  if (boot::TooLarge(text))
    return "it holds a number too large to be one";
  const auto end = picojson::parse(root, text.begin(), text.end(), &error);
  if (!error.empty() || !root.is<picojson::object>())
    return "it is not plain JSON with one object (" + (error.empty() ? std::string("no object") : error) + ")";
  if (text.find_first_not_of(" \t\r\n", static_cast<std::size_t>(end - text.begin())) != std::string::npos)
    return "it is not plain JSON with one object (text after the object)";
  Options o;
  std::string problem;
  Part file(root.get<picojson::object>(), "", &problem);
  launch_file::ReadFile(file, &o);
  if (o.true_widescreen && !o.widescreen)
    file.Refuse("trueWidescreen", "is true with \"widescreen\": false: true widescreen is a way of the game's 16:9 mode");
  if (o.window_name.size() > launch_file::WINDOW_NAME_MOST_BYTES)
    file.Refuse("windowName", "is longer than " + std::to_string(launch_file::WINDOW_NAME_MOST_BYTES) + " bytes");
  ReadNamePicture(file, &o.window_name_picture, &problem);
  ReadTaskbar(file, &o.taskbar, &problem);
  ReadLanesOff(file);
  ReadRace(file, &o, &problem);
  bool scrubber_named = false;
  ReadPlayback(file, &o, &problem, &scrubber_named);
  ReadDebug(file, &o.debug, &problem);
  file.Done();
  // the scrubber runs a playback's race its own way: a test of the rollback path (forced rollbacks, late inputs)
  // has it off, and a file that asks for both is refused
  const bool rollback_test = o.debug.force_every != 0 || o.debug.late_steps != 0;
  if (o.play_file.empty() || (rollback_test && !scrubber_named))
    o.replay_scrubber = false;
  if (o.replay_scrubber && rollback_test)
    file.Refuse("replay", "has \"scrubber\": true with \"debug.forceRollback\" or \"debug.lateSteps\": the scrubber "
                          "runs the race its own way, without those tests' rollbacks");
  if (!o.replay_scrubber && (o.debug.buffer_steps != 0 || !o.debug.replay_script.empty() || o.debug.replay_end_quits))
    file.Refuse("debug", "names a test of the replay scrubber (\"bufferSteps\", \"replayScript\", \"replayEndQuits\"), "
                         "which is off in this start");
  if (!o.match.empty() && !o.play_file.empty())
    file.Refuse("replay", "and \"match\" are both there: a start is a race or a playback");
  if (o.controller_setup && (!o.match.empty() || !o.play_file.empty()))
    file.Refuse("controllerSetup", std::string("and \"") + (o.match.empty() ? "replay" : "match") +
                                       "\" are both there: the game's controller options open in a plain start");
  if ((!o.match.empty() || !o.play_file.empty()) && o.bundle_dir.empty())
    file.Refuse("bundle", "is missing: a race or a replay needs the mod bundle");
  if (!problem.empty())
    return problem;
  *out = std::move(o);
  return std::nullopt;
}

std::optional<std::string> ReadLaunchFile(const std::string& path, Options* out)
{
  std::string text;
  if (!ReadFileToString(path, text))
    return "the launch file cannot be read: " + path;
  if (text.size() > launch_file::MAX_BYTES)
    return "the launch file is longer than 1 MiB and was not read: " + path;
  if (std::optional<std::string> problem = ParseLaunchFile(text, out))
    return "the launch file is refused: " + *problem + " (" + path + ")";
  // the launcher's program file, which a pinned taskbar button would start: Windows is told of none that is not there
  if (!out->taskbar.id.empty() && !IsFile(out->taskbar.program))
    return "the launch file is refused: \"taskbar.program\" is not a file that is there: " + out->taskbar.program + " (" + path + ")";
  out->launch_file = path;
  return std::nullopt;
}

}  // namespace fzgx
