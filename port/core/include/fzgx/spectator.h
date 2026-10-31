// The advanced spectator controls: who a watcher
// follows, with which camera, and what the pad and the overlay's mouse may do, for three kinds of watcher: a pure
// spectator, a replay, and a racer who is out of a live race (knocked out). This is the decision alone, as plain
// numbers with no game in it, so that the pad's mapping, the leader and the closest battle, the next racer in race
// order and the knocked-out racer's restriction are tested without one (tests/test_spectator.cpp). The session that
// applies it to the game (spectator_frame.cpp) reads the racers and the pad once a displayed frame and asks it.
//
// Picture only by design: a choice here is a racer to follow, a camera kind, an overlay shown or not, and for those
// who may scrub a move of the replay scrubber's play position. Nothing simulated is ever touched.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

namespace fzgx::spectator
{
using u16 = std::uint16_t;
using u32 = std::uint32_t;

constexpr u32 MOST = 30;           // racers in a race
constexpr u32 NOBODY = 0xFFFFFFFF;

// Who watches. Only a pure spectator and a replay may scrub: a knocked-out racer stays at live (no pause, no scrub,
// no jump to live; they close the window when done).
enum class Role
{
  Spectator,
  Replay,
  KnockedOut,
};
bool MayScrub(Role role);

// The pad as the game reads it (PADStatus, main.rel .bss 0x998), the player's own controls as the launcher maps
// them onto the GameCube's.
constexpr u16 PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008;
constexpr u16 PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040;
constexpr u16 PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000;
struct Pad
{
  u16 buttons = 0;
  int cstick_y = 0;  // signed, up positive (PADStatus +5)
};
constexpr int CSTICK_PUSHED = 40;  // the C-stick counts as pushed from here (frame_speccontrols.py's STICK)

// The camera kinds, in the order D-pad down goes through them (up goes back). Chase: the game's chase camera behind
// the followed racer, its views 1 to 3. Course: the course's own TV cameras (its camera files). Auto: the game's
// automatic TV shots. Fixed: the game's four manual shot types, each with its variants (the C-stick picks one).
// FirstPerson: the chase camera's view 0, a chase kind like the others; the last in the order, so D-pad up from Chase1
// reaches it, as a racer's D-pad up from view 1 does (the kinds' numbers before it stay).
enum class Kind : u32
{
  Chase1,
  Chase2,
  Chase3,
  Course,
  Auto,
  Fixed1,
  Fixed2,
  Fixed3,
  Fixed4,
  FirstPerson,
};
constexpr u32 KINDS = 10;
constexpr std::array<u32, 4> FIXED_VARIANTS{6, 20, 9, 2};  // memmap.SPEC_HEIGHTS of the manual shots
bool IsChase(Kind kind);
bool IsFixed(Kind kind);
u32 ChaseView(Kind kind);          // 1 to 3, 0 for FirstPerson (the game's view 0); 0 for another kind
u32 ChaseHeight(Kind kind);        // the image's height for a chase kind (memmap.SPEC_HEIGHT): view 1, 2, 3, 0 as 0, 1, 2, 3
u32 FixedType(Kind kind);          // 0 to 3; 0 for another kind
const char* KindName(Kind kind);   // as the overlay's buttons say it

// What the watcher sees over the picture; Z (or the overlay's mode button) takes the next, round. Observer: the
// observer HUD (fzgx/observer.h: the timing tower, the camera buttons, the racer card, the course map), the game's
// own HUD hidden. Game: the game's own HUD of the racer followed, and the course map. Clean: the picture alone.
enum class View : u32
{
  Observer,
  Game,
  Clean,
};
constexpr u32 VIEWS = 3;
View NextView(View view);
const char* ViewName(View view);   // as the overlay's mode button says it

// What a course allows (the camera gate's verdicts: what the gate found changes the race on that course is blocked
// there). Course 41: scenery whose collision follows the machine the game's camera follows (a known trap:
// data+0x2A83C): following any racer but the recorder's own, or moving the camera off it, changes the race.
struct CourseRule
{
  bool follow_others = true;  // another racer may be followed
  u32 kinds = (1u << KINDS) - 1;  // the camera kinds allowed (bits by Kind)
};
CourseRule RuleFor(u32 course);
bool KindAllowed(const CourseRule& rule, Kind kind);

// The racer the results' cameras follow, as the image picked it at the results' start (follow.py, the results
// camera's site; memmap.RESULTS_CAMERA: the top bit, why << 8, racer), or moved them to during the results
// (follow.py results_watch: Gone). Its own body gone (a wreck that exploded, a
// fall below the course), a racer out watches the winner; otherwise its own seat, for one of the other reasons.
enum class ResultsWhy : u32
{
  Own = 1,     // its own machine: finished, racing, or under the Restore rule
  Wreck = 2,   // out, its wreck still drawn: watched where it lies
  Winner = 3,  // out, its body gone (car object +0x390 0x01000000): the winner
  Course = 4,  // course 41: its own seat, as the course's rule asks
  Keep = 5,    // a spectator's game: the seat it watches
  Alone = 6,   // out, its body gone, and no racer left to follow: its own seat
  Gone = 7,    // during the results: its wreck, watched until then, went (a computer machine hit it): the winner
};
struct ResultsPick
{
  bool said = false;  // the word holds a pick
  u32 racer = 0;
  u32 why = 0;        // a ResultsWhy, or another number the image wrote
};
ResultsPick ReadResultsPick(u32 word);
const char* ResultsWhyText(u32 why);  // for the log

// A racer as the snapshot of the race shows it, read only.
struct Racer
{
  bool present = false;   // a machine with a progress record
  bool out = false;       // out by the game's own verdict (out_test.h)
  bool finished = false;
  u32 rank = 0;           // 0 the leader (the progress record's place, +0x115)
  float distance = 0;     // along the course over all laps (progress +0xF8)
};
using Racers = std::array<Racer, MOST>;

// Racers still in the race (present, not out, not finished).
bool Racing(const Racer& r);
// The leader still racing (lowest rank), or the leader of all present racers when none races; NOBODY when none.
u32 Leader(const Racers& racers);
// The closest battle: of two racers still racing next to each other in race order, the pair with the least
// distance between them; the one behind (the chase camera behind it sees both). NOBODY when there is no pair.
u32 ClosestBattle(const Racers& racers);
// The racer `by` places on (positive: further back) from `from` in race order, present racers only, round. A racer
// out stays in the order: a watcher may look at a wreck.
u32 ByRank(const Racers& racers, u32 from, int by);

// What a frame's pad asks for, before any role's restriction.
struct Asked
{
  int racer = 0;        // D-pad right: the next racer in race order (1), left the one before (-1)
  int kind = 0;         // D-pad down: the next camera kind (1), up the one before (-1)
  int variant = 0;      // C-stick up (1) or down (-1): a fixed shot's next or previous variant
  bool leader = false;  // X: follow the leader
  bool battle = false;  // Y: follow the closest battle
  bool overlay = false; // Z: the next view (View)
  int scrub = 0;        // L held: back (-1); R held: on (1)
  bool live = false;    // A: jump back to live
  bool pause = false;   // Start: pause or play
};
// The mapping: `held` the buttons down now, `before` those down the frame before; C-stick pushes count when they
// begin. L and R count while held. The role's restriction is applied: a knocked-out racer gets no scrub, live or
// pause.
Asked Map(Role role, const Pad& now, const Pad& before);

// How the followed racer is chosen.
enum class Follow
{
  Chosen,  // a racer the watcher picked (the D-pad, a click)
  Leader,  // whoever leads, as it changes
  Battle,  // the closest battle, held at least BATTLE_HOLD frames
};
constexpr u32 BATTLE_HOLD = 180;  // frames a battle is watched before another may take its place

// What a frame of the controls decided.
struct Frame
{
  u32 follow = NOBODY;       // the racer to follow
  Kind kind = Kind::Chase1;  // the camera kind
  bool kind_chosen = false;  // the watcher chose a kind: before, the game's own camera stands (a replay: the views
                             // its recording chose)
  u32 variant = 0;           // the fixed shot's variant
  bool follow_changed = false, kind_changed = false;
  View view = View::Observer;  // what is drawn over the picture
  bool overlay = true;       // ... not the clean picture: the course map shown
  int scrub = 0;             // L/R held: the play position moves back or on (scrubbing watchers only)
  bool live = false, pause = false;
};

// The watcher's controls through a race. `first` is the racer followed at the start (the recorder in a replay, the
// leader for a knocked-out racer).
class Controls
{
public:
  Controls(Role role, u32 course, u32 first);
  // A displayed frame: the pad, and the race as it is now.
  Frame Next(const Pad& pad, const Racers& racers);
  // The overlay's mouse: follow this racer (a standings row, a timeline lane or circle); a camera kind's button.
  // Applied at the next frame. False: not allowed (the course's rule).
  bool Click(u32 racer);
  bool ClickKind(Kind kind);
  void ClickOverlay();  // the next view, as Z
  void ClickLeader();   // follow the leader, as X
  void ClickBattle();   // follow the closest battle, as Y
  // The racer followed changed by other means (the game's own switch away from a racer who is out, the scrubber's
  // keys): the controls go on from it.
  void Seen(u32 racer);
  // A move of the play position asked by the overlay (an event's circle, the bar): only for those who may scrub.
  bool MayJump() const { return MayScrub(m_role); }

  Role role() const { return m_role; }
  u32 follow() const { return m_follow; }
  Follow how() const { return m_how; }
  Kind kind() const { return m_kind; }
  View view() const { return m_view; }
  const CourseRule& rule() const { return m_rule; }

private:
  void Choose(u32 racer, Follow how);
  bool SetKind(Kind kind);
  Role m_role;
  CourseRule m_rule;
  u32 m_first;
  u32 m_follow;
  Follow m_how = Follow::Chosen;
  Kind m_kind = Kind::Chase1;
  bool m_kind_chosen = false;
  u32 m_variant = 0;
  View m_view = View::Observer;
  u32 m_battle_frames = 0;  // frames the current battle has been watched
  Pad m_before;
  // the mouse's, taken at the next frame
  u32 m_clicked = NOBODY;
  int m_clicked_kind = -1;
  bool m_clicked_overlay = false, m_clicked_leader = false, m_clicked_battle = false;
  bool m_changed = false, m_kind_changed = false;
};
}  // namespace fzgx::spectator
