// The game window's kept place: its file, the file's text, and whether a kept window fits the monitors of now
// (fzgx/place.h). The one place the file is read and written.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/place.h"

#include "util.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>

namespace fzgx::place
{
namespace
{
constexpr std::string_view FIRST_WORD = "fzgx-window";
constexpr int VERSION = 1;
constexpr std::size_t MOST_BYTES = 256;  // a place is one short line: a longer file is not this program's

std::string FileIn(const std::string& folder)
{
  const bool ended = !folder.empty() && (folder.back() == '/' || folder.back() == '\\');
  return folder + (ended ? "" : "/") + FILE_NAME;
}

std::filesystem::path PathOf(const std::string& utf8)
{
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// The next field of a line of fields with one space between them; none when the line has ended.
std::optional<std::string_view> Field(std::string_view* rest)
{
  if (rest->empty())
    return std::nullopt;
  const std::size_t end = rest->find(' ');
  const std::string_view field = rest->substr(0, end);
  *rest = end == std::string_view::npos ? std::string_view{} : rest->substr(end + 1);
  if (field.empty() || (end != std::string_view::npos && rest->empty()))
    return std::nullopt;  // two spaces, or a space at the end
  return field;
}

std::optional<int> Whole(std::string_view* rest)
{
  const std::optional<std::string_view> field = Field(rest);
  int value = 0;
  if (!field)
    return std::nullopt;
  const auto [end, error] = std::from_chars(field->data(), field->data() + field->size(), value);
  if (error != std::errc{} || end != field->data() + field->size())
    return std::nullopt;
  return value;
}
}  // namespace

std::string Text(const Kept& kept)
{
  char text[160];
  std::snprintf(text, sizeof(text), "%.*s %d %d %d %d %d %d %d\n", static_cast<int>(FIRST_WORD.size()), FIRST_WORD.data(),
                VERSION, kept.window.left, kept.window.top, kept.window.right, kept.window.bottom, kept.maximized ? 1 : 0,
                kept.fullscreen ? 1 : 0);
  return text;
}

std::optional<Kept> Read(std::string_view text)
{
  // one line, with or without its end
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    text.remove_suffix(1);
  if (text.size() > 120)
    return std::nullopt;
  std::string_view rest = text;
  if (Field(&rest) != FIRST_WORD || Whole(&rest) != VERSION)
    return std::nullopt;
  int numbers[6] = {};
  for (int& number : numbers)
  {
    const std::optional<int> value = Whole(&rest);
    if (!value)
      return std::nullopt;
    number = *value;
  }
  if (!rest.empty())
    return std::nullopt;
  Kept kept;
  kept.window = {numbers[0], numbers[1], numbers[2], numbers[3]};
  for (const int edge : {numbers[0], numbers[1], numbers[2], numbers[3]})
  {
    if (edge < -MOST_FROM_ORIGIN || edge > MOST_FROM_ORIGIN)
      return std::nullopt;
  }
  if (kept.window.Width() < LEAST_SIDE || kept.window.Height() < LEAST_SIDE || kept.window.Width() > MOST_SIDE ||
      kept.window.Height() > MOST_SIDE || numbers[4] < 0 || numbers[4] > 1 || numbers[5] < 0 || numbers[5] > 1)
    return std::nullopt;
  kept.maximized = numbers[4] == 1;
  kept.fullscreen = numbers[5] == 1;
  return kept;
}

bool OnAScreen(const Rect& window, const std::vector<Rect>& work_areas, int strip_height, int reach)
{
  if (window.Width() <= 0 || window.Height() <= 0 || strip_height <= 0)
    return false;
  const int needed = std::min(reach, window.Width());
  // the strip's upper half: where the pointer takes the window
  const Rect strip{window.left, window.top, window.right, window.top + std::max(strip_height / 2, 1)};
  for (const Rect& area : work_areas)
  {
    const int width = std::min(strip.right, area.right) - std::max(strip.left, area.left);
    const bool rows = strip.top >= area.top && strip.bottom <= area.bottom;
    if (rows && width >= needed)
      return true;
  }
  return false;
}

std::optional<Kept> ReadKept(const std::string& folder)
{
  File file(FileIn(folder), "rb");
  if (!file.IsOpen())
    return std::nullopt;
  const std::size_t size = file.GetSize();
  std::string text(size, '\0');
  if (size == 0 || size > MOST_BYTES || !file.ReadBytes(text.data(), size))
    return std::nullopt;
  return Read(text);
}

bool WriteKept(const std::string& folder, const Kept& kept)
{
  const std::string path = FileIn(folder), beside = path + ".new", text = Text(kept);
  std::error_code error;
  File file(beside, "wb");
  const bool opened = file.IsOpen();
  const bool written = opened && file.WriteBytes(text.data(), text.size()) && file.Flush();
  if (!file.Close() || !written)
  {
    if (opened)
      std::filesystem::remove(PathOf(beside), error);
    return false;
  }
  std::filesystem::rename(PathOf(beside), PathOf(path), error);
  if (error)
  {
    std::filesystem::remove(PathOf(beside), error);
    return false;
  }
  return true;
}

void Tracker::BeginRestore(const Kept& kept)
{
  m_kept = kept;
  m_now = kept;
  m_have = true;
  m_restoring = true;
}

void Tracker::Note(const Seen& seen)
{
  if (seen.plain)
  {
    m_now.window = seen.rect;
    m_have = true;
  }
  m_now.maximized = seen.maximized;
  m_now.fullscreen = seen.fullscreen;
}

std::optional<Kept> Tracker::ToWrite() const
{
  if (!m_keeps || !m_have || m_restoring || m_dragging || (m_kept && *m_kept == m_now))
    return std::nullopt;
  return m_now;
}
}  // namespace fzgx::place
