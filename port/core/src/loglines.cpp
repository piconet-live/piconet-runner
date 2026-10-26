// The game's log as a file a player sends (fzgx/loglines.h): the emulator's lines that go into it, a line without
// the player's folders.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/loglines.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace fzgx::loglines
{
namespace
{
char Lower(char c)
{
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c == '\\' ? '/' : c;
}

// Where `part` is in `text` from `from` on, letters compared without case and both separators as one.
std::size_t FindFolder(const std::string& text, const std::string& part, std::size_t from)
{
  const auto it = std::search(text.begin() + static_cast<std::ptrdiff_t>(std::min(from, text.size())), text.end(),
                              part.begin(), part.end(), [](char a, char b) { return Lower(a) == Lower(b); });
  return it == text.end() ? std::string::npos : static_cast<std::size_t>(it - text.begin());
}

bool Separator(char c)
{
  return c == '\\' || c == '/';
}

// A path that begins at `at` (a drive letter with its colon and a separator, or two separators and a name): where
// it ends, by what cannot be in a path or ends one in a sentence. 0: none begins here.
std::size_t PathEnd(const std::string& text, std::size_t at)
{
  const bool drive = at + 2 < text.size() && std::isalpha(static_cast<unsigned char>(text[at])) && text[at + 1] == ':' &&
                     Separator(text[at + 2]) && (at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1])));
  const bool share = at + 2 < text.size() && text[at] == '\\' && text[at + 1] == '\\' &&
                     std::isalnum(static_cast<unsigned char>(text[at + 2])) && (at == 0 || !Separator(text[at - 1]));
  if (!drive && !share)
    return 0;
  std::size_t end = at + 2;
  for (; end < text.size(); ++end)
  {
    const char c = text[end], next = end + 1 < text.size() ? text[end + 1] : ' ';
    if (std::string_view("\"'<>|*?\n\r\t()[]").find(c) != std::string_view::npos)
      break;
    if ((c == ':' || c == ',' || c == ';') && (next == ' ' || next == '\n'))
      break;
  }
  return end;
}

// An error's text with every word that holds a digit as one mark: two errors that differ in their numbers only.
std::string WithoutNumbers(const std::string& line)
{
  std::string key;
  for (std::size_t i = 0; i < line.size();)
  {
    std::size_t end = i;
    bool digit = false;
    while (end < line.size() && std::isalnum(static_cast<unsigned char>(line[end])))
      digit |= std::isdigit(static_cast<unsigned char>(line[end++])) != 0;
    if (end == i)
      key += line[i++];
    else
    {
      key += digit ? std::string("#") : line.substr(i, end - i);
      i = end;
    }
  }
  return key;
}
}  // namespace

std::string WithoutFolders(std::string text, const std::vector<Folder>& folders)
{
  for (const Folder& folder : folders)
  {
    if (folder.path.size() < 4)
      continue;
    for (std::size_t at = 0; (at = FindFolder(text, folder.path, at)) != std::string::npos; at += folder.word.size())
      text.replace(at, folder.path.size(), folder.word);
  }
  for (std::size_t at = 0; at < text.size(); ++at)
  {
    const std::size_t end = PathEnd(text, at);
    if (end == 0)
      continue;
    std::size_t name = end;
    while (name > at && !Separator(text[name - 1]))
      --name;
    text.erase(at, name - at);
  }
  return text;
}

void Filter::FlushOne(Tally& tally, std::vector<std::string>* out)
{
  if (tally.repeats != 0)
    out->push_back(std::format("({} more like it: \"{}{}\")", tally.repeats, tally.first.substr(0, 60),
                               tally.first.size() > 60 ? "..." : ""));
  tally.repeats = 0;
}

std::vector<std::string> Filter::Take(Kind kind, const std::string& line)
{
  Tally& tally = m_tallies[static_cast<std::size_t>(kind)];
  std::vector<std::string> out;
  // the budget is used up: said once, after what was held back; nothing more of this kind is looked at
  if (tally.written >= (kind == Kind::Error ? MOST_ERRORS : MOST_REPORTS))
  {
    FlushOne(tally, &out);
    if (!tally.full_said)
      out.push_back(kind == Kind::Error ? "(more errors of the emulator's came; no more are copied in this start)" :
                                          "(more report lines of the game's came; no more are copied in this start)");
    tally.full_said = true;
    return out;
  }
  std::string key = kind == Kind::Error ? WithoutNumbers(line) : line;
  if (tally.written != 0 && key == tally.key)
  {
    ++tally.repeats;
    return out;
  }
  FlushOne(tally, &out);
  ++tally.written;
  tally.key = std::move(key);
  tally.first = line;
  out.push_back(line);
  return out;
}

std::vector<std::string> Filter::Flush()
{
  std::vector<std::string> out;
  for (Tally& tally : m_tallies)
    FlushOne(tally, &out);
  return out;
}
}  // namespace fzgx::loglines
