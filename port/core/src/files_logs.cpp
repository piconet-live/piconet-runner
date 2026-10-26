// The game's log file and the logs kept beside it (fzgx/loglines.h, LogFileToWrite): the one place they are moved.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/loglines.h"

#include <string>

namespace fzgx::loglines
{
namespace
{
// FILE's name with `.word` before its extension: runner.log, "3": runner.3.log.
std::filesystem::path Beside(const std::filesystem::path& file, const std::string& word)
{
  std::filesystem::path beside = file;
  beside.replace_extension("." + word + file.extension().string());
  return beside;
}
}  // namespace

std::filesystem::path LogFileToWrite(const std::filesystem::path& file, int kept)
{
  std::error_code ec;
  if (file.has_parent_path())
    std::filesystem::create_directories(file.parent_path(), ec);
  if (!std::filesystem::exists(file, ec))
    return file;
  if (kept > 0)
  {
    std::filesystem::remove(Beside(file, std::to_string(kept)), ec);
    for (int n = kept - 1; n >= 1; --n)
      std::filesystem::rename(Beside(file, std::to_string(n)), Beside(file, std::to_string(n + 1)), ec);
    std::filesystem::rename(file, Beside(file, "1"), ec);
  }
  else
    std::filesystem::remove(file, ec);
  if (!std::filesystem::exists(file, ec))
    return file;
  // It is still there: another program holds it open (on Windows a file somebody has open is not moved or
  // removed). Writing it would empty it under that program: a file beside it instead.
  for (int n = 1; n <= 5; ++n)
  {
    const std::filesystem::path held = Beside(file, "held" + std::to_string(n));
    std::filesystem::remove(held, ec);
    if (!std::filesystem::exists(held, ec))
      return held;
  }
  return file;
}
}  // namespace fzgx::loglines
