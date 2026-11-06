// The emulator's alerts as lines of the runner's log, never a message box (fzgx/alerts.h says why and what each
// answers). Registered at the runner's first argument of the port's (switches.cpp TakeArgument), so in every start of
// the port's: a game with its launch file, the controller helper and the pipeline build. The emulator's own handler
// on Windows is a MessageBox with no owner window (Common/MsgHandler.cpp DefaultMsgHandler).
//
// Each line goes to the process's standard error: the game's is runner.log (--fzgx-log), the pipeline build's is
// Logs/pipelines.log; the controller helper's goes nowhere anyone keeps, and the helper reads only the controllers'
// settings (controllers.cpp StartInput). Alerts come from any of the emulator's threads.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alerts.h"

#include <cstdio>
#include <mutex>
#include <string>

#include "Common/MsgHandler.h"

#include "fzgx/alerts.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fzgx_port
{
namespace
{
std::mutex s_alerts_mutex;
fzgx::alerts::Lines s_alert_lines;

fzgx::alerts::Kind KindOf(Common::MsgType style)
{
  switch (style)
  {
  case Common::MsgType::Question:
    return fzgx::alerts::Kind::Question;
  case Common::MsgType::Warning:
    return fzgx::alerts::Kind::Warning;
  case Common::MsgType::Critical:
    return fzgx::alerts::Kind::Critical;
  case Common::MsgType::Information:
  default:
    return fzgx::alerts::Kind::Information;
  }
}

bool LoggedAlert(const char* caption, const char* text, bool yes_no, Common::MsgType style)
{
  const fzgx::alerts::Kind kind = KindOf(style);
  std::string line;
  {
    std::lock_guard lock(s_alerts_mutex);
    line = s_alert_lines.Line(kind, caption ? caption : "", text ? text : "", yes_no);
  }
  if (!line.empty())
  {
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
  }
  return fzgx::alerts::Answer(kind, yes_no);
}
}  // namespace

void AlertsToTheLog()
{
  Common::RegisterMsgAlertHandler(LoggedAlert);
}

void NoSystemBoxes()
{
#ifdef _WIN32
  // the system's own: "has stopped working" at a crash (the process then ends at once, its exit status the crash's),
  // "there is no disk in the drive" and the like
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
}
}  // namespace fzgx_port
