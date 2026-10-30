// The panel's look from the game's art (fzgx/panel.h): a picture of the game's size scaled for the screen, and the
// load's progress from the game's preload queue.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/panel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fzgx::panel
{
Picture ForThePicture(const art::Image& native, u32 picture_height, double scale)
{
  Picture out;
  if (native.Empty() || picture_height < 100 || picture_height > 16384 || scale < 0.1 || scale > 8)
    return out;
  const double down = picture_height / static_cast<double>(SCREEN_LINES) * scale, across = down;
  out.width = std::max<u32>(1, static_cast<u32>(std::lround(native.width * across)));
  out.height = std::max<u32>(1, static_cast<u32>(std::lround(native.height * down)));
  out.rgba.assign(std::size_t{out.width} * out.height * 4, 255);
  // each colour as it lies on black (its own value times its opacity), taken between the four nearest pixels
  const auto value = [&](int x, int y, int k) {
    x = std::clamp(x, 0, static_cast<int>(native.width) - 1), y = std::clamp(y, 0, static_cast<int>(native.height) - 1);
    const u8* p = native.At(x, y);
    return p[k] * p[3] / 255.0;
  };
  for (u32 y = 0; y < out.height; ++y)
  {
    const double sy = (y + 0.5) * native.height / out.height - 0.5;
    const int y0 = static_cast<int>(std::floor(sy));
    const double fy = sy - y0;
    for (u32 x = 0; x < out.width; ++x)
    {
      const double sx = (x + 0.5) * native.width / out.width - 0.5;
      const int x0 = static_cast<int>(std::floor(sx));
      const double fx = sx - x0;
      u8* to = &out.rgba[(std::size_t{y} * out.width + x) * 4];
      for (int k = 0; k < 3; ++k)
        to[k] = static_cast<u8>(std::clamp((value(x0, y0, k) * (1 - fx) + value(x0 + 1, y0, k) * fx) * (1 - fy) +
                                               (value(x0, y0 + 1, k) * (1 - fx) + value(x0 + 1, y0 + 1, k) * fx) * fy + 0.5,
                                           0.0, 255.0));
    }
  }
  return out;
}

u32 Progress::Field(bool race_mode, u32 queued, u32 done, Bytes bytes, void* context)
{
  // the first drain is the one that stands queued when the race's mode begins
  if (m_first == 0 && race_mode && queued > done && queued <= QUEUE_ENTRIES)
  {
    m_first = queued;
    m_base = done;
    m_total = bytes(context, m_base, m_first);
  }
  u32 now = 0;
  if (m_first != 0 && done < m_first && done >= m_base)
  {
    // by its bytes; a drain whose entries name no file, by its entries
    const double share = m_total != 0 ? static_cast<double>(bytes(context, m_base, done)) / static_cast<double>(m_total)
                                      : static_cast<double>(done - m_base) / (m_first - m_base);
    now = static_cast<u32>(std::clamp(share, 0.0, 1.0) * FIRST_DRAIN);
  }
  else if (m_first != 0)
    now = std::min(BEFORE_THE_INTRO, FIRST_DRAIN + REST_A_FIELD * ++m_after);
  m_best = std::max(m_best, now);
  return m_best;
}

u32 Progress::Held(u32 load, bool course_shown)
{
  const u32 now = course_shown ? std::min(BEFORE_THE_INTRO, HELD_LOAD + ++m_held_after)
                               : std::min(load, FULL) * HELD_LOAD / FULL;
  m_held_best = std::max(m_held_best, now);
  return m_held_best;
}
}  // namespace fzgx::panel
