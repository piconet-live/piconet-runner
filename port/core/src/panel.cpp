// The panel over the picture before a race (fzgx/panel.h): what it shows and when, the game's font from the
// player's disc, and its text as a picture.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/panel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fzgx::panel
{
namespace
{
// The game's mode that holds a course (MD_GAME), and the phases of it in which the course is drawn: the course intro
// (COURSE_VIEW), the race's start, the race.
constexpr u32 MODE_GAME = 2, PHASE_RACE = 0x29;
// The characters' places in the font's own order.
constexpr u32 SLASH_AT = 18, DIGITS_AT = 58, UPPER_AT = 68, LOWER_AT = 94;

bool Show(Board& board, Shows shows)
{
  return board.shows.exchange(static_cast<u32>(shows), std::memory_order_relaxed) != static_cast<u32>(shows);
}

u32 GlyphOf(char c)
{
  if (c >= '0' && c <= '9')
    return DIGITS_AT + static_cast<u32>(c - '0');
  if (c >= 'A' && c <= 'Z')
    return UPPER_AT + static_cast<u32>(c - 'A');
  if (c >= 'a' && c <= 'z')
    return LOWER_AT + static_cast<u32>(c - 'a');
  return c == '/' ? SLASH_AT : 0;
}

// One line at the font's own size: how solid each pixel is (0 to 1), and the edge around the letters.
struct Strip
{
  u32 width = 0, height = 0;
  std::vector<float> ink, edge;
};

// The edge reaches EDGE pixels of the font from a letter, solid for three of them and fading over the last.
constexpr int EDGE = 4;
constexpr float INK_GAIN = 1.5f;
constexpr u32 MARGIN = 5;  // room on every side for the edge, and for a cell wider than its advance

double Advance(char c)
{
  return c == ' ' ? CELL : CELL * ADVANCE;
}

Strip StripOf(const Font& font, const std::string& line)
{
  double length = 0;
  for (const char c : line)
    length += Advance(c);
  // (a letter's cell reaches a sixth of a cell past its advance on either side: the margin holds it)
  Strip strip;
  strip.width = static_cast<u32>(std::ceil(length)) + 2 * MARGIN;
  strip.height = CELL + 2 * MARGIN;
  strip.ink.assign(std::size_t{strip.width} * strip.height, 0.0f);
  double at = MARGIN;
  for (const char c : line)
  {
    const auto& cell = font.Cell(c);
    // a character's cell is centred on its advance
    const u32 left = static_cast<u32>(std::lround(at + (Advance(c) - CELL) / 2));
    for (u32 y = 0; y < CELL; ++y)
      for (u32 x = 0; x < CELL; ++x)
      {
        float& ink = strip.ink[std::size_t{y + MARGIN} * strip.width + left + x];
        // (the game's letters stand fuller than the file's values alone give: half as solid again)
        ink = std::max(ink, std::min(1.0f, cell[y * CELL + x] * INK_GAIN / 15.0f));
      }
    at += Advance(c);
  }
  strip.edge.assign(strip.ink.size(), 0.0f);
  for (int y = 0; y < static_cast<int>(strip.height); ++y)
    for (int x = 0; x < static_cast<int>(strip.width); ++x)
    {
      float most = 0;
      for (int dy = -EDGE; dy <= EDGE; ++dy)
        for (int dx = -EDGE; dx <= EDGE; ++dx)
        {
          const int sx = x + dx, sy = y + dy;
          if (sx < 0 || sy < 0 || sx >= static_cast<int>(strip.width) || sy >= static_cast<int>(strip.height))
            continue;
          const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
          const float reach = std::clamp(EDGE - distance, 0.0f, 1.0f);
          most = std::max(most, strip.ink[std::size_t(sy) * strip.width + sx] * reach);
        }
      strip.edge[std::size_t(y) * strip.width + x] = most;
    }
  return strip;
}

// A value of a strip's field between its pixels (pixel centres at whole numbers plus a half).
float Sample(const std::vector<float>& field, const Strip& strip, double x, double y)
{
  x = std::clamp(x - 0.5, 0.0, strip.width - 1.0);
  y = std::clamp(y - 0.5, 0.0, strip.height - 1.0);
  const u32 x0 = static_cast<u32>(x), y0 = static_cast<u32>(y);
  const u32 x1 = std::min(x0 + 1, strip.width - 1), y1 = std::min(y0 + 1, strip.height - 1);
  const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
  const auto at = [&](u32 px, u32 py) { return field[std::size_t{py} * strip.width + px]; };
  return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
}

// The letters' colour by the height in the cell (0 its top, 1 its bottom), and the edge's as it lies on black.
constexpr float TOP[3] = {241, 251, 251}, BOTTOM[3] = {168, 247, 247}, EDGE_ON_BLACK[3] = {84, 92, 102};
// The letters stand between these rows of the cell (capitals from 3, descenders to 23).
constexpr float LETTERS_TOP = 3, LETTERS_BOTTOM = 20;
}  // namespace

void Board::Reset()
{
  shows = 0;
  ready = 0;
  of = 0;
  progress = 0;
  until_race = false;
  until_live = false;
  figure = 0;
}

Board& TheBoard()
{
  static Board board;
  return board;
}

void Begin(Board& board, bool until_race, bool until_live)
{
  board.Reset();
  board.until_race = until_race;
  board.until_live = until_live;
  Show(board, Shows::Loading);
}

bool CourseShown(u32 mode, u32 phase)
{
  return mode == MODE_GAME && phase >= PHASE_INTRO && phase <= PHASE_RACE;
}

bool Frame(Board& board, u32 mode, u32 phase)
{
  if (board.until_race || board.until_live || board.Now() != Shows::Loading || !CourseShown(mode, phase))
    return false;
  return Show(board, Shows::Nothing);
}

bool RaceBegins(Board& board)
{
  if (!board.until_race || board.Now() != Shows::Loading)
    return false;
  return Show(board, Shows::Nothing);
}

bool CatchingUp(Board& board, u32 done, u32 of)
{
  if (!board.until_live || board.Now() != Shows::Loading || of == 0)
    return false;
  const u32 at = HELD_LOAD + static_cast<u32>(u64{FULL - HELD_LOAD} * std::min(done, of) / of);
  return board.progress.exchange(std::max(at, board.progress.load(std::memory_order_relaxed)),
                                 std::memory_order_relaxed) < at;
}

bool Live(Board& board)
{
  if (!board.until_live || board.Now() != Shows::Loading)
    return false;
  board.until_live = false;
  return Show(board, Shows::Nothing);
}

bool AtGate(Board& board, u32 waited_ms, u32 ready, u32 of)
{
  if (waited_ms < WAIT_SHOWN_MS)
    return false;
  const bool count = board.ready.exchange(ready, std::memory_order_relaxed) != ready;
  const bool all = board.of.exchange(of, std::memory_order_relaxed) != of;
  return Show(board, Shows::Waiting) || count || all;
}

bool Down(Board& board)
{
  return Show(board, Shows::Nothing);
}

bool Counting(Board& board, u32 figure)
{
  const bool changed = board.figure.exchange(figure, std::memory_order_relaxed) != figure;
  return Show(board, Shows::Countdown) || changed;
}

void Saved(Board& board, u32 along)
{
  board.saved_along.store(std::min(along, FULL), std::memory_order_relaxed);
  board.saves.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> Lines(Shows shows, u32 ready, u32 of)
{
  if (shows == Shows::Loading)
    return {"LOADING"};
  if (shows == Shows::Countdown)
    return {std::to_string(ready)};
  if (shows != Shows::Waiting)
    return {};
  std::vector<std::string> lines{"WAITING FOR PLAYERS"};
  if (of != 0)
    lines.push_back(std::to_string(std::min(ready, of)) + "/" + std::to_string(of));
  return lines;
}

bool Font::Load(const std::vector<u8>& file)
{
  m_cells.clear();
  if (file.size() != std::size_t{GLYPHS} * GLYPH_BYTES)
    return false;
  m_cells.resize(GLYPHS);
  for (u32 glyph = 0; glyph < GLYPHS; ++glyph)
  {
    const u8* bytes = file.data() + std::size_t{glyph} * GLYPH_BYTES;
    u32 nibble = 0;
    for (u32 block_y = 0; block_y < CELL; block_y += 8)
      for (u32 block_x = 0; block_x < CELL; block_x += 8)
        for (u32 y = 0; y < 8; ++y)
          for (u32 x = 0; x < 8; ++x, ++nibble)
          {
            const u8 byte = bytes[nibble / 2];
            m_cells[glyph][(block_y + y) * CELL + block_x + x] = nibble % 2 == 0 ? byte >> 4 : byte & 15;
          }
  }
  return true;
}

const std::array<u8, CELL * CELL>& Font::Cell(char character) const
{
  static const std::array<u8, CELL * CELL> none{};
  return m_cells.empty() ? none : m_cells[GlyphOf(character)];
}

u32 CellHeight(u32 picture_height)
{
  return std::max<u32>(8, (picture_height * NAME_LINES + PICTURE_LINES / 2) / PICTURE_LINES);
}

Picture Draw(const Font& font, const std::vector<std::string>& lines, u32 cell_height)
{
  Picture picture;
  if (!font.Loaded() || lines.empty() || cell_height < 8 || cell_height > 512)
    return picture;
  // a pixel of the font is this many pixels of the picture, down and across
  const double down = cell_height / static_cast<double>(CELL), across = down * CELL_ASPECT;
  std::vector<Strip> strips;
  for (const std::string& line : lines)
  {
    strips.push_back(StripOf(font, line));
    picture.width = std::max(picture.width, static_cast<u32>(std::ceil(strips.back().width * across)));
  }
  const u32 pitch = static_cast<u32>(std::lround(cell_height * LINE_PITCH));
  const u32 strip_height = static_cast<u32>(std::ceil(strips[0].height * down));
  picture.height = pitch * (static_cast<u32>(lines.size()) - 1) + strip_height;
  picture.rgba.assign(std::size_t{picture.width} * picture.height * 4, 0);
  for (std::size_t i = 3; i < picture.rgba.size(); i += 4)
    picture.rgba[i] = 255;
  for (std::size_t n = 0; n < strips.size(); ++n)
  {
    const Strip& strip = strips[n];
    const u32 wide = static_cast<u32>(std::ceil(strip.width * across));
    const u32 left = (picture.width - wide) / 2, top = pitch * static_cast<u32>(n);
    for (u32 y = 0; y < strip_height; ++y)
    {
      const double sy = (y + 0.5) / down;
      const float along =
          std::clamp((static_cast<float>(sy) - MARGIN - LETTERS_TOP) / (LETTERS_BOTTOM - LETTERS_TOP), 0.0f, 1.0f);
      for (u32 x = 0; x < wide; ++x)
      {
        const double sx = (x + 0.5) / across;
        const float ink = Sample(strip.ink, strip, sx, sy), edge = Sample(strip.edge, strip, sx, sy);
        u8* pixel = &picture.rgba[(std::size_t{top + y} * picture.width + left + x) * 4];
        for (int c = 0; c < 3; ++c)
        {
          const float letter = TOP[c] + (BOTTOM[c] - TOP[c]) * along;
          const float value = EDGE_ON_BLACK[c] * edge * (1 - ink) + letter * ink;
          // (two lines' strips overlap by their margins: the brighter stays)
          pixel[c] = std::max(pixel[c], static_cast<u8>(std::clamp(value + 0.5f, 0.0f, 255.0f)));
        }
      }
    }
  }
  return picture;
}
}  // namespace fzgx::panel
