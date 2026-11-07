// Which of a mod pack's files the runner takes (fzgx/mod_files.h says the rules).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/mod_files.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

namespace fzgx::mod_files
{
namespace
{
std::string Lower(std::string text)
{
  std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool StartsWith(const std::string& text, const std::string& start) { return text.starts_with(start); }
bool EndsWith(const std::string& text, const std::string& end) { return text.ends_with(end); }

// A path under files/ as the disc spells its files: names joined by '/', none empty, "." or "..". Anything else
// could name a file outside files/ (the emulator's file patches read "main.dol" alone as the executable, and a path
// starting with '/' from the disc's root).
bool PathUnderFiles(const std::string& disc)
{
  if (disc.empty() || disc.front() == '/' || disc.back() == '/' || disc.find('\\') != std::string::npos ||
      disc.find(':') != std::string::npos)
    return false;
  std::size_t at = 0;
  while (at <= disc.size())
  {
    const std::size_t end = std::min(disc.find('/', at), disc.size());
    const std::string name = disc.substr(at, end - at);
    if (name.empty() || name == "." || name == ".." ||
        std::ranges::any_of(name, [](unsigned char c) { return c < 0x20 || c == 0x7F; }))
      return false;
    at = end + 1;
  }
  return true;
}

bool RaceFile(const std::string& lower)
{
  return lower == "game/cardata.lz" || (StartsWith(lower, "stage/coli_course") && EndsWith(lower, ".lz"));
}
}  // namespace

std::string Refusal(const std::string& disc, bool offline_game, bool race_same)
{
  if (!PathUnderFiles(disc))
    return "is no path under the disc's files/ ('/' between names, no '..')";
  // the disc's names are matched without regard to case, as the emulator's file patches match them
  const std::string path = Lower(disc);
  const std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
  if (StartsWith(path, "sys/") || StartsWith(path, "&&systemdata/"))
    return "is a system file of the disc, never taken from a mod";
  if (name == "main.dol" || EndsWith(name, ".rel") || StartsWith(path, "enemy_line/"))
    return "is the game's code (main.dol, a .rel, enemy_line), never taken from a mod: its changes come as packs";
  if (!offline_game && !race_same && RaceFile(path))
    return "is a race file (car data, a course) whose race fields the launcher did not find equal to the disc's "
           "(no raceSame): taken so only in the offline game";
  return {};
}

Plan Choose(const std::vector<OverlayFile>& asked, bool offline_game,
            const std::function<bool(const std::string& disc)>& on_disc,
            const std::function<bool(const std::string& from)>& is_file)
{
  Plan plan;
  std::map<std::string, std::size_t> taken;  // a disc path, without regard to case: its place in plan.files
  for (const OverlayFile& entry : asked)
  {
    if (const std::string why = Refusal(entry.disc, offline_game, entry.race_same); !why.empty())
    {
      plan.refused.push_back(std::format("\"{}\" {}", entry.disc, why));
      continue;
    }
    if (!on_disc(entry.disc))
    {
      ++plan.not_on_disc;
      continue;
    }
    if (!is_file(entry.from))
    {
      ++plan.no_source;
      continue;
    }
    const auto [it, first] = taken.emplace(Lower(entry.disc), plan.files.size());
    if (first)
    {
      plan.files.push_back(entry);
      plan.race_files += RaceFile(Lower(entry.disc));
    }
    else
    {
      plan.files[it->second].from = entry.from;  // a later entry for the same path wins
      ++plan.replaced;
    }
  }
  return plan;
}

std::string Summary(const Plan& plan, bool offline_game)
{
  std::string line = std::format("fzgx: a mod pack's {} file{} over the disc's ({})", plan.files.size(),
                                 plan.files.size() == 1 ? "" : "s",
                                 offline_game ? "the offline game: every file but the code's" :
                                                "a race or a playback: race files only with the disc's race fields");
  if (plan.race_files != 0)
    line += std::format("; {} of them race file{}", plan.race_files, plan.race_files == 1 ? "" : "s");
  if (!plan.refused.empty())
    line += std::format("; {} refused", plan.refused.size());
  if (plan.replaced != 0)
    line += std::format("; {} named again, the last taken", plan.replaced);
  if (plan.not_on_disc != 0)
    line += std::format("; {} not on the disc, passed over", plan.not_on_disc);
  if (plan.no_source != 0)
    line += std::format("; {} with no file to read, passed over", plan.no_source);
  return line;
}
}  // namespace fzgx::mod_files
