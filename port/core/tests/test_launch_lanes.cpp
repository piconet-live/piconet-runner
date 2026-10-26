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
