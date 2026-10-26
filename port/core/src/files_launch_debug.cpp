// The launch file's "debug" part, its keys the reader takes by hand (launch.toml's `hand` members and objects), each
// kind by a function of its own: the settled switches' keys (refused), the high refresh rate's re-draw, the
// per-machine rollback's settings, the freeze on purpose, the emulator's side, and the file's "lanes". ReadDebug (files_launch.cpp) calls them after the generated
// reader's keys (launch_file.g.h); in a file of their own for that file's length only.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"
#include "files_launch_part.h"
#include "launch_file.g.h"

#include <algorithm>

namespace fzgx::launch
{
// The settled A/B switches: each kept a former way for comparison, and that way is gone. A file asking for one is
// refused, as the parked lanes are.
void RefuseRemovedKeys(Part& p)
{
  for (const char* key : {"pacedLoad", "drawSkipOff", "hitShowOff", "spectateLive"})
    if (p.Take(key))
      p.Refuse(key, "was removed on 2026-10-25: the former way it kept is gone");
}

// "debug.redraw": the high refresh rate's re-draw (VideoCommon/FrameRedraw.h).
void ReadRedraw(Part& p, Debug* d, std::string* problem)
{
  const picojson::object* redraw = p.Object("redraw");
  if (!redraw)
    return;
  Part f(*redraw, p.Under("redraw"), problem);
  launch_file::ReadRedraw(f, d);
  std::string at;
  f.Words("at", &at);
  if (!at.empty() && at != "copy" && at != "drawDone")
    p.Refuse("redraw", "\"at\" is \"copy\" or \"drawDone\"");
  d->redraw_at_draw_done = at == "drawDone";
  if (d->redraw_replays == 0)
    p.Refuse("redraw", "needs \"replays\" (1 to 3)");
  f.Done();
}

// The file's "lanes": 0, as every launcher writes it. The multicore physics is parked on branch parked/lanes: a
// count of lanes is refused.
void ReadLanesOff(Part& p)
{
  const picojson::value* v = p.Take("lanes");
  if (v && !(v->is<double>() && v->get<double>() == 0))
    p.Refuse("lanes", "is not 0: the multicore physics is parked on branch parked/lanes");
}

// "debug.partialRollback": the per-machine rollback's test settings (fzgx::Debug says what each does): a mode alone
// ("off", "check", "writes"), or an object with the mode and the log's and the steps' settings, or with a log or
// "guard" alone (the path on, as by default). The contact jobs ("contact") and the "trace" mode are gone: a file
// asking for either is refused.
void ReadPartialKeys(Part& p, Debug* d, std::string* problem)
{
  const picojson::value* v = p.Take("partialRollback");
  if (!v)
    return;
  const auto mode = [&](const std::string& m) {
    if (m == "off" || m == "check" || m == "writes")
      d->partial_mode = m;
    else if (m == "trace")
      p.Refuse("partialRollback", "asks for \"trace\": that mode was removed on 2026-10-25");
    else
      p.Refuse("partialRollback", "is not \"off\", \"check\", \"writes\" or an object with one of them as \"mode\"");
  };
  if (v->is<std::string>())
    return mode(v->get<std::string>());
  if (!v->is<picojson::object>())
    return p.Refuse("partialRollback", "is not \"off\", \"check\", \"writes\" or an object");
  Part f(v->get<picojson::object>(), p.Under("partialRollback"), problem);
  if (f.Take("contact"))
    return f.Refuse("contact", "is no setting: the contact jobs were removed on 2026-10-25");
  std::string m;
  f.OneOf("mode", &m, {"off", "check", "trace", "writes"});
  f.Words("log", &d->partial_log);
  std::vector<u32> steps;
  f.Wholes("traceSteps", &steps, 0, 0xFFFFFFFF, 2, 2);
  if (steps.size() == 2)
  {
    d->partial_from = steps[0];
    d->partial_to = steps[1];
  }
  f.Flag("widen", &d->partial_widen);
  f.Words("watch", &d->partial_watch);
  f.Flag("guard", &d->partial_guard);
  f.Done();
  // without a mode the path stays on, as without the key: an object of a log alone (a live race's counts)
  if (!m.empty())
    mode(m);
  else if (d->partial_log.empty() && d->partial_guard)
    p.Refuse("partialRollback", "needs \"mode\" (\"off\", \"check\" or \"writes\") or \"log\"");
}

// "debug.standStill": the freeze on purpose (fzgx::Debug says what it does).
void ReadStandStill(Part& p, Debug* d, std::string* problem)
{
  if (const picojson::object* stand = p.Object("standStill"))
  {
    Part f(*stand, p.Under("standStill"), problem);
    launch_file::ReadStandStill(f, d);
    if (d->stand_steps.empty() || d->stand_steps.size() != d->stand_tenths.size() ||
        !std::is_sorted(d->stand_steps.begin(), d->stand_steps.end()))
      p.Refuse("standStill", "needs \"steps\" (ascending) and as many \"tenths\" (of a second, each at most 1200)");
    f.Done();
  }
}

namespace
{
// The emulator's side: the test settings the runner's patched emulator core and its runtime take. Each has a key
// here and the name the emulator knows it by (forks/recompcore/2-port, Common/RunSettings.h). A key whose `on` is
// set is true or false, and true gives the emulator that value; a key with `allowed` is a text of those characters
// only (a script or a list, in the emulator's own form; "" as the text also takes the one word `word`); any other
// key is a text as it is (a file). The emulator reads these from here and from nowhere else in a start with a
// launch file: a variable of the same name in the environment does nothing.
struct EmulatorKey
{
  const char* key;
  const char* name;
  const char* on = nullptr;
  const char* allowed = nullptr;
  const char* word = nullptr;
};
constexpr char HEX_LIST[] = "0123456789abcdefABCDEF:,@-";
constexpr EmulatorKey EMULATOR_KEYS[] = {
    // what the game computes, and its input
    {"pad", "FZGX_PAD", nullptr, "0123456789abcdefABCDEF:, ", "neutral"},  // FIELD:BUTTONS[:SX:SY[:CX:CY]],...
    {"poke", "FZGX_POKE8", nullptr, HEX_LIST},                             // ADDR:VALUE[@FROM-TO],...
    {"pokeTask", "FZGX_POKE8_TASK", nullptr, HEX_LIST},
    {"pokeTaskByFrame", "FZGX_POKE8_TASK_BY", "frame"},
    {"timebaseByCharge", "FZGX_TIMEBASE_BY_CHARGE", "1"},
    {"noHookFlush", "FZGX_NO_HOOK_FLUSH", "1"},
    {"noBusyWait", "FZGX_NO_BUSYWAIT", "1"},
    {"noGatherInline", "FZGX_GATHER_INLINE", "0"},
    {"noIcacheFast", "FZGX_ICACHE_FAST", "0"},
    {"keepHostCall", "FZGX_KEEP_HOSTCALL", "1"},
    {"noReinterpretReuse", "FZGX_REINTERPRET_REUSE", "0"},
    {"fallbackRanges", "STATICRECOMP_FALLBACK_RANGES", nullptr, "0123456789abcdefABCDEF,-"},  // START-END,...
    {"lockstep", "STATICRECOMP_LOCKSTEP", "1"},
    {"lockstepStart", "STATICRECOMP_LOCKSTEP_START", nullptr, "0123456789abcdefABCDEFx"},
    {"lockstepLimit", "STATICRECOMP_LOCKSTEP_LIMIT", nullptr, "0123456789abcdefABCDEFx"},
    {"lockstepFilter", "STATICRECOMP_LOCKSTEP_FILTER"},
    {"lockstepNoDedup", "STATICRECOMP_LOCKSTEP_NODEDUP", "1"},
    {"lockstepMaxReport", "STATICRECOMP_LOCKSTEP_MAXREPORT", nullptr, "0123456789abcdefABCDEFx"},
    {"lockstepStepCap", "STATICRECOMP_LOCKSTEP_STEPCAP", nullptr, "0123456789"},
    {"lockstepTrace", "STATICRECOMP_LOCKSTEP_TRACE", nullptr, "0123456789abcdefABCDEFx"},
    {"lockstepRepeat", "STATICRECOMP_LOCKSTEP_REPEAT", nullptr, "0123456789abcdefABCDEFx"},
    {"lockstepWhitelist", "STATICRECOMP_LOCKSTEP_WHITELIST", nullptr, "0123456789abcdefABCDEFx,-: "},
    // files, and time lost writing them
    {"probe", "FZGX_PROBE"},
    {"probeNoHash", "FZGX_PROBE_HASH", "0"},
    {"paceLog", "FZGX_PACE_LOG"},
    {"taskTimers", "FZGX_TASKPROF", "1"},
    {"taskDumps", "FZGX_TASKDUMP", nullptr, "0123456789,"},
    {"taskList", "FZGX_TASKLIST", "1"},
    {"dumpFields", "FZGX_DUMP_FIELDS", nullptr, "0123456789,"},
    {"dumpRam", "FZGX_DUMP_RAM", "1"},
    {"sample", "FZGX_SAMPLE"},
    {"sampleTasks", "FZGX_SAMPLE_TASKS", "1"},
    {"samplePhysics", "FZGX_SAMPLE_PHYSICS", "1"},
    {"census", "FZGX_CENSUS", "1"},
    {"accessTrace", "FZGX_TRACE", "1"},
    {"accessTraceNoInterrupts", "FZGX_TRACE", "noirq"},
    {"noSlowLog", "FZGX_SLOW_LOG", "0"},
    {"slowLogEverySecond", "FZGX_SLOW_LOG", "all"},
    {"voiceLog", "FZGX_AXLOG"},
    {"dispatchSamples", "STATICRECOMP_DISPATCH_SAMPLES", "1"},
    {"dispatchTrace", "STATICRECOMP_TRACE_FILE"},
    // the window: shown without taking the keyboard; its controls that change the game (saved states by the File menu
    // and F1, Pause, fast forward while Space is held), which a window without the key does not have
    {"windowNoActivate", "FZGX_WINDOW_NOACTIVATE", "1"},
    {"windowControls", "FZGX_WINDOW_CONTROLS", "1"},
};

}  // namespace

// The keys of the emulator's side, into d->emulator in the table's order (the generated reader takes the core's own
// switches of it, "singleCore" to "userSettings").
void ReadEmulatorSide(Part& p, Debug* d)
{
  for (const EmulatorKey& k : EMULATOR_KEYS)
  {
    if (k.on)
    {
      bool on = false;
      p.Flag(k.key, &on);
      if (!on)
        continue;
      // two keys of one name (the access trace's two kinds, the slow-second log's two): the later one in the table
      // would silently win
      for (const EmulatorSetting& given : d->emulator)
      {
        if (given.name == k.name)
          p.Refuse(k.key, "and \"" + given.key + "\" are two settings of one thing: give one");
      }
      d->emulator.push_back({k.key, k.name, k.on});
      continue;
    }
    std::string text;
    p.Words(k.key, &text);
    if (text.empty())
      continue;
    if (k.allowed && text.find_first_not_of(k.allowed) != std::string::npos && !(k.word && text == k.word))
      p.Refuse(k.key, std::string("holds a character that is none of \"") + k.allowed + "\"" +
                          (k.word ? std::string(" (and is not \"") + k.word + "\")" : ""));
    d->emulator.push_back({k.key, k.name, text});
  }
}
}  // namespace fzgx::launch
