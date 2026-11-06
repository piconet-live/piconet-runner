// No box of the emulator's, nor of the system's in a mode with no window (alerts.cpp, fzgx/alerts.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace fzgx_port
{
// The emulator's alerts (PanicAlert, a question, a critical error) are lines of the runner's standard error and never a
// message box; from the runner's first argument of the port's (TakeArgument) on
void AlertsToTheLog();
// In a mode with no window (the controller helper, the pipeline build): none of the system's own boxes either
void NoSystemBoxes();
}  // namespace fzgx_port
