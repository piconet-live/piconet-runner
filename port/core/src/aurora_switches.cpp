// The Aurora switches a start's launch file brings (fzgx/aurora_switches.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/aurora_switches.h"

#include <algorithm>

namespace fzgx::aurora
{
namespace
{
constexpr Switch HIGH_REFRESH_SET[] = {
    {"AURORA_REDRAW", "1"},          {"AURORA_REDRAW_BUNDLES", "1"},  {"AURORA_PACE", "1"},
    {"AURORA_PRESENT_OVERLAP", "1"}, {"AURORA_PRESENT_SLOTS", "1"},   {"AURORA_REDRAW_SKIP_OFFSCREEN", "1"},
    {"AURORA_PRESENT_FIFO", "1"},
};

// v060c-rtx.cmd's order, less its SDL_JOYSTICK_HIDAPI_GAMECUBE (the controller adapter's, no picture's)
constexpr Switch RAY_TRACING_SET[] = {
    {"AURORA_RT", "1"},
    {"AURORA_RT_RULES_LATE", "1"},
    {"AURORA_ANISO_GX", "0"},
    {"AURORA_RT_SCENE_STEADY", "1"},
    {"AURORA_RT_LIGHT", "1"},
    {"AURORA_RT_LOOK", "2"},
    {"AURORA_LOOK_RAW", "1"},
    {"AURORA_LOOK_HDR", "1"},
    {"AURORA_RT_GAME_LIGHT", "0"},
    {"AURORA_RT_LIGHTING", "1"},
    {"AURORA_RT_DENOISE", "1"},
    {"AURORA_RT_SCENE", "1"},
    {"AURORA_RT_SCENE_STATIC", "1"},
    {"AURORA_RT_SCENE_MERGE", "1"},
    {"AURORA_RT_HALF", "1"},
    {"AURORA_RT_BEFORE_BLENDS", "1"},
    {"AURORA_RT_SHINE_STABLE", "1"},
    {"AURORA_RT_AMBIENT_WARMTH", "1"},
    {"AURORA_RT_SUN_GAIN", "0.6"},
    {"AURORA_RT_SKY", "0.5"},
    {"AURORA_RT_BOUNCE", "0.5"},
    {"AURORA_RT_VIS_FLOOR", "0.4"},
    {"AURORA_RT_MOTION_THREAD", "1"},
    {"AURORA_PIPELINE_QUIET", "1"},
    {"AURORA_THREADS_UP", "1"},
    {"AURORA_RT_GATE_MIN", "2"},
    {"AURORA_RT_NO_ENGINE_STAR", "1"},
    {"AURORA_RT_PAIR_GAP", "1"},
    {"AURORA_RT_LAYER_PASS", "1"},
    {"AURORA_RT_BACKDROP_SKIP", "1"},
    {"AURORA_RT_BACKDROP_SHEETS", "1"},
    {"AURORA_RT_HUD_SKIP", "1"},
    {"AURORA_RT_SOFT_CARDS", "1"},
    {"AURORA_RT_ENGINE_STAR_FROM", "24"},
    {"AURORA_RT_HOT_CORE", "1"},
    {"AURORA_RT_FLARE", "1"},
    {"AURORA_RT_GLOW_GAIN", "1.4"},
    {"AURORA_RT_GLOW_HALF", "2"},
    // the look fixes of build c (recompcore gxr/240-nocircle)
    {"AURORA_RT_LIGHT_OWN", "1"},
    {"AURORA_RT_GLOW_WIDE", "1"},
    {"AURORA_RT_SHADER_CACHE", "1"},
    {"AURORA_RT_BEFORE_LATEST", "1"},
    // build d (recompcore gxr/240-sheets, its look checks passed): the camera's rays pass a translucent sheet with a
    // soft alpha (cloud layers, waterfalls)
    {"AURORA_RT_SHEETS", "1"},
    // recompcore gxr/rt-cards (its look checks passed): an effect card hands the blender its colour and alpha in the game's own 0..1, so
    // the raw picture and the glow no longer turn its body into a flat white square (lava haze, fire, sparks)
    {"AURORA_RT_CARDS", "1"},
};

class List
{
public:
  List(std::vector<Setting>& out, const Environment& environment) : out_(out), environment_(environment) {}

  // as asked: over the environment's value
  void Set(const char* name, std::string value)
  {
    const auto at = Find(name);
    if (at != out_.end())
      at->value = std::move(value);
    else
      out_.push_back({name, std::move(value)});
  }

  // brought with a setting: the environment's value, or one this start sets already, stays
  void Bring(const char* name, const char* value)
  {
    if ((environment_ ? environment_(name) : nullptr) == nullptr && Find(name) == out_.end())
      out_.push_back({name, value});
  }

  // what the switch is for this start: this list's value, else the environment's; nullptr when neither sets it
  const char* Value(const char* name) const
  {
    const auto at = std::find_if(out_.begin(), out_.end(), [&](const Setting& s) { return s.name == name; });
    if (at != out_.end())
      return at->value.c_str();
    return environment_ ? environment_(name) : nullptr;
  }

private:
  std::vector<Setting>::iterator Find(const char* name)
  {
    return std::find_if(out_.begin(), out_.end(), [&](const Setting& s) { return s.name == name; });
  }

  std::vector<Setting>& out_;
  const Environment& environment_;
};
}  // namespace

std::span<const Switch> HighRefreshSet()
{
  return HIGH_REFRESH_SET;
}

std::span<const Switch> RayTracingSet()
{
  return RAY_TRACING_SET;
}

std::vector<Setting> Switches(const Options& o, const Environment& environment)
{
  std::vector<Setting> out;
  List list(out, environment);
  if (o.aurora_edges >= 4)  // (1 or 4 are the only counts every graphics card has)
    list.Set("AURORA_MSAA", "4");
  if (o.aurora_textures > 1)
  {
    list.Set("AURORA_ANISO", std::to_string(o.aurora_textures));
    list.Set("AURORA_ANISO_GX", "0");
    list.Set("AURORA_ANISO_ALL", "1");
  }
  if (!o.aurora_colourblind.empty() && o.aurora_colourblind != "off")
    list.Set("AURORA_COLOURBLIND", o.aurora_colourblind);
  // (the step ahead's skip, AURORA_RUNAHEAD_SKIP, is on by default with it: runahead.cpp; a test's 0 stays)
  if (o.aurora_high_refresh != 0)
  {
    list.Set("AURORA_RUNAHEAD", std::to_string(o.aurora_high_refresh));
    for (const Switch& s : HIGH_REFRESH_SET)
      list.Bring(s.name, s.value);
  }
  if (o.aurora_ray_tracing)
    for (const Switch& s : RAY_TRACING_SET)
      list.Bring(s.name, s.value);
  if (const char* rays = list.Value("AURORA_RT"); rays != nullptr && rays[0] == '1')
    list.Bring("AURORA_RT_CAPTURE_ASYNC", "1");
  return out;
}

std::string Line(const Options& o, const std::vector<Setting>& set)
{
  std::string line = std::string("fzgx: aurora: ray tracing ") + (o.aurora_ray_tracing ? "on" : "off") + ", switches set:";
  if (set.empty())
    line += " none";
  for (const Setting& s : set)
    line += " " + s.name + "=" + s.value;
  return line;
}
}  // namespace fzgx::aurora
