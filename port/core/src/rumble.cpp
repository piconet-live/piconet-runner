// Whether the player's controllers may rumble (fzgx/core.h RumbleAllowed). The game asks for rumble for the racer
// the camera follows (the image's seatport.py: pad 0, the local controller), so a player who watches someone else
// would feel that racer's hits. So: no rumble while spectating, and a switch in the launcher's
// settings, on by default. The host holds the motors still while this says false (the runner: the emulator's rumble
// hook, HostHooks rumble_allowed); nothing of the game changes, and its rumble requests are computed as ever.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

namespace fzgx
{
namespace net
{
bool RumbleWanted(const Options& o, const Session* s, u32 step)
{
  if (!o.rumble || o.spectate || !o.play_file.empty())
    return false;  // switched off, a spectator's start, a playback
  if (s == nullptr)
    return true;   // a plain start, or a race before its session: the game's own rumble (menus, offline play)
  if (s->watcher || s->playback)
    return false;
  // the player drives: not out or over the line (watching), and not a seat the AI drives until it is given back
  return s->watch == Watch::Racing && !s->returning && s->own_from != NEVER && step >= s->own_from;
}
}  // namespace net

bool RumbleAllowed()
{
  if (!Active())
    return TheOptions().rumble;  // (no game of ours: a plain start of the fork's kind, as the switch says)
  const net::Session* s = net::s_session.get();
  u32 step = 0;
  if (s != nullptr)
  {
    const Guest g(TheHost(), Data());
    step = g.Read32(net::STEP);
  }
  return net::RumbleWanted(TheOptions(), s, step);
}
}  // namespace fzgx
