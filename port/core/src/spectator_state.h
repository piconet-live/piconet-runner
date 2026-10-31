// The advanced spectator controls' state in a session (net_internal.h includes this; spectator_frame.cpp says what
// it does).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/observer.h"
#include "fzgx/spectator.h"

#include <array>
#include <memory>

namespace fzgx::net
{
using u32 = std::uint32_t;
using u16 = std::uint16_t;

// The observer HUD's numbers kept through a race watched (observer_frame.cpp).
struct ObserverState
{
  observer::FrontClock front;   // the race's front by step shown, for the gaps
  bool named = false;           // the seats' colours and names, and their machines' names, were read
  std::array<u32, 30> colour{};
  std::array<std::array<char, 16>, 30> name{};
  std::array<std::array<char, 24>, 30> machine{};
  bool hud_hidden = false;      // the game's own HUD was hidden behind the observer HUD or the clean picture
  u32 kept = 0;                 // ... and the HUD mask the image kept (control block +60) before it was
  // for the log: displayed frames in each view, the game's HUD put back, the first frame's line said
  std::array<u32, spectator::VIEWS> frames{};
  u32 hud_back = 0;
  u32 game_mask[2] = {0, 0};  // frames in the game HUD view with its mask off, on
  bool said = false;
};

// The advanced spectator controls applied to a race watched (spectator_frame.cpp).
struct SpectatorState
{
  std::unique_ptr<spectator::Controls> controls;  // from the race phase's first frame
  bool tv = false;       // the image's spectator cameras are the controls' (control block +248, +56)
  bool leaving = false;  // ... and are handed back to the chase camera in this frame
  bool shown = false;    // the overlay was told something to show
  bool overlay = true;   // ... and not the clean picture (Z, the mode button): with it the course map
  u32 camera_mode = 0xFFFFFFFF;  // the image's mode LiveCameras asked for last
  ObserverState observer;
  u32 followed = 0xFFFFFFFF;  // ... and the racer it followed
  u32 last_step = 0;     // the step the frame before showed: another than the next is a move of the picture
  // what the pad and the overlay asked of the scrubber this frame (spectators and replays only)
  int scrub = 0;
  bool live = false, pause = false;
  u32 jump = 0xFFFFFFFF;
  // for the log; and the pad of the frame before, for the presses a racer who is out may not make (scrub, pause, live)
  u32 follows = 0, kind_changes = 0, clicks = 0, refused = 0, kinds_used = 0, held_back = 0;
  u16 pad_before = 0;
};
}  // namespace fzgx::net
