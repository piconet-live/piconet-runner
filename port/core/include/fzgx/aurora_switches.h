// The Aurora switches a start's launch file brings: the player's picture settings ("auroraEdges", "auroraTextures",
// "auroraColourblind", "auroraHighRefresh", "auroraRayTracing") as the environment variables Aurora reads for itself
// once it starts. The adapter's aurora.cpp sets them before Aurora starts; here they are plain strings with no emulator
// in them, so that the mapping is tested without one (tests/test_aurora_switches.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/core.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fzgx::aurora
{
struct Switch
{
  const char* name;
  const char* value;
};

// High refresh's tested set (owner-slots240-unleashed.cmd, gxr/240-slots), beside AURORA_RUNAHEAD: the pictures
// between drawn again from the true frame's draws as render bundles, paced to the refreshes, asked on time while the
// next true frame decodes, its offscreen passes not drawn again, each picture its own refresh (Fifo)
std::span<const Switch> HighRefreshSet();

// Ray tracing's tested set ("auroraRayTracing"): the owner's set of 0.6.0's build c (v060c-rtx.cmd, 2026-11-03), the
// matte look with AURORA_RT_GLOW_HALF=2 and the look fixes AURORA_RT_GLOW_WIDE, _BEFORE_LATEST and _SHADER_CACHE (build
// c's fourth, a light's own body, is the game's draw marks now, always on), build d's translucent sheets
// (AURORA_RT_SHEETS), and effect cards in the game's own range (AURORA_RT_CARDS).
// A GPU without DirectX 12 ray tracing (DXR 1.1) shows the game's own picture: Aurora says "[rt] live pass off" in its log.
std::span<const Switch> RayTracingSet();

// A switch's value in the start's environment, or nullptr when it is not set (std::getenv in the runner)
using Environment = std::function<const char*(const char*)>;

struct Setting
{
  std::string name;
  std::string value;
};

// The switches to set for this start, in order, each at most once. The player's own choices (edges, textures,
// colourblind filter, the refresh rate asked for) are set as asked. A set a setting brings with it (high refresh's,
// ray tracing's) leaves a switch the environment already has alone, so a test's own value stays (0 = off). With the
// rays on, from either side (AURORA_RT=1), the stream thread hands the draws' state keys to the translate threads
// (AURORA_RT_CAPTURE_ASYNC, gxr/240-capture), unless the environment says otherwise.
std::vector<Setting> Switches(const Options& o, const Environment& environment);

// The runner's log line of them: "fzgx: aurora: ray tracing on|off, switches set: NAME=VALUE ..." (or "none"); a switch
// the start had set itself is not in it
std::string Line(const Options& o, const std::vector<Setting>& set);
}  // namespace fzgx::aurora
