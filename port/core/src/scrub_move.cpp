// The replay scrubber (scrub.cpp runs the frame): where the play position goes in a frame. What is asked (the keys,
// a scrub on the bar, a test's script), how the play position's memory gets to another step, and the replay's end.
//
// A scrub: while the mouse button is held on the bar the frame shows the step under the pointer, silently, and
// buffering goes on; the pointer beyond what can be shown holds the picture at the newest step that can. Released,
// the replay goes on from there as it was before the press, playing or paused. A press let go at once is a jump.
//
// The memory gets to a step, in the order tried:
//   1. back within the play log (the last PLAY_LOG_STEPS steps played): its entries, picture state as it was shown;
//   2. a short way below the play log, when the log's oldest memory is the front's own of that step: on down by the
//      front's entries;
//   3. from a whole moment of the front's: the nearest key above the step (one every KEY_EVERY steps) or the front
//      edge itself when that is nearer, then down by the front's entries;
//   4. when the front's log of those steps was dropped (over the replay's memory budget): from the key below the
//      step, or the play position when it is nearer, by running the steps again before the frame's own (scrub.cpp,
//      CatchStep).
// So no move costs more than a copy of the changed pages and KEY_EVERY entries, however far the front edge is
// (unpacking a unit of them when they were packed); or, beyond the budget, KEY_EVERY steps run again.
// Memory reached by 2 or 3 has had no presentation task run on it since the race's first frame: a picture made
// from it has had one pass of them (its step's).
//
// The end. The replay ends where the race settles: the front edge finds the step that makes the race over (the
// image cuts its frame there: scrub.cpp, StepsDone), or the recording's last step when the race never settles in
// it. The play position never runs that step: the game would see the race over and go on to its results. So the
// last picture is the one before it; playing into it pauses there, and Space there starts again from the race's
// first step. The game's window stays until the player closes it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace fzgx::net
{
namespace
{
using Steady = std::chrono::steady_clock;
constexpr u32 JUMP_STEPS = 300;  // Left and Right: five seconds
constexpr u32 SCRUB_STEPS = 4;   // the pad's L or R held: steps a displayed frame

double Ms(Steady::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

// What the frame is asked for.
struct Wish
{
  u32 jump = NONE_STEP;   // a step to jump to
  u32 scrub = NONE_STEP;  // the step under the pointer of a scrub
  int frame = 0;          // a frame step: 1 on, -1 back (. and ,)
  int racer = 0;          // the racer watched: 1 the next, -1 the one before ([ and ]; the race view)
};

// Tests (Debug::replay_script): the entries whose frame has come, as keys pressed, a jump, a scrub's pointer.
u32 TakeScript(ScrubState& a, Wish* wish)
{
  u32 pressed = 0;
  if (!a.script_read)
    ReadScrubScript(a);
  for (; a.script_next < a.script.size() && a.frames >= a.script[a.script_next][0]; ++a.script_next)
  {
    const auto [frame, what, n] = a.script[a.script_next];
    switch (what)
    {
    case 'p': pressed |= KeyPause; break;
    case 'h': pressed |= KeyFirst; break;
    case 'e': pressed |= KeyLast; break;
    case 'j': wish->jump = n; break;
    case 's':
      a.script_held = true;
      a.script_pointer = n;
      a.script_speed = 0;
      break;
    case 'f': a.script_speed = n; break;
    case 'k': a.script_speed = -static_cast<long long>(n); break;
    case 'r': a.script_held = false; break;
    case 'n': pressed |= KeyFrameAhead; break;
    case 'v': pressed |= KeyFrameBack; break;
    case 'u': pressed |= KeyRacerBack; break;
    case 'i': pressed |= KeyRacerAhead; break;
    case 'b': a.script_pad |= n; break;
    case 'w': a.script_follow = n; break;
    case 'c': a.script_kind = static_cast<int>(n); break;
    default: break;
    }
  }
  return pressed;
}

Wish Wishes(Session& s, u32 play)
{
  ScrubState& a = s.scrub;
  Wish wish;
  u32 pressed = TheHost().ReplayKeys() | TakeScript(a, &wish);
  // the spectator controls' pad and overlay (spectator_frame.cpp; never for a knocked-out racer): Start, A as End,
  // an event's jump, L or R held
  const SpectatorState& sp = s.spectator;
  pressed |= (sp.pause ? u32{KeyPause} : 0u) | (sp.live ? u32{KeyLast} : 0u);
  if (sp.jump != NONE_STEP)
    wish.jump = sp.jump;
  else if (sp.scrub < 0)
    wish.jump = play > a.first + SCRUB_STEPS ? play - SCRUB_STEPS : a.first;
  else if (sp.scrub > 0 && (a.front_done || play + SCRUB_STEPS < a.front))
    wish.jump = play + SCRUB_STEPS;
  wish.racer = (pressed & KeyRacerAhead) != 0 ? 1 : (pressed & KeyRacerBack) != 0 ? -1 : 0;
  wish.scrub = TheHost().ReplayScrub();
  if (a.script_held)
  {
    a.script_pointer = std::clamp<long long>(a.script_pointer + a.script_speed, 0,
                                             static_cast<long long>(s.official.size()));
    wish.scrub = static_cast<u32>(a.script_pointer);
  }
  if (wish.scrub != NONE_STEP)
    return wish;  // (while the bar is held the keys do nothing)
  const u32 from = a.wait_for != NONE_STEP ? a.wait_for : play;
  if ((pressed & KeyPause) != 0 && a.front_done && play >= a.end)
  {
    a.paused = a.end_paused = false;
    wish.jump = a.first;
    Notice("fzgx: scrubber: play at the end: again from the race's first step, {}", a.first);
  }
  else if ((pressed & KeyPause) != 0)
  {
    a.paused = !a.paused;
    Notice("fzgx: scrubber: {} at step {} (the front edge is at {})", a.paused ? "paused" : "playing", play, a.front);
  }
  if ((pressed & KeyBack) != 0)
    wish.jump = from > JUMP_STEPS ? from - JUMP_STEPS : 0;
  if ((pressed & KeyAhead) != 0)
    wish.jump = from + JUMP_STEPS;
  if ((pressed & KeyFirst) != 0)
    wish.jump = a.first;
  if ((pressed & KeyLast) != 0)
    wish.jump = a.front_done ? a.end : a.front > a.first ? a.front - 1 : a.first;
  wish.frame = (pressed & KeyFrameAhead) != 0 ? 1 : (pressed & KeyFrameBack) != 0 ? -1 : 0;
  return wish;
}

// The play position's memory from before step `play` to before step `to` (this file's head: the four ways).
void Reach(Guest& g, Session& s, u32 play, u32 to)
{
  ScrubState& a = s.scrub;
  a.catch_left = 0;
  if (to == play)
    return;
  const auto started = Steady::now();
  const u32 oldest = s.rollback.Oldest();
  if (to < play && to >= oldest)
  {
    s.rollback.RestoreTo(g, play, to);
    ++a.jumps_play;
    ++a.one.by_logs;
    return;
  }
  u32 from = oldest;
  const u32 key = s.front.KeyAbove(to);
  const bool by_logs = to < play && oldest <= play && oldest == a.pure_at && oldest - to <= KEY_EVERY &&
                       s.front.Ready(g, oldest, to);
  if (by_logs)
  {
    s.rollback.RestoreTo(g, play, oldest);
    ++a.jumps_play;
    ++a.one.by_logs;
  }
  else if (key != NONE_STEP && key < a.front && s.front.Ready(g, key, to))
  {
    s.front.TakeKey(g, s.rollback, key, a.base);
    from = key;
    ++a.jumps_key;
    ++a.one.by_key;
  }
  else if (s.front.Ready(g, a.front, to))
  {
    s.rollback.Exchange(g, s.front, &a.other, true);
    from = a.front;
    ++a.jumps_front;
    ++a.one.by_edge;
  }
  else
  {
    // 4. the front's log of these steps was dropped (the budget): they are run again before the frame's own step,
    // from the play position when it is below and nearer than the key below, else from that key
    const u32 below = s.front.KeyBelow(to);
    if (to > play && (below == NONE_STEP || play >= below))
      a.catch_at = play;
    else
    {
      s.front.TakeKey(g, s.rollback, below, a.base);
      s.rollback.Adopt(g, s.front, below);
      g.Write32(STEP, below);
      a.pure_at = below;
      a.catch_at = below;
    }
    a.catch_left = to - a.catch_at;
    ++a.catch_frames;
    ++a.one.by_runs;
    return;
  }
  s.front.Apply(g, from, to);
  s.rollback.Adopt(g, s.front, to);
  g.Write32(STEP, to);
  a.pure_at = to;
  if (by_logs)
    return;
  const double ms = Ms(Steady::now() - started);
  a.jump_ms += ms;
  a.jump_most_ms = std::max(a.jump_most_ms, ms);
  a.jump_steps_most = std::max(a.jump_steps_most, from - to);
  if (!a.scrubbing)
  {
    Notice("fzgx: scrubber: jump from step {} to step {}: {} steps put back from {} at step {} in {:.2f} ms", play, to,
           from - to, from == a.front ? "the front edge" : "a key", from, ms);
  }
}

// A scrub's frame: the step under the pointer, or the newest that can be shown.
u32 Scrub(Guest& g, Session& s, u32 play, u32 pointer, u32 low, u32 high)
{
  ScrubState& a = s.scrub;
  const auto now = Steady::now();
  if (!std::exchange(a.scrubbing, true))
  {
    a.one = ScrubCount{};
    a.one.from = std::clamp(pointer, low, high);
    a.scrub_at = now;
    a.wait_for = NONE_STEP;
  }
  const u32 to = std::clamp(pointer, low, high);
  a.clamped = pointer > high;
  Reach(g, s, play, to);
  const double ms = Ms(Steady::now() - now);
  a.one.move_ms += ms;
  a.one.move_most_ms = std::max(a.one.move_most_ms, ms);
  ++a.one.frames;
  a.one.pictures += to != a.play_step ? 1 : 0;
  a.one.clamped += a.clamped ? 1 : 0;
  a.one.to = to;
  a.play_silent = a.held = true;
  return to;
}

// The bar is let go: the scrub's line, and its figures into all scrubs'.
void EndScrub(ScrubState& a)
{
  a.scrubbing = false;
  ScrubCount& one = a.one;
  one.ms = Ms(Steady::now() - a.scrub_at);
  Notice("fzgx: scrubber: scrub {}: from step {} to step {} in {} frames and {:.0f} ms: {} of them showed another "
         "step ({:.1f} pictures a second), {} with the pointer beyond what can be shown; the memory moved from a key "
         "{} times, from the front edge {}, by the logs alone {}, by running steps again {}; moving it took {:.2f} ms "
         "a frame, {:.2f} ms at most",
         a.all.scrubs + 1, one.from, one.to, one.frames, one.ms, one.pictures,
         one.ms > 0 ? one.pictures * 1000.0 / one.ms : 0.0, one.clamped, one.by_key, one.by_edge, one.by_logs,
         one.by_runs, one.frames ? one.move_ms / one.frames : 0.0, one.move_most_ms);
  ScrubCount& all = a.all;
  ++all.scrubs;
  all.frames += one.frames;
  all.pictures += one.pictures;
  all.clamped += one.clamped;
  all.by_key += one.by_key;
  all.by_edge += one.by_edge;
  all.by_logs += one.by_logs;
  all.by_runs += one.by_runs;
  all.ms += one.ms;
  all.move_ms += one.move_ms;
  all.move_most_ms = std::max(all.move_most_ms, one.move_most_ms);
}

// A frame step (. and ,): the replay pauses, and the picture is the next step's or the one before the picture shown
// (the frame before ran step `play` - 1), silently. On: step `play`, when it is buffered. Back: from the play log, or
// the front's log, or a key, as any move.
u32 FrameStep(Guest& g, Session& s, u32 play, int frame, u32 low, u32 high)
{
  ScrubState& a = s.scrub;
  a.paused = true;
  a.end_paused = false;
  a.wait_for = NONE_STEP;
  a.play_silent = true;
  if (frame > 0)
  {
    if (play > high)
    {
      Reach(g, s, play, play - 1);  // (not buffered yet, or the end: the picture stands)
      a.held = true;
      return play - 1;
    }
    ++a.frames_ahead;
    return play;
  }
  const u32 to = play >= low + 2 ? play - 2 : low;
  const u32 keyed = a.jumps_key + a.jumps_front + a.catch_frames;
  const auto started = Steady::now();
  Reach(g, s, play, to);
  const double ms = Ms(Steady::now() - started);
  ++a.frames_back;
  a.frames_back_keyed += a.jumps_key + a.jumps_front + a.catch_frames != keyed ? 1 : 0;
  a.frame_back_ms += ms;
  a.frame_back_most_ms = std::max(a.frame_back_most_ms, ms);
  return to;
}

// The racer watched in the race view, one on or one back ([ and ], as the D-pad's right and left): the stand-in seat,
// which the camera and the HUD follow (playback.cpp, SpectateReplay).
void ChangeRacer(Guest& g, Session& s, int by)
{
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  if (!s.as_race || count == 0)
    return;
  // a course whose scenery follows the camera's machine (spectator.h RuleFor: 41) keeps the recorder's racer
  if (s.spectator.controls && !s.spectator.controls->rule().follow_others)
    return;
  const u32 seat = g.Read32(SEAT_SLOT + 4) % count;
  g.Write32(SEAT_SLOT + 4, by > 0 ? (seat + 1) % count : (seat + count - 1) % count);
  ++s.scrub.racer_changes;
}

// The step this frame runs for its picture, with the memory brought to before it: a scrub's; or the one a jump
// asks for; or, while the picture stands (paused, at the end, or waiting for the front edge), the one before again;
// or the next one.
u32 MovePlay(Guest& g, Session& s, u32 play, const Wish& wish)
{
  ScrubState& a = s.scrub;
  const u32 low = a.first;
  // the newest step that may be run for a picture: below the front edge, and never the replay's end
  const u32 newest = a.front_done ? a.end : a.front;
  const u32 high = std::max(low, newest > 0 ? newest - 1 : 0);
  a.play_silent = a.held = a.clamped = false;
  if (wish.scrub != NONE_STEP)
    return Scrub(g, s, play, wish.scrub, low, high);
  if (a.scrubbing)
    EndScrub(a);
  // a replay that paused itself at its end plays on when it is taken away from there (a scrub, a jump)
  if (a.end_paused && play < a.end)
    a.paused = a.end_paused = false;
  u32 jump = wish.jump;
  if (jump != NONE_STEP)
    a.wait_for = NONE_STEP;
  if (jump != NONE_STEP && jump > high && !a.front_done)
  {
    a.wait_for = std::min<u32>(jump, s.official.empty() ? 0 : static_cast<u32>(s.official.size()) - 1);
    jump = NONE_STEP;
    Notice("fzgx: scrubber: a jump to step {}, beyond the front edge at {}: the picture stands until it is there",
           a.wait_for, a.front);
  }
  if (jump == NONE_STEP && a.wait_for != NONE_STEP && (a.wait_for <= high || a.front_done))
    jump = std::exchange(a.wait_for, NONE_STEP);
  if (jump != NONE_STEP)
  {
    const u32 to = std::clamp(jump, low, high);
    Reach(g, s, play, to);
    return to;
  }
  if (wish.frame != 0)
    return FrameStep(g, s, play, wish.frame, low, high);
  if (a.front_done && play >= a.end && !a.paused)
  {
    a.paused = a.end_paused = true;
    if (!std::exchange(a.end_said, true))
      Notice("fzgx: scrubber: the replay is at its end, before step {}: paused on its last picture", a.end);
  }
  if ((a.paused || a.wait_for != NONE_STEP) && play > low)
  {
    Reach(g, s, play, play - 1);
    a.play_silent = a.held = true;
    return play - 1;
  }
  return play;
}
}  // namespace

u32 ScrubMove(Guest& g, Session& s, u32 play)
{
  const Wish wish = Wishes(s, play);
  if (wish.racer != 0)
    ChangeRacer(g, s, wish.racer);
  return MovePlay(g, s, play, wish);
}
}  // namespace fzgx::net
