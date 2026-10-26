// The panel over the picture before a race: LOADING while the game loads the course, WAITING FOR PLAYERS while it
// holds at the start gate for the relay's go. A start that goes straight into a race or a replay shows the game's
// start-up pictures (the logos, the title's first frame) for a few fields and then draws nothing until the course
// is loaded, eight seconds or so, so the last start-up picture stood on the screen all that time; and at the gate
// the game draws nothing either. The host draws the panel over whatever the game shows, on a black ground.
// Here: what the panel shows and when (Board, written by the game's thread and read by the thread that presents),
// the game's own 24 by 24 font read from the player's disc (Font), and the panel's text as a picture (Draw), in
// the look of the course name in the course intro. Nothing here reads or writes the game's memory, nothing of it
// is in a step, and once the race runs it costs the presenting thread one load of a flag a picture.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "fzgx/art.h"
#include "fzgx/host.h"

namespace fzgx::panel
{
enum class Shows : u32
{
  Nothing,
  Loading,  // from the start until the course's first picture (a replay held to the race: its first step)
  Waiting,  // at the start gate, joined to the relay, no go yet
  Countdown,  // a practice's second before the race runs on again (practice_session.cpp): its figure, over the
              // game's own picture (no black ground)
};

// What is published. One game per process, so there is one (TheBoard).
struct Board
{
  std::atomic<u32> shows{0};  // a Shows
  // Waiting: the players that are ready, of all players; `of` 0: not known, and the panel has no second line. The
  // relay tells a game neither today (protocol/formats.md, "The panel before a race").
  std::atomic<u32> ready{0}, of{0};
  // Loading: how far the load is, in thousandths (Progress, below): the meter under LOADING. Only ever grows.
  std::atomic<u32> progress{0};
  // A start held to the race (a replay played with its scrubber, Begin): LOADING stays up through the course intro
  // and the countdown, until the race's first step (RaceBegins).
  std::atomic<bool> until_race{false};
  // A game started again into its running race (Options::rejoin): LOADING stays up
  // through the load, the intro, the countdown and the race's steps run again unseen, until the game has caught up
  // with the race or the race is over (Live). The meter: the load fills the first HELD_LOAD thousandths, the steps
  // run of the race the rest (CatchingUp).
  std::atomic<bool> until_live{false};
  // Countdown: the figure shown, 3, 2, 1 over the second
  std::atomic<u32> figure{0};
  // A practice's save (D-pad right), whatever the panel shows: how many saves there were (the host marks each new
  // one briefly), and where the last lies along the rewind's bar, in thousandths.
  std::atomic<u32> saves{0}, saved_along{0};

  Shows Now() const { return static_cast<Shows>(shows.load(std::memory_order_relaxed)); }
  void Reset();
};
Board& TheBoard();

// The game's thread's side, in the order a start goes through them. Each says whether what the panel shows
// changed, which is when the host is told (Host::PanelChanged).
// A start that goes straight into a race or a replay: LOADING from its first picture. `until_race`: a replay played
// with its scrubber, whose first picture is the race's first step: LOADING stays up over the course intro and the
// countdown too, which run unseen (Frame never takes it down), until RaceBegins.
void Begin(Board& board, bool until_race = false, bool until_live = false);
// Whether the game's mode word and the phase word of its race mode say the course is loaded and drawn: the mode is
// the race's or the replay's, and its phase the course intro's (0x27) or a later one of the race, up to the race's
// own (0x29).
bool CourseShown(u32 mode, u32 phase);
constexpr u32 PHASE_INTRO = 0x27;
// A frame of such a start, with the game's mode word and the phase word of its race mode: the course is loaded
// once the mode is the race's or the replay's and its phase is the course intro's or a later one of the race (the
// start gate holds on the intro's last frame, so every race passes it). From then on the game draws the course,
// and the panel is down for good: LOADING never comes back in this start.
// In a start held to the race it never does.
bool Frame(Board& board, u32 mode, u32 phase);
// The race's first step in a start held to the race (the replay scrubber's first step, scrub.cpp Fork): the panel is
// down. False, and nothing changes, in any other start.
bool RaceBegins(Board& board);
// A start held until it is live (Board::until_live): the game has run `done` of the `of` steps it catches up on (the
// meter, which never goes back); and it has caught up, or the race is over: the panel is down. Both false, and
// nothing changes, in any other start.
bool CatchingUp(Board& board, u32 done, u32 of);
bool Live(Board& board);
// A pass of the start gate's wait, `waited_ms` after the wait began. A wait shorter than WAIT_SHOWN_MS shows
// nothing: with every game ready the go is one round trip away, and a panel for a tenth of a second is a flicker.
constexpr u32 WAIT_SHOWN_MS = 250;
bool AtGate(Board& board, u32 waited_ms, u32 ready, u32 of);
// The go, a failed session, the end: nothing is shown.
bool Down(Board& board);
// A practice's countdown, `figure` (3, 2, 1) now; Down when the race runs on.
bool Counting(Board& board, u32 figure);
// A practice's save, `along` thousandths of the way along the rewind's bar (clamped to FULL).
void Saved(Board& board, u32 along);

// The panel's lines: "LOADING"; "WAITING FOR PLAYERS" and, when `of` is not 0, "3/5"; the countdown's figure
// (`ready` holds it). Labels and values only.
std::vector<std::string> Lines(Shows shows, u32 ready, u32 of);

// The game's 24 by 24 font: the disc's file font/fnt_asc_24x24.bin, 1,387 characters of 288 bytes each. A
// character is 24 by 24 pixels of four bits (0 clear to 15 solid), stored as the console's I4 textures are: nine
// blocks of 8 by 8 pixels, row by row, each block's pixels row by row, the left pixel of a pair in a byte's high
// half. The characters are in the order of the game's own table: the space first, punctuation ("/" is the 19th),
// then from the 59th the digits, A to Z and a to z, then kana and the rest. No width is stored: each fills its
// cell as a full-width character does.
constexpr u32 CELL = 24, GLYPH_BYTES = CELL * CELL / 2, GLYPHS = 1387;
constexpr char FONT_FILE[] = "font/fnt_asc_24x24.bin";
class Font
{
public:
  // False when `file` is not the font file by its size: nothing is drawn from it then.
  bool Load(const std::vector<u8>& file);
  bool Loaded() const { return !m_cells.empty(); }
  // The character's cell, 24 rows of 24 values of 0 to 15; a character the panel does not use (anything but A to
  // Z, a to z, 0 to 9, "/" and the space) is the space.
  const std::array<u8, CELL * CELL>& Cell(char character) const;

private:
  std::vector<std::array<u8, CELL * CELL>> m_cells;
};

// The font from the game's files, asked of the core's host (files_disc.cpp; after fzgx::Init, from any thread: the
// host's reader is a plain file read). False when the file is not there or is not the font; `size_read`, when
// given, is what the host read, for the log.
bool LoadFromTheGame(Font* font, std::size_t* size_read = nullptr);

// A picture of four bytes a pixel (red, green, blue, opacity 255), row by row from the top.
struct Picture
{
  u32 width = 0, height = 0;
  std::vector<u8> rgba;
};

// The look, measured on the course name in a course intro (a 16:9 picture of 2,334 by 1,344 pixels of Lightning,
// Loop Cross): a character's cell is 18 of the picture's 448 lines high and 0.933 times as wide on the screen (the
// console's pixels are narrower than high); the next character begins two thirds of a cell on, a space takes a
// whole cell; the letters run from near white at the top of the cell to cyan at its bottom; around them a soft
// grey-blue edge a sixth of a cell wide.
constexpr u32 NAME_LINES = 18, PICTURE_LINES = 448;
constexpr double CELL_ASPECT = 0.933, ADVANCE = 16.0 / 24.0, LINE_PITCH = 1.5;
// The height in pixels of a character's cell in a picture `picture_height` pixels high (at least 8).
u32 CellHeight(u32 picture_height);
// The lines as one picture on black, each centred, a cell `cell_height` pixels high (8 to 512). Empty without a
// loaded font, without lines, or outside that range.
Picture Draw(const Font& font, const std::vector<std::string>& lines, u32 cell_height);

// ---- The look: the venue name's lettering, and the energy meter as the load's bar.

// The large lettering of the course intro: the venue's name there is no text but a picture, one of twelve strips
// of 432 by 36 pixels in texture 0 of bmp_e/bmp_view.tpl.lz (white capitals 24 pixels high with a cyan-grey
// glow). The letters are cut from the strips as they are, with no coordinates written down: in each strip the
// connected regions of near-white solid pixels (opacity 200 or more, every colour 190 or more; at least 6 pixels;
// a region inside another's box is part of it), from left to right, are the letters of that venue's name, and a
// strip whose regions are not as many as its name's letters gives none. Each letter keeps the pixels within 5 of
// it that are nearer to it than to its neighbours: its glow. The twelve names hold A B C D E F G H I L M N O P R
// S T U W Y: every letter of the panel's two lines, no digit and no slash.
constexpr u32 STRIP_WIDTH = 432, STRIP_HEIGHT = 36, STRIPS = 12;
constexpr char LETTERS_FILE[] = "bmp_e/bmp_view.tpl.lz";
class Lettering
{
public:
  // The strips' texture (432 pixels wide, twelve strips high or more). False when no strip gave its letters.
  bool Load(const art::Image& strips);
  bool Has(char letter) const;
  // Whether every character of the line is a letter the strips gave, or a space.
  bool Writes(const std::string& line) const;
  // The line at the strips' own size, 36 pixels high, on black: each letter where it stood in its strip's
  // height, the next beginning one pixel short of the letter's width, a space 9 pixels, as the strips are set.
  // Empty when the line cannot be written.
  art::Image Line(const std::string& line) const;

private:
  struct Letter
  {
    art::Image cut;        // the letter with its glow
    int left = 0, top = 0; // where the cut lies from the letter's own left edge, and from the strip's top
    u32 width = 0;         // the letter's own width, without its glow
  };
  std::array<Letter, 26> m_letters;
};

// The game's energy meter, from bmp_e/bmp_rac.tpl.lz: the frame is texture 23 (136 by 24, a steel edge 4 pixels
// wide), the fill texture 22 (128 by 16, red at the left to magenta at the right), which lies 4 pixels inside the
// frame and is cut off at the right to what it shows.
constexpr char METER_FILE[] = "bmp_e/bmp_rac.tpl.lz";
constexpr u32 METER_FRAME = 23, METER_FILL = 22, METER_EDGE = 4, METER_GAP = 5;
struct Meter
{
  art::Image frame, fill;
  bool Loaded() const { return !frame.Empty() && !fill.Empty(); }
};

// The lettering and the meter from the game's files, asked of the core's host (files_disc.cpp; as
// LoadFromTheGame). Each is left unloaded when its file or its texture is not as known; `problem` then says which.
void LoadArtFromTheGame(Lettering* lettering, Meter* meter, std::string* problem);

// A picture of the game's own size as the game's race display draws it in a picture `picture_height` pixels high,
// on black, solid, times `scale`. Measured on the energy meter of a race picture (2,334 by 1,344, 16:9: its fill
// of 128 by 16 is 350 by 45 pixels): the display is laid out on a screen of 640 by 480 whose pixel is a 480th of
// the picture's height each way, in a 16:9 picture as in a 4:3 one.
// The panel's sizes (the load's meter as large on the screen as the race's own; the words to suit it): the meter
// at the game's size, 136 by 24 of that screen; the lettering at two thirds, capitals 16.
constexpr u32 SCREEN_LINES = 480;
constexpr double METER_SCALE = 1.0, LETTERS_SCALE = 2.0 / 3.0;
Picture ForThePicture(const art::Image& native, u32 picture_height, double scale);

// How far the load is, from what the load itself shows (no total is kept from an earlier load, and nothing by
// course: custom courses come). The game loads by draining a queue of file preloads (main.rel: a ring of 512
// entries of 12 bytes, a count of the entries queued and one of those done). The first drain, queued when the
// race is asked for, is about three quarters of the load whatever the course (403 to 418 of about 536 fields):
// it fills the first 750 thousandths by the bytes of its files done (their sizes from the disc's file table as
// the game holds it). The rest, a few shorter drains and the course's set-up, has no such measure: it adds two
// thousandths a field, up to 990. The last ten are the intro's first frame. The answer never goes back.
class Progress
{
public:
  // The entries' sizes in bytes from entry `from` up to entry `to` of the queue (not a file: 0).
  using Bytes = u64 (*)(void* context, u32 from, u32 to);
  // One field of the load: whether the game's mode is the race's or the replay's, and the queue's two counts.
  u32 Field(bool race_mode, u32 queued, u32 done, Bytes bytes, void* context);
  // One field of a start held to the race, with what Field gave and whether the course is shown (CourseShown): the
  // load fills the first HELD_LOAD thousandths, and what runs unseen after it (the intro's closing fade and the
  // countdown, about 400 fields) a thousandth a field, up to BEFORE_THE_INTRO. Never goes back.
  u32 Held(u32 load, bool course_shown);

private:
  u32 m_first = 0, m_base = 0;  // the first drain: the count queued when it was seen, and done before it
  u64 m_total = 0;              // its files' bytes
  u32 m_after = 0;              // fields since it was done
  u32 m_best = 0;
  u32 m_held_after = 0, m_held_best = 0;  // (Held)
};
constexpr u32 QUEUE_ENTRIES = 512, FIRST_DRAIN = 750, REST_A_FIELD = 2, BEFORE_THE_INTRO = 990, FULL = 1000;
constexpr u32 HELD_LOAD = 600;
}  // namespace fzgx::panel
