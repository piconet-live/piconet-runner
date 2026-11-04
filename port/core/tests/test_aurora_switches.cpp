// Tests of the Aurora switches a start's launch file brings (fzgx/aurora_switches.h; the adapter's aurora.cpp sets
// them): each player's key into its switches, a set brought with a key leaving the start's own switches alone, the
// rays' capture, and the log line.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "fzgx/aurora_switches.h"

#include <map>

namespace
{
using Env = std::map<std::string, std::string>;

std::vector<aurora::Setting> SwitchesOf(const std::string& launch, const Env& env = {})
{
  Options o;
  const auto problem = ParseLaunchFile(launch, &o);
  if (problem)
    std::printf("refused: %s\n", problem->c_str());
  return aurora::Switches(o, [&env](const char* name) -> const char* {
    const auto at = env.find(name);
    return at == env.end() ? nullptr : at->second.c_str();
  });
}

// the value set for `name`, or "" when it is not set
std::string Of(const std::vector<aurora::Setting>& set, const std::string& name)
{
  for (const aurora::Setting& s : set)
    if (s.name == name)
      return s.value;
  return "";
}

bool EachOnce(const std::vector<aurora::Setting>& set)
{
  std::map<std::string, int> seen;
  for (const aurora::Setting& s : set)
    if (++seen[s.name] > 1)
      return false;
  return true;
}

constexpr const char* AURORA = R"({"about": "x", "renderer": "aurora")";
}  // namespace

void TestTheAuroraSwitches()
{
  // nothing asked: nothing set
  CHECK(SwitchesOf(std::string(AURORA) + "}").empty());
  CHECK(aurora::Line(Options{}, {}) == "fzgx: aurora: ray tracing off, switches set: none");

  // the player's own choices, as asked
  const auto picture = SwitchesOf(std::string(AURORA) +
                                  R"(, "auroraEdges": 4, "auroraTextures": 16, "auroraColourblind": "protanopia"})");
  CHECK(Of(picture, "AURORA_MSAA") == "4" && Of(picture, "AURORA_ANISO") == "16" && Of(picture, "AURORA_ANISO_GX") == "0" &&
        Of(picture, "AURORA_ANISO_ALL") == "1" && Of(picture, "AURORA_COLOURBLIND") == "protanopia" && picture.size() == 5);
  CHECK(SwitchesOf(std::string(AURORA) + R"(, "auroraColourblind": "off"})").empty());

  // high refresh: the rate, and its tested set unless the start set a switch itself
  const auto high = SwitchesOf(std::string(AURORA) + R"(, "auroraHighRefresh": 240})");
  CHECK(Of(high, "AURORA_RUNAHEAD") == "240" && high.size() == 1 + aurora::HighRefreshSet().size());
  for (const aurora::Switch& s : aurora::HighRefreshSet())
    CHECK(Of(high, s.name) == s.value);
  const auto highOwn = SwitchesOf(std::string(AURORA) + R"(, "auroraHighRefresh": 120})", {{"AURORA_PACE", "0"}, {"AURORA_RUNAHEAD", "60"}});
  CHECK(Of(highOwn, "AURORA_PACE").empty() && Of(highOwn, "AURORA_RUNAHEAD") == "120");
  CHECK(Of(high, "AURORA_RT").empty() && Of(high, "AURORA_RT_CAPTURE_ASYNC").empty());

  // ray tracing: the whole tested set, the rays' capture with it; off by default and as false
  Options rt;
  CHECK(!ParseLaunchFile(std::string(AURORA) + R"(, "auroraRayTracing": true})", &rt) && rt.aurora_ray_tracing);
  CHECK(!ParseLaunchFile(std::string(AURORA) + R"(, "auroraRayTracing": false})", &rt) && !rt.aurora_ray_tracing);
  CHECK(!ParseLaunchFile(std::string(AURORA) + "}", &rt) && !rt.aurora_ray_tracing);
  CHECK(ParseLaunchFile(std::string(AURORA) + R"(, "auroraRayTracing": 1})", &rt).has_value());
  const auto rays = SwitchesOf(std::string(AURORA) + R"(, "auroraRayTracing": true})");
  CHECK(rays.size() == aurora::RayTracingSet().size() + 1 && EachOnce(rays));
  for (const aurora::Switch& s : aurora::RayTracingSet())
    CHECK(Of(rays, s.name) == s.value);
  CHECK(Of(rays, "AURORA_RT") == "1" && Of(rays, "AURORA_RT_CAPTURE_ASYNC") == "1");
  // the owner's choices of build c, held here so a change of the set is a change of this test too
  CHECK(Of(rays, "AURORA_RT_GLOW_HALF") == "2" && Of(rays, "AURORA_RT_LIGHT_OWN") == "1" && Of(rays, "AURORA_RT_GLOW_WIDE") == "1" &&
        Of(rays, "AURORA_RT_BEFORE_LATEST") == "1" && Of(rays, "AURORA_RT_SHADER_CACHE") == "1" &&
        Of(rays, "AURORA_RT_SUN_GAIN") == "0.6" && Of(rays, "AURORA_RT_LOOK") == "2" && Of(rays, "AURORA_RT_GLOW_GAIN") == "1.4");
  CHECK(Of(rays, "SDL_JOYSTICK_HIDAPI_GAMECUBE").empty());
  CHECK(aurora::Line(rt, {}) == "fzgx: aurora: ray tracing off, switches set: none");
  CHECK(!ParseLaunchFile(std::string(AURORA) + R"(, "auroraRayTracing": true})", &rt));
  CHECK(aurora::Line(rt, {{"AURORA_RT", "1"}, {"AURORA_RT_HALF", "1"}}) ==
        "fzgx: aurora: ray tracing on, switches set: AURORA_RT=1 AURORA_RT_HALF=1");

  // a test's own switches stay: rays off by hand (no capture then), one value of its own, one switch the set has not
  const auto off = SwitchesOf(std::string(AURORA) + R"(, "auroraRayTracing": true})",
                              {{"AURORA_RT", "0"}, {"AURORA_RT_SUN_GAIN", "0.9"}, {"AURORA_RT_SHEETS", "1"}});
  CHECK(Of(off, "AURORA_RT").empty() && Of(off, "AURORA_RT_SUN_GAIN").empty() && Of(off, "AURORA_RT_SHEETS").empty());
  CHECK(Of(off, "AURORA_RT_CAPTURE_ASYNC").empty() && off.size() == aurora::RayTracingSet().size() - 2);
  const auto captureOwn = SwitchesOf(std::string(AURORA) + R"(, "auroraRayTracing": true})", {{"AURORA_RT_CAPTURE_ASYNC", "0"}});
  CHECK(Of(captureOwn, "AURORA_RT") == "1" && Of(captureOwn, "AURORA_RT_CAPTURE_ASYNC").empty());
  // rays on by hand without the key, as the .cmd files of 0.6.0's test builds: the capture alone, as before the key
  const auto byHand = SwitchesOf(std::string(AURORA) + "}", {{"AURORA_RT", "1"}});
  CHECK(byHand.size() == 1 && Of(byHand, "AURORA_RT_CAPTURE_ASYNC") == "1");

  // with Sharper textures the player's own AURORA_ANISO_GX=0 stands, once; all three keys at once, each switch once
  const auto all = SwitchesOf(std::string(AURORA) +
                              R"(, "auroraTextures": 8, "auroraHighRefresh": 240, "auroraRayTracing": true})");
  CHECK(EachOnce(all) && Of(all, "AURORA_ANISO_GX") == "0" && Of(all, "AURORA_ANISO") == "8" &&
        Of(all, "AURORA_REDRAW") == "1" && Of(all, "AURORA_RT") == "1" && Of(all, "AURORA_RT_CAPTURE_ASYNC") == "1");
}
