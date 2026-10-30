// The advanced spectator controls (fzgx/spectator.h) applied to a replay played as the race, and to a live race
// watched: once a displayed frame, the pad and the overlay's clicks are turned into a racer to follow, a camera kind
// and, for those who may scrub, a move of the scrubber's play position (scrub_move.cpp takes it). For replays,
// and for a live race: a spectator's game once it watches behind
// live (spectate.cpp), and a racer who is out once the image's own spectating would begin (its out count at its cut).
// A live watcher never scrubs yet (a knocked-out racer never will: Role::KnockedOut), and live, every camera kind
// goes through the image's spectator cameras, the chase views too (its chase mode behind +252): the stand-in seat
// (the slot's +4) is the session's own seat there and is never written.
//
// Picture only. What is written, all of it the cameras' and the image's choices, never a machine, a progress record,
// an input row or a generator:
//   - the racer followed: the stand-in seat (SEAT_SLOT + 4), which the chase camera and the HUD follow, as the
//     scrubber's [ and ] do (playback.cpp SpectateReplay says what follows from a change);
//   - a chase view: the chase camera record's view ([.bss 0xF6C] + 0xA4, 1 to 3), which fn_1_ABAC otherwise takes
//     from the followed racer's own recorded presses;
//   - the TV kinds (the course's cameras, the automatic shots, the four manual shot types): the image's spectator
//     cameras, which run inside every step (frame_spectate.py `specstep`), turned on by the control block's out count
//     (+248, held at SPECTATE_CUT) and spectating word (+56), with the racer (+252) and the mode words
//     (memmap.SPEC_MODE, SPEC_HEIGHT, SPEC_ASKED, SPEC_PENDING) written here. The image's own pad reading
//     (`speccontrols`) is kept from acting: its "pressed before" word (+64) is set to every button, so nothing is
//     newly pressed for it;
//   - back from a TV kind to the chase camera: one frame of the image's chase mode (it sets the chase camera up anew
//     behind +252, fn_1_5370(0, N)), then the image's cameras off and the HUD's parts as they were (+60, which the
//     image keeps while it does not spectate).
// The scrubber moves the play position's memory, the cameras with it: every word is written again each frame, and a
// set-up is asked again (SPEC_PENDING) after any move of the picture that is not the next step.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"
#include "files.h"
#include "out_test.h"

#include <bit>
#include <optional>

namespace fzgx::net
{
namespace
{
namespace sp = spectator;
constexpr u32 PAD_STATUS = BSS + 0x998;  // PADStatus of port 0: buttons (u16), stick, C-stick (+4 x, +5 y)
constexpr u32 SPECTATING = CONTROL + 56, RACER = CONTROL + 252, PAD_BEFORE = CONTROL + 64, HUD_KEPT = CONTROL + 60;
constexpr u32 HUD_OFF = CONTROL + 68;
constexpr u32 SPEC_MODE = CONTROL + 0x3A0, SPEC_HEIGHT = CONTROL + 0x3A4, SPEC_PENDING = CONTROL + 0x3AC;
constexpr u32 SPEC_ASKED = CONTROL + 0x3C0;
constexpr u32 SPEC_CHASE = 6;            // memmap.SPEC_CHASE
constexpr u32 SPECTATE_CUT_FRAMES = 180;  // frame_spectate.py: the image's cameras act from this out count on
constexpr u32 HUD_MASK = BSS - 0x10000 + 0xB04;  // the HUD parts' mask (frame_spectate.py `spechud`)
constexpr u32 CAMERA_MODE = BSS + 0xF68, CHASE_RECORD = BSS + 0xF6C;
// The chase camera record's state (+0xA8: 2 once its racer has finished) and, in that state, its shot timer (+0x78,
// a halfword): it counts down a step at a time and, at 0, moves the view (+0xA4) on and starts again from 46, so a
// finished racer's own camera changes view every 47 steps (runs a2o1-1, sflk1-3: dumps before and after step 3800).
constexpr u32 CHASE_STATE = 0xA8, CHASE_TIMER = 0x78, CHASE_FINISHED = 2, CHASE_TIMER_HOLD = 46;
constexpr u32 CARS = BSS + 0x6D83C;      // the car objects (0x440 each, by a machine's +5; fn_1_868C0)
constexpr u32 TV_MODE = 9;               // [CAMERA_MODE] + 0x48: the TV cameras
constexpr int SCRUB_STEPS = 4;           // L or R held: steps a displayed frame (four times the race's speed)

// The image's mode for a TV kind (frame_spectate.py: 0 automatic, 1 the course's cameras, 2 to 5 a manual shot).
u32 ImageMode(sp::Kind kind)
{
  if (kind == sp::Kind::Auto)
    return 0;
  if (kind == sp::Kind::Course)
    return 1;
  return 2 + sp::FixedType(kind);
}

sp::Racers ReadRacers(Guest& g, u32 module, u32 machines, u32 count)
{
  sp::Racers racers{};
  for (u32 i = 0; i < count; ++i)
  {
    const u32 base = machines + i * game::MACHINE_SIZE;
    const u32 progress = g.Read32(base + game::MACHINE_PROGRESS);
    if (progress == 0)
      continue;
    sp::Racer& r = racers[i];
    r.present = true;
    r.out = out_test::Out(g, module, base);
    r.finished = (g.Read32(base) & game::MACHINE_FLAG_FINISHED) != 0;
    r.rank = g.Read8(progress + game::PROGRESS_RANK);
    r.distance = std::bit_cast<float>(g.Read32(progress + game::PROGRESS_DISTANCE));
  }
  return racers;
}

void Show(Guest& g, Session& s, const sp::Frame& f, const sp::Racers& racers, u32 machines, u32 count, u32 shown)
{
  const sp::Controls& c = *s.spectator.controls;
  SpectatorShow show;
  show.on = true;
  show.overlay = f.overlay;
  show.may_scrub = sp::MayScrub(c.role()) && ScrubOn(s);  // (a live watcher has no bar yet)
  show.follow = f.follow;
  show.how = static_cast<u32>(c.how());
  show.kind = static_cast<u32>(f.kind);
  show.kind_chosen = f.kind_chosen;
  show.kinds = c.rule().kinds;
  show.follow_others = c.rule().follow_others;
  for (u32 i = 0; i < count; ++i)
  {
    const sp::Racer& r = racers[i];
    if (!r.present)
      continue;
    const u32 base = machines + i * game::MACHINE_SIZE;
    const u32 progress = g.Read32(base + game::MACHINE_PROGRESS);
    SpectatorRow row;
    row.seat = static_cast<u8>(i);
    row.rank = static_cast<u8>(r.rank);
    row.laps = static_cast<u8>(std::min<u32>(g.Read32(progress + game::PROGRESS_LAPS), 255));
    row.out = r.out;
    row.finished = r.finished;
    const float most = static_cast<float>(g.Read16(base + game::MACHINE_MAX_ENERGY));
    const float energy = std::bit_cast<float>(g.Read32(base + game::MACHINE_ENERGY));
    row.energy = most > 0 ? std::clamp(energy / most, 0.0f, 1.0f) : 0.0f;
    // in race order
    u32 at = show.count++;
    for (; at > 0 && show.rows[at - 1].rank > row.rank; --at)
      show.rows[at] = show.rows[at - 1];
    show.rows[at] = row;
  }
  ObserverFill(g, s, show, racers, machines, count, shown);
  TheHost().ShowSpectator(show);
}

// The image's cameras on, for a TV kind or for the one frame that hands them back to the chase camera.
void ImageCameras(Guest& g, const sp::Frame& f, u32 mode, u32 height, bool again)
{
  g.Write32(OUT_FRAMES, SPECTATE_CUT_FRAMES);
  g.Write32(SPECTATING, 1);
  g.Write32(RACER, f.follow);
  g.Write32(SPEC_MODE, mode);
  g.Write32(SPEC_ASKED, mode);
  g.Write32(SPEC_HEIGHT, height);
  g.Write32(HUD_OFF, 0);
  if (again)
    g.Write32(SPEC_PENDING, 1);
}
// Who watches a live race: a spectator's game once it watches behind live; a racer who is out once the image's own
// count of its frames out reached the cut, where the image begins to spectate (frame_spectate.py). None: nobody.
std::optional<sp::Role> LiveRole(Guest& g, const Session& s)
{
  if (s.playback)
    return std::nullopt;
  if (s.watcher)
    return s.watch == Watch::Behind ? std::optional(sp::Role::Spectator) : std::nullopt;
  if (s.watch != Watch::Racing && g.Read32(OUT_FRAMES) >= SPECTATE_CUT_FRAMES)
    return sp::Role::KnockedOut;
  return std::nullopt;
}

// Live, every kind through the image's spectator cameras, written each frame (their words are the control block's,
// which no rollback puts back; the cameras take them inside every step): the chase kinds as its chase mode with the
// view as the height, the others as A1 has them. A course that allows nothing keeps the image's own camera.
void LiveCameras(Guest& g, SpectatorState& st, const sp::Controls& c, const sp::Frame& f)
{
  if (c.rule().kinds == 0 || f.follow >= sp::MOST)
    return;
  // Behind the chase camera the game's HUD is the local seat's and the image keeps it off (`spechud`), so the game's HUD
  // view, the watched racer's HUD, would show nothing: that view takes the automatic shots, which the HUD follows.
  const bool wants_hud = f.view == sp::View::Game;
  const bool chase = sp::IsChase(f.kind) && !wants_hud;
  const u32 mode = chase ? SPEC_CHASE : sp::IsChase(f.kind) ? ImageMode(sp::Kind::Auto) : ImageMode(f.kind);
  ImageCameras(g, f, mode, chase ? sp::ChaseView(f.kind) - 1 : f.variant,
               f.kind_changed || f.follow_changed || !st.tv || std::exchange(st.camera_mode, mode) != mode);
  st.tv = true;
}
// The controls, at a watched race's first frame. A knocked-out racer goes on from the racer the image took at its
// cut (the leader then); the others from the seat followed (a replay's recorder, a spectator's first seat).
std::unique_ptr<sp::Controls> MakeControls(Guest& g, sp::Role role, u32 seat, u32 count)
{
  const u32 course = g.Read32(boot::RACE + 4);
  const u32 first = role == sp::Role::KnockedOut && g.Read32(RACER) < count ? g.Read32(RACER) : seat;
  auto controls = std::make_unique<sp::Controls>(role, course, first);
  Notice("fzgx: spectator: {} on course {}, followed from racer {}; {}",
         role == sp::Role::Replay ? "a replay" : role == sp::Role::Spectator ? "a spectator, live" : "a racer out, live",
         course, first,
         controls->rule().kinds == 0 ? "the course allows no other racer or camera (its scenery follows the camera's "
                                       "machine)" : "every racer and camera kind");
  return controls;
}

// The chase view is the spectator's (live, or a replay's chosen chase kind), written every step (frame_spectate.py
// `specstep`) or every frame (below). A finished racer's chase camera moves its view on by itself every 47 steps
// (CHASE_TIMER): the step drew the next view and the write after it put the chosen one back, one frame of another
// view every 0.78 s while the racer watched had finished (a one-frame flicker when spectating; run sflk1-3). Its
// timer is held here once a displayed frame, so the view stays the one chosen; a
// frame runs far fewer than 46 steps. Picture only: the camera record, which no step of the race reads.
void HoldTheFinishedView(Guest& g, u32 module, const sp::Controls& c, const sp::Frame& f, bool live)
{
  if (c.rule().kinds == 0 || !sp::IsChase(f.kind) || !(live || f.kind_chosen))
    return;
  const u32 chase = g.Read32(module + CHASE_RECORD);
  if (chase != 0 && g.Read16(chase + CHASE_STATE) == CHASE_FINISHED)
    g.Write16(chase + CHASE_TIMER, CHASE_TIMER_HOLD);
}

// The overlay's clicks and a test script's entries, to the controls (a live watcher's jumps are not taken: no bar).
void TakeClicks(SpectatorState& st, sp::Controls& c, ScrubState& a, bool live)
{
  const SpectatorClick click = TheHost().SpectatorClicks();
  const u32 follow_clicked = click.follow != NONE_STEP ? click.follow : std::exchange(a.script_follow, NONE_STEP);
  const int kind_clicked = click.kind >= 0 ? click.kind : std::exchange(a.script_kind, -1);
  if (follow_clicked != NONE_STEP)
    st.refused += c.Click(follow_clicked) ? (++st.clicks, 0) : 1;
  if (kind_clicked >= 0 && kind_clicked < static_cast<int>(sp::KINDS))
    st.refused += c.ClickKind(static_cast<sp::Kind>(kind_clicked)) ? (++st.clicks, 0) : 1;
  if (click.overlay)
    c.ClickOverlay();
  if (click.leader)
    c.ClickLeader();
  if (click.battle)
    c.ClickBattle();
  st.jump = click.jump != NONE_STEP && c.MayJump() && !live ? click.jump : NONE_STEP;
}
}  // namespace

void SpectatorFrame(Guest& g, Session& s, u32 step, u32 module)
{
  const std::optional<sp::Role> live = LiveRole(g, s);
  if (!s.as_race && !live)
  {
    if (s.spectator.controls && std::exchange(s.spectator.shown, false))
      ObserverOff(g, s, module, true);  // (a live race's: the racer races again, a rollback took the knock-out back)
    return;
  }
  if (g.Read16(module + PHASE) != 0x29)
  {
    if (s.spectator.controls && std::exchange(s.spectator.shown, false))
      ObserverOff(g, s, module, false);  // the race is over: the overlay goes
    return;
  }
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  if (machines == 0 || count == 0)
    return;
  SpectatorState& st = s.spectator;
  const u32 seat = g.Read32(SEAT_SLOT + 4);
  if (!st.controls)
    st.controls = MakeControls(g, live.value_or(sp::Role::Replay), seat, count);
  sp::Controls& c = *st.controls;
  if (!live)
    c.Seen(seat);
  TakeClicks(st, c, s.scrub, live.has_value());
  ScrubState& a = s.scrub;
  sp::Pad pad;
  pad.buttons = static_cast<u16>(g.Read16(module + PAD_STATUS) | std::exchange(a.script_pad, 0));
  pad.cstick_y = static_cast<std::int8_t>(g.Read8(module + PAD_STATUS + 5));
  const sp::Racers racers = ReadRacers(g, module, machines, count);
  const sp::Frame f = c.Next(pad, racers);
  // (a racer who is out: L, R, A and Start do nothing, the controls' restriction; counted for the log, a press each)
  constexpr u16 SCRUB_KEYS = sp::PAD_L | sp::PAD_R | sp::PAD_A | sp::PAD_START;
  if (c.role() == sp::Role::KnockedOut)
    st.held_back += std::popcount(static_cast<u32>(pad.buttons & SCRUB_KEYS & ~st.pad_before));
  st.pad_before = pad.buttons;
  st.scrub = f.scrub;
  st.live = f.live;
  st.pause = f.pause;

  if (f.follow_changed && f.follow < count)
  {
    if (!live)
      g.Write32(SEAT_SLOT + 4, f.follow);  // (live: the image's cameras' racer, LiveCameras)
    ++st.follows;
  }
  if (f.kind_changed)
  {
    ++st.kind_changes;
    st.kinds_used |= 1u << static_cast<u32>(f.kind);
  }
  // the picture moved otherwise than on by a step (with the scrubber: the step the frame before showed; held: the same)
  const u32 shown = ScrubOn(s) ? s.scrub.play_step : step;
  const bool moved = shown != st.last_step + 1 && shown != st.last_step;
  st.last_step = shown;
  if (live)
    LiveCameras(g, st, c, f);
  else if (f.kind_chosen && c.rule().kinds != 0)
  {
    const u32 mode_record = g.Read32(module + CAMERA_MODE);
    const bool tv_now = mode_record != 0 && g.Read8(mode_record + 0x48) == TV_MODE;
    if (!sp::IsChase(f.kind))
    {
      ImageCameras(g, f, ImageMode(f.kind), f.variant, f.kind_changed || f.follow_changed || moved || !st.tv);
      st.tv = true;
      st.leaving = false;
    }
    else if (tv_now && !st.leaving)
    {
      // back to the chase camera: one frame of the image's chase mode sets it up behind the racer
      ImageCameras(g, f, SPEC_CHASE, sp::ChaseView(f.kind) - 1, true);
      st.tv = st.leaving = true;
    }
    else
    {
      if (st.tv)
      {
        g.Write32(OUT_FRAMES, 0);
        g.Write32(SPECTATING, 0);
        if (const u32 kept = g.Read32(HUD_KEPT); kept != 0)
          g.Write32(module + HUD_MASK, kept);
        st.tv = st.leaving = false;
      }
      const u32 chase = g.Read32(module + CHASE_RECORD);
      if (chase != 0 && !tv_now)
        g.Write16(chase + 0xA4, static_cast<u16>(sp::ChaseView(f.kind)));
    }
  }
  HoldTheFinishedView(g, module, c, f, live.has_value());
  // the image's own spectator buttons never act on this pad (its "pressed before": every button)
  g.Write32(PAD_BEFORE, 0xFFFFFFFF);
  Show(g, s, f, racers, machines, count, shown);
  // the game's own HUD behind the observer HUD and the clean picture (observer_frame.cpp)
  ObserverGameHud(g, s, module, f.view, f.follow < count && sp::Racing(racers[f.follow]));
  st.shown = true;
  st.overlay = f.overlay;
  st.followed = f.follow;
}

void ResultsCamera(Guest& g, const Session& s, u32 module)
{
  g.Write32(RESULTS_KEEP, s.watcher ? 1 : 0);
  const sp::ResultsPick pick = sp::ReadResultsPick(g.Read32(RESULTS_CAMERA));
  if (!pick.said)
    return;
  g.Write32(RESULTS_CAMERA, 0);
  // what the game left of the local machine: its flags, +0x58C (0x10 out of contact) and its car object's +0x390
  // (0x01000000: not drawn, fn_1_25004)
  const u32 seat = g.Read32(SEAT_SLOT + 4);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 cars = g.Read32(module + CARS);
  const u32 count = g.Read8(module + MACHINE_COUNT);
  u32 flags = 0, flags2 = 0, drawn = 0;
  if (machines != 0 && seat < count)
  {
    const u32 base = machines + seat * game::MACHINE_SIZE;
    flags = g.Read32(base);
    flags2 = g.Read32(base + game::MACHINE_FLAGS2);
    if (cars != 0)
      drawn = g.Read32(cars + g.Read8(base + 5) * 0x440 + 0x390);
  }
  Notice("fzgx: results camera: racer {}, {} (the local seat {}: flags {:08x}, +0x58C {:x}, car +0x390 {:08x})",
         pick.racer, sp::ResultsWhyText(pick.why), seat, flags, flags2, drawn);
}

void SpectatorSummary(const Session& s)
{
  const SpectatorState& st = s.spectator;
  if (!st.controls)
    return;
  Notice("fzgx: spectator: {} changes of the racer followed, {} of the camera kind (kinds used: {:#x}), {} clicks, "
         "{} refused by the course's rule; followed at the end: racer {}; {} presses of scrub, pause or live refused (a "
         "racer out)", st.follows, st.kind_changes, st.kinds_used, st.clicks, st.refused, st.controls->follow(),
         st.held_back);
  ObserverSummary(s);
}
}  // namespace fzgx::net
