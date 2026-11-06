// The emulator's alerts (its message boxes: Common/MsgHandler's PanicAlert, a question, a critical error) as lines of
// the runner's log. No process of the runner ever shows one: the launcher starts the game, the controller helper and
// the pipeline build without a console and, the last two, without a window, and a box took the player's focus and held
// its process until somebody clicked it (the owner's "IOS_FS: Failed to rename temporary FST file", 2026-11-06). The
// adapter registers the handler at the runner's first argument of the port's (alerts.cpp), before the emulator does
// anything; this part says what each alert answers and the line it is written as, for the host and the core's tests.
//
// An alert that ends nothing is a line and no more. A question is answered as the emulator's own boxless default
// answers it (Common/MsgHandler.cpp DefaultMsgHandler outside Windows: "no"), and a panic that asks ("Ignore and
// continue?" of an ASSERT, "Press Yes to make another attempt" of a NAND file) as the game answered it before, when its
// panic handlers were off and the emulator said "yes" itself (ShowMessageAlert). Nothing here ends the game: an alert
// that comes before a failure is followed by the failure's own end (the runner's exit status, which the launcher says).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace fzgx::alerts
{
// Common::MsgType's four
enum class Kind
{
  Information,
  Question,
  Warning,
  Critical,
};

// The answer to an alert: what a box's button would have said (only a yes/no alert's is read by the emulator)
bool Answer(Kind kind, bool yes_no);

// How many alert lines one process writes; the next one says that no more are written, and the rest are not
// (an alert in a loop must not fill the disc). The answers go on being given.
constexpr std::size_t MOST_LINES = 200;

// The lines of one process's alerts, in order
class Lines
{
public:
  // The line for an alert (no newline), or empty once MOST_LINES were given (the one after them says so)
  std::string Line(Kind kind, const std::string& caption, const std::string& text, bool yes_no);

private:
  std::size_t m_written = 0;
};
}  // namespace fzgx::alerts
