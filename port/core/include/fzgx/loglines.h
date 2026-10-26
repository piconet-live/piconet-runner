// The game's log as a file a player sends: which of the emulator's own lines go into it and how many (Filter), a
// line without the player's folders (WithoutFolders), and which file a start writes when the last games' logs are
// kept beside it (LogFileToWrite). Plain text and file names: the host has the emulator's and the system's side.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace fzgx::loglines
{
// A folder of the player's and the word a line gets in its place ("<user folder>").
struct Folder
{
  std::string path, word;
};
// `text` without the player's folders: each of `folders` becomes its word, however its separators and letter case
// are written; then every other absolute Windows path (a drive letter or \\server, to the end of what can be one)
// is cut down to what follows its last separator, the file's name. A folder's path holds the account's name.
// Not found: a path that is no file's and ends in plain words (its last folder stays), a relative path.
std::string WithoutFolders(std::string text, const std::vector<Folder>& folders);

// The emulator's lines that go into the game's log. Two kinds, each with a budget of its own, so that a burst of
// the one cannot keep the other out: what the game itself reports (its panic, its "GP WAIT Timeout"), and the
// emulator's errors. A line like the one before of its kind is not written again: a report that is the same text,
// an error that is the same but for its numbers. Their count follows when another line comes, or at Flush.
class Filter
{
public:
  enum class Kind { Report, Error };
  static constexpr int MOST_REPORTS = 400, MOST_ERRORS = 120;

  // What to write now for this line: nothing, the line, and before it what was held back.
  std::vector<std::string> Take(Kind kind, const std::string& line);
  // What is held back ("(N more like ...)"), for a moment the log should be whole: a freeze's report, the stop.
  std::vector<std::string> Flush();

private:
  struct Tally
  {
    int written = 0, repeats = 0;
    bool full_said = false;
    std::string key, first;
  };
  void FlushOne(Tally& tally, std::vector<std::string>* out);
  std::array<Tally, 2> m_tallies;
};

// The game's log is written to `file`, and the `kept` logs before it stay beside it (FILE's name with .1 to .KEPT
// before its extension, .1 the latest; the oldest goes). Makes room and says which file to write: `file`, when the
// one of that name could be moved aside or there was none. When it could not be moved (a game that never finished
// its shutdown still holds it: the log that says why) it is left as it is, and the answer is a file beside it,
// FILE's name with .held1 to .held5, the first that is free; `file` itself only when none of the five is.
std::filesystem::path LogFileToWrite(const std::filesystem::path& file, int kept);
}  // namespace fzgx::loglines
