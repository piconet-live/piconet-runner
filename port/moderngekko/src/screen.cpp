// What the swap chain is, said in the log, and the window's full screen as the video backend's own.
// A display with variable refresh (G-Sync, FreeSync) follows a program's pictures when they reach the screen without
// the system's compositor in between. In the application's own, exclusive full screen that is so with the drivers'
// default settings; in a window, and in a borderless window the size of the screen, only where the driver presents
// the swap chain on a plane of its own and its windowed mode is switched on.
// The launch file's "fullScreen" says which of the two a player has (the launcher's setting "Full screen"):
// "borderless", and a file without the key: the window without a frame, the size of its monitor,
// and nothing here asks the backend for anything. "exclusive": the game's full screen (F11, Alt+Enter, the strip's
// button: frame_screen.cpp makes the window borderless and the size of its monitor, as ever) asks the Vulkan
// backend for its exclusive full screen on top of that, while the game's window has the focus (a display of a fixed
// rate tears there with vertical sync off, which a borderless window never does: hence the setting):
//   - asked before a present, on the thread that presents, where the backend takes it (VKGfx::SetFullscreen is a
//     request the next BindBackbuffer acts on);
//   - a driver that refuses (VK_EXT_full_screen_exclusive absent, the surface not taken, the acquire failing) leaves
//     the borderless window as it is: nothing else changes, the log says so, and it is not asked again until the
//     window leaves full screen or gets the focus back;
//   - given back when another window gets the focus (the borderless window stays, behind it) and asked for again
//     when the focus returns; given back when the window leaves full screen;
//   - a player's GFX.ini that says BorderlessFullscreen = True keeps the borderless window alone (the emulator's own
//     setting, g_ActiveConfig.ExclusiveFullscreenEnabled).
// The Direct3D backends are not asked: their swap chain is the flip-model kind the system can put on a plane of its
// own in a window as in a borderless one, and it presents tearing-capable.
// What is asked when, a refusal and the giving back are plain state in the core (fzgx/screen.h), tested there.
// Nothing here touches the game: the picture's surface changes its mode, as after any resize.
// The log's line ("fzgx: screen: ...") is written after the first present and whenever what it says changed: the
// backend, what the backend says of its swap chain (the present mode; forks/recompcore, AbstractGfx::
// DescribeSwapChain), vertical sync, and whether the window is a window, a borderless full screen (and why not
// more) or the backend's own full screen.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <atomic>
#include <string>

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/Host.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

#include "fzgx/screen.h"

namespace fzgx_port
{
namespace
{
// The window is in full screen (frame_screen.cpp, the window's thread; read by the thread that presents).
std::atomic<bool> s_window_full{false};

// The thread that presents only.
struct Screen
{
  fzgx::screen::Ask ask;  // what was asked of the backend, and whether it refused
  std::string said;      // the line as last written
  int lines = 0;
  // what the line was last made from, and the presents since: it is made again when one of these changed, and
  // once a second besides (the swap chain's own words can change with a new swap chain)
  bool full = false, held = false, was_refused = false, sync = false;
  const char* why = nullptr;
  int presents = 0;
};
Screen s_screen;
constexpr int MOST_LINES = 60;  // of one start: a window that flickers between two states does not fill the log

bool OursToAsk()
{
  return g_backend_info.api_type == APIType::Vulkan;
}

// Why a window in full screen is a borderless one and no more, in the log's words; none: nothing stands in the way.
const char* InTheWay()
{
  const fzgx::Debug& debug = s_state.options.debug;
  // (the player's setting first; a test's "debug.fullScreenWithoutFocus" asks whatever it says)
  if (s_state.options.full_screen != fzgx::screen::EXCLUSIVE && !debug.full_screen_without_focus)
    return "the setting is Borderless";
  if (!OursToAsk())
    return "this backend is not asked for a full screen of its own";
  if (!g_backend_info.bSupportsExclusiveFullscreen)
    return "the driver has no exclusive full screen for this window";
  if (!g_ActiveConfig.ExclusiveFullscreenEnabled())
    return "the settings say BorderlessFullscreen";
  // (a test's window is shown without the focus: the launch file's "debug.fullScreenWithoutFocus" asks all the same)
  if (!Host_RendererHasFocus() && !debug.full_screen_without_focus)
    return "another window has the focus";
  return nullptr;
}
}  // namespace

void TellFullscreen(bool on)
{
  s_window_full.store(on, std::memory_order_relaxed);
}

void FollowFullscreen()
{
  if (!g_gfx || !OursToAsk())
    return;
  const bool wanted = s_window_full.load(std::memory_order_relaxed) && InTheWay() == nullptr;
  switch (fzgx::screen::Before(s_screen.ask, wanted, g_gfx->IsFullscreen()))
  {
  case fzgx::screen::Tell::Ask: g_gfx->SetFullscreen(true); break;
  case fzgx::screen::Tell::GiveBack: g_gfx->SetFullscreen(false); break;
  case fzgx::screen::Tell::Nothing: break;
  }
}

void SayTheScreen()
{
  if (!g_gfx)
    return;
  const bool full = s_window_full.load(std::memory_order_relaxed);
  const bool held = OursToAsk() && g_gfx->IsFullscreen();
  // asked before this present, which acted on it: held now, or the driver refused (or took it away again)
  if (fzgx::screen::After(s_screen.ask, held))
    g_gfx->SetFullscreen(false);
  // (the window went to full screen while this present was under way: the next one asks, and says what came of it)
  const char* const why = full && !held && !s_screen.ask.refused ? InTheWay() : nullptr;
  if (full && !held && !s_screen.ask.refused && OursToAsk() && why == nullptr)
    return;
  const bool sync = g_ActiveConfig.bVSyncActive;
  if (!s_screen.said.empty() && full == s_screen.full && held == s_screen.held && s_screen.ask.refused == s_screen.was_refused &&
      sync == s_screen.sync && why == s_screen.why && ++s_screen.presents < 60)
    return;
  s_screen.full = full, s_screen.held = held, s_screen.was_refused = s_screen.ask.refused, s_screen.sync = sync;
  s_screen.why = why;
  s_screen.presents = 0;
  std::string line = "fzgx: screen: backend " + Config::Get(Config::MAIN_GFX_BACKEND);
  if (const std::string swap_chain = g_gfx->DescribeSwapChain(); !swap_chain.empty())
    line += ", " + swap_chain;
  line += sync ? ", vertical sync on" : ", vertical sync off";
  if (!full)
    line += "; the game is in a window";
  else if (held)
    line += "; the game is in full screen, the backend's own (exclusive)";
  else if (s_screen.ask.refused)
    line += "; the game is in full screen as a borderless window: the driver refused the backend's own full screen";
  else if (why != nullptr)
    line += std::string("; the game is in full screen as a borderless window: ") + why;
  else
    line += "; the game is in full screen as a borderless window";
  if (line == s_screen.said || s_screen.lines >= MOST_LINES)
    return;
  s_screen.said = line;
  ++s_screen.lines;
  s_core_host.Log(fzgx::LogLevel::Notice, line);
}

void ForgetTheScreen()
{
  s_screen = {};
  s_window_full.store(false, std::memory_order_relaxed);
}
}  // namespace fzgx_port
