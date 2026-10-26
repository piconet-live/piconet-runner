// The replay's sound gate (fzgx/sound_gate.h; the scrubber's ScrubBar::heard): the game's sound is heard only while
// the replay plays forward at its own pace. It starts shut. The gate is the sound backend's stream volume, faded over 100 ms each
// way, never above the player's own volume (0 under --mute or the window's mute). Nothing of the game is touched.
// Run on the bar window's thread, by its timer, and by a faster timer of its own while it fades: the backend's
// SetVolume waits for the backend's own thread, which the game's thread must never do each frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "replay_bar.h"

#include <atomic>
#include <chrono>
#include <string>

#ifdef _WIN32
#include "AudioCommon/SoundStream.h"
#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/System.h"
#include "fzgx/sound_gate.h"
#endif

namespace fzgx_port
{
namespace
{
std::atomic<bool> s_heard{false};
std::atomic<double> s_level{0.0};  // the gate's level as its last tick left it (the bar window's thread writes it)
}  // namespace

void ReplaySoundHeard(bool heard)
{
  s_heard.store(heard, std::memory_order_relaxed);
}


#ifdef _WIN32
namespace
{
constexpr UINT_PTR FADE_TIMER = 0x4253;
fzgx::SoundGate s_gate;
bool s_fading = false;
std::atomic<int> s_sent{-1};  // (set by the bar window's thread, and by the game's at the load's end)
unsigned s_ticks = 0;
unsigned s_said = 0;
constexpr unsigned AGAIN_EVERY = 15;  // bar ticks (half a second): the volume is set again though nothing changed

double NowMs()
{
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The stream's volume for the gate's level, set when it changed (or when it must be set again).
void Send(double level, bool again)
{
  SoundStream* const stream = Core::System::GetInstance().GetSoundStream();
  if (stream == nullptr)
    return;
  const int own = Config::Get(Config::MAIN_AUDIO_MUTED) ? 0 : Config::Get(Config::MAIN_AUDIO_VOLUME);
  const int volume = fzgx::SoundGate::Volume(own, level);
  if (volume == s_sent && !again)
    return;
  stream->SetVolume(volume);
  s_sent = volume;
}
}  // namespace

bool ReplaySoundTick(HWND window, WPARAM timer)
{
  const bool mine = timer == FADE_TIMER;
  const bool heard = s_heard.load(std::memory_order_relaxed);
  const double level = s_gate.Level(heard, NowMs());
  s_level.store(level, std::memory_order_relaxed);
  Send(level, !mine && ++s_ticks % AGAIN_EVERY == 0);
  const bool fading = s_gate.Fading(heard);
  if (fading != s_fading)
  {
    // (the log: the first changes of the gate, each when its fade has ended, with the stream's volume)
    if (!fading && ++s_said <= 40)
      s_core_host.Log(fzgx::LogLevel::Notice, std::string("fzgx: replay sound: ") + (heard ? "heard" : "silent") +
                                                  ", the stream's volume " + std::to_string(s_sent));
    s_fading = fading;
    if (fading)
      SetTimer(window, FADE_TIMER, 10, nullptr);
    else
      KillTimer(window, FADE_TIMER);
  }
  return mine;
}
#endif

// Another part of the runner set the stream's volume (the load's pace, at the course's load end): the gate's level
// goes over it at once, in the same call, so not even a few milliseconds are heard at the player's volume.
void ReplaySoundVolumeReset()
{
#ifdef _WIN32
  if (s_state.options.replay_scrubber || s_state.options.practice)
    Send(s_level.load(std::memory_order_relaxed), true);
#endif
}
}  // namespace fzgx_port
