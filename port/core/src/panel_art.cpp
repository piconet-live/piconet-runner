// The panel's look from the game's art (fzgx/panel.h): the venue name's letters cut from their strips, a picture
// of the game's size scaled for the screen, and the load's progress from the game's preload queue.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/panel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fzgx::panel
{
namespace
{
// The venues' names in the strips' order, top to bottom: which letter a strip's regions are.
constexpr const char* VENUES[STRIPS] = {"MUTECITY",   "PORTTOWN",     "BIGBLUE",    "LIGHTNING",
                                        "SANDOCEAN",  "GREENPLANT",   "FIREFIELD",  "CASINOPALACE",
                                        "OUTERSPACE", "AEROPOLIS",    "COSMOTERMINAL", "PHANTOMROAD"};
constexpr int GLOW = 5;        // pixels of glow a letter keeps
constexpr u32 LEAST = 6;       // pixels of the smallest region taken for a letter
constexpr int TIGHTER = 1, WORD_SPACE = 9, MARGIN = 6;

bool NearWhite(const u8* p)
{
  return p[3] >= 200 && p[0] >= 190 && p[1] >= 190 && p[2] >= 190;
}

struct Region
{
  int left, top, right, bottom;  // the box, right and bottom past the last pixel
  std::vector<int> pixels;       // x + y * STRIP_WIDTH, in the strip
};

// The connected regions (a pixel touches its eight neighbours) of near-white pixels of one strip, left to right.
std::vector<Region> Regions(const art::Image& image, u32 strip)
{
  const int w = STRIP_WIDTH, h = STRIP_HEIGHT, y0 = static_cast<int>(strip * STRIP_HEIGHT);
  std::vector<u8> seen(std::size_t(w) * h, 0);
  std::vector<Region> regions;
  std::vector<int> stack;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
    {
      if (seen[x + y * w] || !NearWhite(image.At(x, y0 + y)))
        continue;
      Region region{x, y, x + 1, y + 1, {}};
      stack.assign(1, x + y * w);
      seen[x + y * w] = 1;
      while (!stack.empty())
      {
        const int at = stack.back(), cx = at % w, cy = at / w;
        stack.pop_back();
        region.pixels.push_back(at);
        region.left = std::min(region.left, cx), region.right = std::max(region.right, cx + 1);
        region.top = std::min(region.top, cy), region.bottom = std::max(region.bottom, cy + 1);
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
          {
            const int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[nx + ny * w] || !NearWhite(image.At(nx, y0 + ny)))
              continue;
            seen[nx + ny * w] = 1;
            stack.push_back(nx + ny * w);
          }
      }
      if (region.pixels.size() >= LEAST)
        regions.push_back(std::move(region));
    }
  // a region inside another's box (within a pixel) is a part of that letter; the larger boxes first
  std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) {
    return (a.right - a.left) * (a.bottom - a.top) > (b.right - b.left) * (b.bottom - b.top);
  });
  std::vector<Region> letters;
  for (Region& region : regions)
  {
    const auto holder = std::find_if(letters.begin(), letters.end(), [&](const Region& k) {
      return region.left >= k.left - 1 && region.right <= k.right + 1 && region.top >= k.top - 1 &&
             region.bottom <= k.bottom + 1;
    });
    if (holder == letters.end())
      letters.push_back(std::move(region));
    else
      holder->pixels.insert(holder->pixels.end(), region.pixels.begin(), region.pixels.end());
  }
  std::sort(letters.begin(), letters.end(), [](const Region& a, const Region& b) { return a.left < b.left; });
  return letters;
}

// Which letter each pixel of the strip belongs to: the letters' own pixels, then GLOW rings around them, a pixel
// going to the letter that reaches it first (-1: none).
std::vector<int> Owners(const std::vector<Region>& letters)
{
  const int w = STRIP_WIDTH, h = STRIP_HEIGHT;
  std::vector<int> owner(std::size_t(w) * h, -1), front, next;
  for (std::size_t n = 0; n < letters.size(); ++n)
    for (const int at : letters[n].pixels)
    {
      owner[at] = static_cast<int>(n);
      front.push_back(at);
    }
  for (int ring = 0; ring < GLOW; ++ring)
  {
    next.clear();
    for (const int at : front)
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
          const int nx = at % w + dx, ny = at / w + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h || owner[nx + ny * w] != -1)
            continue;
          owner[nx + ny * w] = owner[at];
          next.push_back(nx + ny * w);
        }
    front.swap(next);
  }
  return owner;
}
}  // namespace

bool Lettering::Load(const art::Image& strips)
{
  for (Letter& letter : m_letters)
    letter = {};
  if (strips.width != STRIP_WIDTH || strips.height < STRIPS * STRIP_HEIGHT)
    return false;
  bool any = false;
  for (u32 strip = 0; strip < STRIPS; ++strip)
  {
    const std::string name = VENUES[strip];
    const std::vector<Region> letters = Regions(strips, strip);
    if (letters.size() != name.size())
      continue;  // not the strip known: none of its letters is taken
    const std::vector<int> owner = Owners(letters);
    for (std::size_t n = 0; n < letters.size(); ++n)
    {
      Letter& letter = m_letters[name[n] - 'A'];
      if (!letter.cut.Empty())
        continue;  // the first strip that has a letter gives it
      int left = STRIP_WIDTH, top = STRIP_HEIGHT, right = 0, bottom = 0;
      for (int at = 0; at < static_cast<int>(owner.size()); ++at)
        if (owner[at] == static_cast<int>(n))
        {
          left = std::min(left, at % int(STRIP_WIDTH)), right = std::max(right, at % int(STRIP_WIDTH) + 1);
          top = std::min(top, at / int(STRIP_WIDTH)), bottom = std::max(bottom, at / int(STRIP_WIDTH) + 1);
        }
      letter.cut = art::Image{u32(right - left), u32(bottom - top), std::vector<u8>(std::size_t(right - left) * (bottom - top) * 4, 0)};
      for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
          if (owner[x + y * STRIP_WIDTH] == static_cast<int>(n))
            std::copy_n(strips.At(x, strip * STRIP_HEIGHT + y), 4, letter.cut.At(x - left, y - top));
      letter.left = left - letters[n].left;
      letter.top = top;
      letter.width = static_cast<u32>(letters[n].right - letters[n].left);
      any = true;
    }
  }
  return any;
}

bool Lettering::Has(char letter) const
{
  return letter >= 'A' && letter <= 'Z' && !m_letters[letter - 'A'].cut.Empty();
}

bool Lettering::Writes(const std::string& line) const
{
  return !line.empty() && std::all_of(line.begin(), line.end(), [&](char c) { return c == ' ' || Has(c); });
}

art::Image Lettering::Line(const std::string& line) const
{
  if (!Writes(line))
    return {};
  int length = 0;
  for (const char c : line)
    length += c == ' ' ? WORD_SPACE : static_cast<int>(m_letters[c - 'A'].width) - TIGHTER;
  art::Image out{static_cast<u32>(length + TIGHTER + 2 * MARGIN), STRIP_HEIGHT, {}};
  out.rgba.assign(std::size_t{out.width} * out.height * 4, 0);
  for (std::size_t i = 3; i < out.rgba.size(); i += 4)
    out.rgba[i] = 255;
  int x = MARGIN;
  for (const char c : line)
  {
    if (c == ' ')
    {
      x += WORD_SPACE;
      continue;
    }
    const Letter& letter = m_letters[c - 'A'];
    for (u32 cy = 0; cy < letter.cut.height; ++cy)
      for (u32 cx = 0; cx < letter.cut.width; ++cx)
      {
        const int ox = x + letter.left + static_cast<int>(cx), oy = letter.top + static_cast<int>(cy);
        const u8* from = letter.cut.At(cx, cy);
        if (from[3] == 0 || ox < 0 || oy < 0 || ox >= static_cast<int>(out.width) || oy >= static_cast<int>(out.height))
          continue;
        u8* to = out.At(ox, oy);
        for (int k = 0; k < 3; ++k)
          to[k] = static_cast<u8>((from[k] * from[3] + to[k] * (255 - from[3]) + 127) / 255);
      }
    x += static_cast<int>(letter.width) - TIGHTER;
  }
  return out;
}

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
