// The replay's sound gate (the replay scrubber, ScrubBar::heard): a playback's sound is heard only while the
// replay plays forward at its own pace. Paused, held on the bar, waiting for the buffer, running steps again, and
// before the race (the unpaced run up to GO), the host turns its whole sound output down; nothing of the game is
// touched, so the game makes its sound, music included, as ever. The gate fades over FADE_MS each way, so that
// neither edge clicks, and its volume is never above the player's own (0 when the player's is 0, as under --mute).
// Plain arithmetic, so that the host and the core's tests share it: the host asks for the level at each tick of
// its own, on its window's thread, and sets its stream's volume when it changed.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace fzgx
{
class SoundGate
{
public:
  static constexpr double FADE_MS = 100.0;

  // The level, 0 (silent) to 1 (the player's volume), at `now_ms` on any steady clock in milliseconds, the gate
  // being open (`heard`) or not from now on. It moves towards that at 1 / FADE_MS a millisecond. The gate starts shut
  // (a replay is silent until it plays), and the first call starts its clock.
  double Level(bool heard, double now_ms)
  {
    const double target = heard ? 1.0 : 0.0;
    if (m_started)
    {
      const double step = std::max(now_ms - m_at, 0.0) / FADE_MS;
      m_level = target > m_level ? std::min(target, m_level + step) : std::max(target, m_level - step);
    }
    m_started = true;
    m_at = now_ms;
    return m_level;
  }

  // The level as the last call left it.
  double Now() const { return m_level; }

  // Whether the level is still on its way (the host ticks faster meanwhile).
  bool Fading(bool heard) const { return m_level != (heard ? 1.0 : 0.0); }

  // The stream's volume, 0 to 100, for a level and the player's own volume: never above the player's.
  static int Volume(int own, double level)
  {
    const int clamped = std::clamp(own, 0, 100);
    return std::clamp(static_cast<int>(std::lround(clamped * level)), 0, clamped);
  }

private:
  double m_level = 0.0;
  double m_at = 0;
  bool m_started = false;
};
}  // namespace fzgx
