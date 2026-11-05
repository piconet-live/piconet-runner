// Tests of the launch file's "lanes" key and the per-machine rollback's keys (files_launch.cpp). "lanes" is 0 or
// absent: every launcher writes 0, and the multicore physics is parked on branch parked/lanes (a count is refused);
// "debug.machineLanes" is no setting any more; "debug.partialRollback" holds the per-machine rollback's. And the
// settled A/B switches' keys, refused.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
bool Refused(const std::string& text, const std::string& part)
{
  Options o;
  const std::string problem = ParseLaunchFile(text, &o).value_or("");
  if (problem.find(part) == std::string::npos)
    std::printf("refused with: \"%s\", expected: %s\n", problem.c_str(), part.c_str());
  return problem.find(part) != std::string::npos;
}
}  // namespace

void TestTheLanesSettings()
{
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && DebugSettingsOn(o.debug).empty());
  CHECK(!ParseLaunchFile(R"({"about": "x", "lanes": 0})", &o) && DebugSettingsOn(o.debug).empty());
  for (const char* wrong : {"1", "4", "8", "\"auto\"", "-1", "1.5", "\"0\"", "true"})
    CHECK(Refused(std::string(R"({"about": "x", "lanes": )") + wrong + "}", "\"lanes\" is not 0: the multicore physics is parked"));
  CHECK(Refused(R"({"about": "x", "debug": {"machineLanes": {"lanes": 4}}})", "\"debug.machineLanes\" is not a setting"));
  // the settled A/B switches, removed as the lanes were parked: refused, whatever the value
  for (const std::string gone : {"pacedLoad", "drawSkipOff", "hitShowOff", "spectateLive"})
    CHECK(Refused(R"({"about": "x", "debug": {")" + gone + R"(": false}})", "\"debug." + gone + "\" was removed on 2026-10-25"));
}

// "debug.partialRollback": on unless a test says otherwise, by a mode alone or an object (log, steps, guard). The
// contact jobs ("contact") and the "trace" mode are gone: refused. A file with no match part: the key
// is read the same.
void TestThePartialRollbackKeys()
{
  const auto with = [](const std::string& value) { return R"({"about": "x", "debug": {"partialRollback": )" + value + "}}"; };
  const std::vector<std::string> named{"debug.partialRollback"};
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && o.debug.partial_mode.empty() && o.debug.partial_guard);
  CHECK(!ParseLaunchFile(with(R"("off")"), &o) && o.debug.partial_mode == "off" && DebugSettingsOn(o.debug) == named);
  CHECK(!ParseLaunchFile(with(R"({"mode": "check", "log": "p.csv", "traceSteps": [5, 9]})"), &o) &&
        o.debug.partial_mode == "check" && o.debug.partial_log == "p.csv" && o.debug.partial_to == 9);
  CHECK(Refused(with(R"("run")"), "\"debug.partialRollback\" is not \"off\", \"check\", \"writes\""));
  CHECK(Refused(with(R"("trace")"), "\"debug.partialRollback\" asks for \"trace\": that mode was removed on 2026-10-25"));
  CHECK(Refused(with(R"({"mode": "trace"})"), "\"debug.partialRollback\" asks for \"trace\": that mode was removed on 2026-10-25"));
  CHECK(Refused(with(R"({"mode": "check", "traceCars": true})"), "\"debug.partialRollback.traceCars\" is not a setting"));
  CHECK(!ParseLaunchFile(with(R"({"log": "p.csv"})"), &o) && o.debug.partial_mode.empty() && o.debug.partial_log == "p.csv" &&
        DebugSettingsOn(o.debug) == named);
  CHECK(Refused(with(R"({"widen": true})"), "\"debug.partialRollback\" needs \"mode\""));
  CHECK(!ParseLaunchFile(with(R"({"mode": "writes", "guard": false})"), &o) && !o.debug.partial_guard);
  for (const char* contact : {R"({"contact": true})", R"({"contact": false})", R"({"mode": "check", "contact": true})"})
    CHECK(Refused(with(contact), "\"debug.partialRollback.contact\" is no setting: the contact jobs were removed on 2026-10-25"));
  CHECK(Refused(with(R"({"mode": "check", "depth": 3})"), "\"debug.partialRollback.depth\" is not a setting"));
}

// "debug.rollbackWorker": the deep-rollback worker's test key (fzgx/rollback_worker.h); not named, the mode stays empty
// (every player's start: on from 4 hardware threads, ModeFor): a mode alone or {"mode", "depth"}.
void TestTheRollbackWorkerKeys()
{
  const auto with = [](const std::string& value) { return R"({"about": "x", "debug": {"rollbackWorker": )" + value + "}}"; };
  Options o;
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && o.debug.worker_mode.empty() && o.debug.worker_depth == 7);
  // (tests only since the old path's retirement: "off" is a test setting like every other mode)
  CHECK(!ParseLaunchFile(with(R"("off")"), &o) && o.debug.worker_mode == "off" &&
        DebugSettingsOn(o.debug) == std::vector<std::string>{"debug.rollbackWorker"});
  CHECK(!ParseLaunchFile(R"({"about": "x"})", &o) && DebugSettingsOn(o.debug).empty());
  CHECK(!ParseLaunchFile(with(R"("check")"), &o) && o.debug.worker_mode == "check" &&
        DebugSettingsOn(o.debug) == std::vector<std::string>{"debug.rollbackWorker"});
  CHECK(!ParseLaunchFile(with(R"({"mode": "compare", "depth": 3})"), &o) && o.debug.worker_mode == "compare" &&
        o.debug.worker_depth == 3);
  CHECK(Refused(with(R"("adopt")"), "\"debug.rollbackWorker\" is not \"off\", \"compare\", \"check\""));
  // S2: the thread ("on"), its baseline ("sync"), the rule and the write watch
  CHECK(!ParseLaunchFile(with(R"("on")"), &o) && o.debug.worker_mode == "on" && o.debug.worker_rule == "fit");
  CHECK(!ParseLaunchFile(with(R"({"mode": "sync", "rule": "always", "watch": "805973D4:415-430"})"), &o) &&
        o.debug.worker_mode == "sync" && o.debug.worker_rule == "always" && o.debug.worker_watch == "805973D4:415-430");
  CHECK(Refused(with(R"({"mode": "on", "rule": "never"})"), "\"debug.rollbackWorker.rule\""));
  CHECK(Refused(with(R"({"depth": 3})"), "\"debug.rollbackWorker\" needs \"mode\""));
  CHECK(Refused(with(R"({"mode": "check", "depth": 0})"), "\"debug.rollbackWorker.depth\""));
  // S3: the adoptions compared with their sync rollbacks, the restarts' cap, the job's start
  CHECK(!ParseLaunchFile(with(R"("async-check")"), &o) && o.debug.worker_mode == "async-check");
  CHECK(!ParseLaunchFile(with(R"({"mode": "on", "restarts": 2, "start": "whole"})"), &o) && o.debug.worker_restarts == 2 &&
        o.debug.worker_start == "whole");
  CHECK(Refused(with(R"({"mode": "on", "restarts": 31})"), "\"debug.rollbackWorker.restarts\""));
  // S4: the catch-up bound, the slow worker's switch and its test delay
  CHECK(!ParseLaunchFile(with(R"("on")"), &o) && o.debug.worker_catch_up == 60 && o.debug.worker_slow_us == 6000 &&
        o.debug.worker_delay_us == 0);
  CHECK(!ParseLaunchFile(with(R"({"mode": "on", "catchUp": 30, "slowStepUs": 0, "delayUs": 7000})"), &o) &&
        o.debug.worker_catch_up == 30 && o.debug.worker_slow_us == 0 && o.debug.worker_delay_us == 7000);
  CHECK(!ParseLaunchFile(with(R"({"mode": "on", "delayUs": 9000, "delayFrom": 1500, "delayTo": 2700, "spikeAt": 900,
                                  "spikeUs": 150000})"), &o) &&
        o.debug.worker_delay_from == 1500 && o.debug.worker_delay_to == 2700 && o.debug.worker_spike_at == 900 &&
        o.debug.worker_spike_us == 150000 && o.debug.worker_spike_slot == 2);
  CHECK(!ParseLaunchFile(with(R"({"mode": "on", "spikeAt": 900, "spikeUs": 150000, "spikeSlot": 1})"), &o) &&
        o.debug.worker_spike_slot == 1);
  CHECK(Refused(with(R"({"mode": "on", "spikeSlot": 2})"), "\"debug.rollbackWorker.spikeSlot\""));
  CHECK(Refused(with(R"({"mode": "on", "catchUp": 300})"), "\"debug.rollbackWorker.catchUp\""));
  CHECK(Refused(with(R"({"mode": "on", "start": "some"})"), "\"debug.rollbackWorker.start\""));
  // the second worker (study section 20.2): "workers" 1 or 2 forces it off or on; unset, 0 (the PC's threads decide)
  CHECK(!ParseLaunchFile(with(R"("on")"), &o) && o.debug.worker_workers == 0);
  CHECK(!ParseLaunchFile(with(R"({"mode": "on", "workers": 2})"), &o) && o.debug.worker_workers == 2);
  CHECK(Refused(with(R"({"mode": "on", "workers": 3})"), "\"debug.rollbackWorker.workers\""));
  CHECK(Refused(with(R"({"mode": "on", "workers": 0})"), "\"debug.rollbackWorker.workers\""));
}
