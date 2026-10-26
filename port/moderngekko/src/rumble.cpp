// The controllers' motors held still while the player may not feel rumble (fzgx::RumbleAllowed: the launch file's
// "rumble", and the player drives a seat of their own; not while spectating, in a replay, or with their racer out or
// over the line). Cut at the emulator's one rumble path (recompcore 0048, HostHooks rumble_allowed, asked on the CPU
// thread in CSIDevice_GCController::Rumble for a standard controller's and the adapter's commands alike): a command
// that would run a motor is passed on as one that stops it. The game computes its rumble as ever: nothing of its
// memory changes. When the answer turns to false while a motor may run, every port is stopped once.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <format>

#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_DeviceGCController.h"
#include "Core/System.h"

namespace fzgx_port
{
namespace
{
u64 s_held = 0;          // commands that would have run a motor, passed on as stops
u64 s_passed = 0;        // ... and those that ran one
bool s_allowed = true;   // the answer of the last field (RumbleField)

bool Allowed()
{
  return s_state.options.rumble && fzgx::RumbleAllowed();
}
}  // namespace

void HoldRumble(HostHooks::Hooks& hooks)
{
  s_held = s_passed = 0;
  s_allowed = true;
  hooks.rumble_allowed = [](int) {
    const bool allowed = Allowed();
    ++(allowed ? s_passed : s_held);
    return allowed;
  };
}

void RumbleField()
{
  const bool allowed = Allowed();
  if (allowed == s_allowed)
    return;
  s_allowed = allowed;
  if (allowed)
    return;
  // (a motor the game started before may still run: the game's own stop would come only at its request's end)
  auto& si = Core::System::GetInstance().GetSerialInterface();
  for (int pad = 0; pad < 4; ++pad)
    SerialInterface::CSIDevice_GCController::Rumble(pad, 0.0, si.GetDeviceType(pad));
  s_core_host.Log(fzgx::LogLevel::Notice, "fzgx: rumble: held from here (the controllers' motors stopped)");
}

void ReportRumble()
{
  s_core_host.Log(fzgx::LogLevel::Notice,
                  std::format("fzgx: rumble: {} commands ran a motor, {} were held back (the launch file's "
                              "\"rumble\" {}; held while the player does not drive)",
                              s_passed, s_held, s_state.options.rumble ? "true" : "false"));
}
}  // namespace fzgx_port
