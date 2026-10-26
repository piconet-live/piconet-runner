// The game window's full screen as the video backend's own, exclusive one: what the thread that presents asks of
// the backend and when (the runner's adapter, port/moderngekko/src/screen.cpp, has the emulator's side). Plain
// state with no emulator in it, so that the asking, a refusal and the giving back are tested without one.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace fzgx::screen
{
// The kinds of full screen a launch file names ("fullScreen"): the window without a frame, the size of its monitor,
// as up to 0.2.0; or that and, on top of it, the backend's own full screen.
constexpr char BORDERLESS[] = "borderless", EXCLUSIVE[] = "exclusive";

struct Ask
{
  bool asked = false;    // the backend was asked for its own full screen, and holds it or has not answered yet
  bool refused = false;  // it was asked and did not get it, or lost it: not asked again until it is no longer
                         // wanted (the window left full screen, or another window got the focus) and wanted again
};

enum class Tell
{
  Nothing,
  Ask,       // ask the backend for its own full screen: its next present acts on it
  GiveBack,  // tell it to give the screen back
};

// Before a present. `wanted`: the window is in full screen, the exclusive kind is the player's, and nothing stands
// in the way (the backend can, the window has the focus). `held`: the backend holds its own full screen now.
constexpr Tell Before(Ask& ask, bool wanted, bool held)
{
  if (!wanted)
  {
    const bool give = ask.asked || held;
    ask = {};
    return give ? Tell::GiveBack : Tell::Nothing;
  }
  if (ask.asked || ask.refused)
    return Tell::Nothing;
  ask.asked = true;
  return Tell::Ask;
}

// After a present, which acted on what was asked. True: the backend was asked and does not hold the screen: the
// driver refused, or took it away again. The borderless window stays, and the backend is told to stop asking.
constexpr bool After(Ask& ask, bool held)
{
  if (!ask.asked || held)
    return false;
  ask.asked = false;
  ask.refused = true;
  return true;
}
}  // namespace fzgx::screen
