// Practice with rewind: the decision of each displayed frame (fzgx/practice.h says what it is).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/practice.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fzgx::practice
{
double ScrubSpeed(int stick_x)
{
  const int tilt = std::abs(stick_x);
  if (tilt <= STICK_DEAD)
    return 0;
  // 0 just past the dead zone, 1 at full tilt
  const double u = std::min(1.0, static_cast<double>(tilt - STICK_DEAD) / (STICK_FULL - STICK_DEAD));
  // evenly from a quarter to the race's speed over the first half; then doubling three times over the second half
  const double speed = u <= 0.5 ? SLOWEST + (HALF_TILT - SLOWEST) * (u / 0.5)
                                : HALF_TILT * std::pow(FASTEST / HALF_TILT, (u - 0.5) / 0.5);
  return stick_x < 0 ? -speed : speed;
}

u32 Rewind::Position() const
{
  if (m_mode == Mode::Racing)
    return m_front;
  return static_cast<u32>(m_position);
}

Frame Rewind::Next(const Pad& pad)
{
  m_before = m_buttons;
  m_buttons = pad.buttons;
  Frame frame;
  // D-pad right saves the moment on screen: the front while racing, the position while paused or counting down
  if (Pressed(BUTTON_RIGHT))
  {
    m_save = Position();
    ++m_counts.saves;
    frame.saved = true;
  }
  else if (Pressed(BUTTON_LEFT) && m_save)
  {
    Load();
    frame.loaded = true;
  }
  else if (Pressed(BUTTON_START))
  {
    switch (m_mode)
    {
    case Mode::Racing:
      m_mode = Mode::Paused;
      m_position = m_front;
      ++m_counts.pauses;
      break;
    case Mode::Paused:
      m_mode = Mode::Countdown;
      m_countdown = COUNTDOWN;
      break;
    case Mode::Countdown:  // Start in the countdown stands again where it was
      m_mode = Mode::Paused;
      m_countdown = 0;
      break;
    }
  }
  else if (m_mode == Mode::Paused)
  {
    const double moved = m_position + ScrubSpeed(pad.stick_x);
    m_position = std::clamp(moved, static_cast<double>(Oldest()), static_cast<double>(m_front));
  }
  if (m_mode == Mode::Countdown && !frame.loaded && m_countdown > 0 && --m_countdown == 0)
  {
    const u32 at = Position();
    m_mode = Mode::Racing;
    if (at < m_front)
    {
      Branch(at);
      frame.branch = true;
    }
  }
  frame.run = m_mode == Mode::Racing;
  frame.step = Position();
  return frame;
}

void Rewind::Load()
{
  m_position = *m_save;
  m_mode = Mode::Countdown;
  m_countdown = COUNTDOWN;
  ++m_counts.loads;
}

void Rewind::Seek(u32 step)
{
  if (m_mode != Mode::Paused)
    return;
  m_position = std::clamp(step, Oldest(), m_front);
}

void Rewind::Ran(u32 step)
{
  if (step != m_front || m_mode != Mode::Racing)
  {
    ++m_misses;
    return;
  }
  if (WantsKey(step) && (m_kept.empty() || m_kept.back() < step))
    m_kept.push_back(step);
  m_front = step + 1;
  Expire();
}

// What fell out of the minute: the save, once the race has run a minute past it; the keys before the one the
// oldest moment is reached from.
void Rewind::Expire()
{
  const u32 oldest = Oldest();
  if (m_save && *m_save < oldest)
  {
    m_save.reset();
    ++m_counts.saves_expired;
  }
  const u32 key = KeyFor(oldest);
  const auto keep = std::lower_bound(m_kept.begin(), m_kept.end(), key);
  m_dropped.insert(m_dropped.end(), m_kept.begin(), keep);
  m_kept.erase(m_kept.begin(), keep);
}

// The race runs on from the moment before `at`: what came after it is gone, a save in it too.
void Rewind::Branch(u32 at)
{
  m_front = at;
  ++m_counts.branches;
  if (m_save && *m_save > at)
  {
    m_save.reset();
    ++m_counts.saves_dropped;
  }
  const auto after = std::upper_bound(m_kept.begin(), m_kept.end(), at);
  m_dropped.insert(m_dropped.end(), after, m_kept.end());
  m_kept.erase(after, m_kept.end());
}

bool Line::Ran(u32 step, std::vector<u8> row, u32 checksum)
{
  if (step < m_written || step > End())
    return false;
  if (step < End())
    m_steps.resize(step - m_first);  // a step run again: what followed it is another run's
  m_steps.push_back({std::move(row), checksum});
  return true;
}

void Line::Cut(u32 at)
{
  if (at < End())
    m_steps.resize(std::max(at, m_written) - m_first);
}

const Line::Step* Line::At(u32 step) const
{
  return step >= m_first && step < End() ? &m_steps[step - m_first] : nullptr;
}

std::vector<std::pair<u32, Line::Step>> Line::TakeFinal(u32 below)
{
  std::vector<std::pair<u32, Step>> out;
  for (; m_written < std::min(below, End()); ++m_written)
    out.emplace_back(m_written, m_steps[m_written - m_first]);
  return out;
}

std::vector<u32> Rewind::TakeDropped()
{
  std::vector<u32> out;
  out.swap(m_dropped);
  return out;
}
}  // namespace fzgx::practice
