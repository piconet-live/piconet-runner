// The player's file of race summaries (summary.h): one line a race, the last few hundred kept. The one place it is
// read and written. Read as lines and nothing more: no line of it is parsed here.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "summary.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace fzgx::net
{
namespace
{
// A file larger than this is not this program's (300 lines are under 200 KiB): it is not read, and is replaced.
constexpr std::size_t MOST_BYTES = std::size_t{4} << 20;

std::filesystem::path PathOf(const std::string& utf8)
{
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// The last `most` lines of `text` that hold anything, oldest first, each ended by one line feed (a carriage return
// before it is dropped). Found from the end: what stands before them is not looked at, so a file of two million
// one-byte lines costs what its last few do.
std::string LastLines(const std::string& text, std::size_t most)
{
  std::vector<std::pair<std::size_t, std::size_t>> found;  // a line's first byte, and the one after its last
  for (std::size_t end = text.size(); end != 0 && found.size() < most;)
  {
    const std::size_t feed = text.rfind('\n', end - 1);
    const std::size_t begin = feed == std::string::npos ? 0 : feed + 1;
    std::size_t last = end;
    while (last > begin && (text[last - 1] == '\r' || text[last - 1] == '\n'))
      --last;
    if (last > begin)
      found.emplace_back(begin, last);
    end = feed == std::string::npos ? 0 : feed;
  }
  std::string lines;
  for (auto it = found.rbegin(); it != found.rend(); ++it)
    lines.append(text, it->first, it->second - it->first).push_back('\n');
  return lines;
}
}  // namespace

bool AppendBounded(const std::string& path, const std::string& line, std::size_t keep, std::string* why)
{
  // The lines there. A file that is there and cannot be opened or read is left alone, with nothing written: read
  // as empty, 299 races' lines would become one. (No file yet: none. One over MOST_BYTES: not read, and replaced.)
  std::string old;
  std::error_code error;
  if (std::filesystem::exists(PathOf(path), error) || error)
  {
    File file(path, "rb");
    const std::size_t size = file.IsOpen() ? file.GetSize() : 0;
    if (!file.IsOpen())
    {
      *why = "the file is there and cannot be opened";
      return false;
    }
    if (size != 0 && size <= MOST_BYTES)
    {
      old.resize(size);
      if (!file.ReadBytes(old.data(), size))
      {
        *why = "the file is there and cannot be read";
        return false;
      }
    }
  }
  const std::string text = (keep > 1 ? LastLines(old, keep - 1) : std::string()) + line + '\n';
  // Written beside the file and moved over it, once every byte is in the file beside: a game ended half way, or a
  // disk that is full, leaves the file as it was and nothing beside it.
  const std::string beside = path + ".new";
  File file(beside, "wb");
  const bool opened = file.IsOpen();
  const bool written = opened && file.WriteBytes(text.data(), text.size()) && file.Flush();
  if (!file.Close() || !written)
  {
    *why = "the new text could not be written beside it";
    if (opened)
      std::filesystem::remove(PathOf(beside), error);
    return false;
  }
  std::filesystem::rename(PathOf(beside), PathOf(path), error);
  if (error)
  {
    *why = "the new file could not be moved over it (" + error.message() + ")";
    std::filesystem::remove(PathOf(beside), error);
    return false;
  }
  return true;
}
}  // namespace fzgx::net
