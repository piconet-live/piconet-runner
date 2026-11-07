// The emulator's alerts as lines of the runner's log (fzgx/alerts.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/alerts.h"

namespace fzgx::alerts
{
namespace
{
constexpr char BREAK[] = " / ";  // a line break of the alert's text, in its one line

bool EndsWithBreak(const std::string& s)
{
  return s.size() >= 3 && s.compare(s.size() - 3, 3, BREAK) == 0;
}
}  // namespace

bool Answer(Kind kind, bool yes_no)
{
  if (!yes_no)
    return true;  // (no question: the box's one button)
  // a question: "no", the emulator's own answer where it shows no box; a panic that asks: "yes", as the game's answer
  // was while its panic handlers were off
  return kind != Kind::Question;
}

std::string Lines::Line(Kind kind, const std::string& caption, const std::string& text, bool yes_no)
{
  if (m_written > MOST_LINES)
    return {};
  if (m_written++ == MOST_LINES)
    return "fzgx: emulator alert: " + std::to_string(MOST_LINES) + " written; no more are";
  std::string flat;
  flat.reserve(text.size());
  for (const char c : text)
  {
    if (c == '\r')
      continue;
    if (c != '\n')
      flat += c;
    else if (!flat.empty() && !EndsWithBreak(flat))
      flat += BREAK;  // (a run of breaks, a blank line among them, is one)
  }
  if (EndsWithBreak(flat))
    flat.resize(flat.size() - 3);
  std::string line = "fzgx: emulator alert (" + caption + "): " + flat;
  if (yes_no)
    line += Answer(kind, yes_no) ? " [answered yes]" : " [answered no]";
  return line;
}
}  // namespace fzgx::alerts
