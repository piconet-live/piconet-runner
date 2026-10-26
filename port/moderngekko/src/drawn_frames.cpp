// The pictures between two steps (the launch file's "refreshRate"; fzgx/hfr.h): the emulator's side. The core makes
// a request a pass; this file hands it to the GPU thread's re-draw (VideoCommon/FrameRedraw.h), keyed by the
// command stream's position, with the core's guess forward for each picture, and gives the core the display's
// refresh rate. The re-draw's pictures are held by pictures.cpp for their time like the game's own.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include "VideoCommon/FrameRedraw.h"

#include "fzgx/hfr.h"
#include "fzgx/present.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fzgx_port
{
namespace
{
double DisplayHz()
{
#ifdef _WIN32
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
    return static_cast<double>(mode.dmDisplayFrequency);
#endif
  return 0;
}

VideoCommon::FrameRedraw::InBetween Convert(const fzgx::hfr::InBetween& b)
{
  VideoCommon::FrameRedraw::InBetween o;
  o.fraction = b.fraction;
  o.camera = b.camera;
  for (fzgx::u32 i = 0; i < VideoCommon::FrameRedraw::MOST_MACHINES && i < fzgx::hfr::SEATS; ++i)
  {
    o.machine[i] = b.machine[i];
    o.motion[i] = b.motion[i];
  }
  return o;
}
}  // namespace

void StartDrawnFrames()
{
  fzgx::hfr::Video video;
  video.request = [](fzgx::hfr::Request q) {
    VideoCommon::FrameRedraw::Request r;
    // the stream's position now: this pass's frame begin (its draw-done) is before it, its drawing after it
    r.position = VideoCommon::FrameRedraw::WrittenPosition();
    r.machines = q.machines;
    for (fzgx::u32 i = 0; i < VideoCommon::FrameRedraw::MOST_MACHINES && i < fzgx::hfr::SEATS; ++i)
      r.at[i] = q.at[i];
    r.fractions = std::move(q.fractions);
    // (on the GPU thread, just before each picture is drawn)
    r.predict = [predict = std::move(q.predict)](float fraction, VideoCommon::FrameRedraw::InBetween* out) {
      *out = Convert(predict(fraction));
    };
    VideoCommon::FrameRedraw::Queue(std::move(r));
  };
  video.display_hz = [] { return DisplayHz(); };
  fzgx::hfr::Start(s_state.options, std::move(video));
  // the GPU thread records each frame; with the high refresh rate it draws it again for the requests, and the
  // re-draw spike's test settings (debug.redraw) draw it again without them
  const fzgx::Debug& d = s_state.options.debug;
  VideoCommon::FrameRedraw::Settings settings{d.redraw_replays, d.redraw_compare, d.redraw_log, d.redraw_dump,
                                              d.redraw_turn, d.redraw_budget_ms, d.redraw_at_draw_done};
  if (fzgx::hfr::On())
  {
    settings.hfr = true;
    settings.replays = fzgx::hfr::MOST_FRAMES;
    settings.field_us = static_cast<float>(fzgx::present::FIELD_US);
    settings.turn_degrees = 0;
    settings.draws_at = d.redraw_draws_at;
    settings.stills_at = d.redraw_stills_at;
    settings.stills = d.redraw_stills;
    settings.stills_every = d.redraw_stills_every;
  }
  VideoCommon::FrameRedraw::Configure(settings);
  if (fzgx::hfr::On())
  {
    char text[200];
    std::snprintf(text, sizeof(text), "fzgx: high refresh rate: \"%s\", %u pictures between steps by the test "
                  "setting, display at %.0f Hz, by re-drawing", s_state.options.refresh_rate.c_str(),
                  s_state.options.debug.drawn_frames, DisplayHz());
    s_core_host.Log(fzgx::LogLevel::Notice, text);
  }
}

void StopDrawnFrames()
{
  if (const std::string line = fzgx::hfr::Report(); !line.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, line);
  if (const std::string line = VideoCommon::FrameRedraw::Report(); !line.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: " + line);
  VideoCommon::FrameRedraw::Configure({});
}
}  // namespace fzgx_port
