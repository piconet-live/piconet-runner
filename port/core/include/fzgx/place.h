// Where the game's window was when it was last open: its place and size, and whether it was maximised or in full
// screen. The runner's adapter keeps it in the player's data folder when the window changes or closes and puts the
// window back there at the next start (port/moderngekko/src/place.cpp); every race starts the game anew. Here: the
// file's text, and where a kept window goes on the monitors that are connected now, as plain numbers with no window
// in them, so that both are tested against made-up monitor layouts.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fzgx::place
{
// A rectangle of the desktop, in pixels: a monitor to the left of the main one has negative numbers.
struct Rect
{
  int left = 0, top = 0, right = 0, bottom = 0;
  int Width() const { return right - left; }
  int Height() const { return bottom - top; }
  bool operator==(const Rect&) const = default;
};

struct Kept
{
  Rect window;             // the window as it is when neither maximised nor in full screen
  bool maximized = false;  // it was maximised (in full screen: it was before it went there)
  bool fullscreen = false;
  bool operator==(const Kept&) const = default;
};

constexpr char FILE_NAME[] = "window-place.txt";
// A window is never kept smaller or larger than these (a file that says so is no window's).
constexpr int LEAST_SIDE = 64, MOST_SIDE = 32768, MOST_FROM_ORIGIN = 1 << 20;

// The file's text: one line, "fzgx-window 1 LEFT TOP RIGHT BOTTOM MAXIMIZED FULLSCREEN".
std::string Text(const Kept& kept);
// The place a text holds, or none: another first word or version, a missing or extra field, a number that is none
// or out of range, a rectangle that is no window's size, a flag that is not 0 or 1.
std::optional<Kept> Read(std::string_view text);

// Whether a kept window can be put back on these monitors (each monitor's work area): enough of its top strip, the
// part a player takes it by, must be on one of them: `reach` pixels of its width (the whole width of a narrower
// window) and the strip's upper half. A window on a monitor that is no longer connected, or moved off every
// screen, is not put back: the game then opens at its usual place.
bool OnAScreen(const Rect& window, const std::vector<Rect>& work_areas, int strip_height, int reach = 160);

// The place kept in the folder `folder` (the game's user folder, by its UTF-8 path), with the file's text as it
// stands; none: no file, one that cannot be read or is longer than a place's line, a text that is no place.
std::optional<Kept> ReadKept(const std::string& folder);
// Keeps a place there: written beside the file and moved over it, so the file is whole or as it was. False: it
// could not be written (the game goes on; the window opens at its last kept place, or its usual one).
bool WriteKept(const std::string& folder, const Kept& kept);

// The window as the system says it is at this moment.
struct Seen
{
  Rect rect;                // its rectangle
  bool plain = false;       // neither maximised, minimised nor in full screen: `rect` is then the window's own place
  bool maximized = false;   // maximised, or minimised from maximised; in full screen: it was before it went there
  bool fullscreen = false;
};

// What of the window's place is kept, and when: the runner's window tells it what it sees and asks what to write.
// - A test game's window keeps nothing (`keeps` false).
// - The window's own place is its rectangle while it is plain: a maximised window and one in full screen keep the
//   place they had before.
// - While the window is dragged its place changes with every move of the mouse: written when the drag ends.
// - A window being put back where it was (BeginRestore) is first moved, and only later maximised and taken to full
//   screen, by messages that wait until it is shown: until they have been acted on (Restored) nothing is written,
//   or the place of the moment, neither maximised nor in full screen, would replace the one just read.
// - A place equal to the one last read or written is not written again; one whose write failed is offered again.
class Tracker
{
public:
  Tracker() = default;  // keeps nothing
  explicit Tracker(bool keeps) : m_keeps(keeps) {}
  bool Keeps() const { return m_keeps; }
  void BeginRestore(const Kept& kept);
  void Restored() { m_restoring = false; }
  bool Restoring() const { return m_restoring; }
  void Dragging(bool on) { m_dragging = on; }
  // The window changed, or is about to close: what it is now.
  void Note(const Seen& seen);
  // The place to write now; none: nothing new, nothing known yet, a drag or a restore under way, nothing kept.
  std::optional<Kept> ToWrite() const;
  // It was written.
  void Written(const Kept& kept) { m_kept = kept; }

private:
  bool m_keeps = false, m_have = false, m_restoring = false, m_dragging = false;
  Kept m_now;                  // the window's own place, and its two states
  std::optional<Kept> m_kept;  // as last read or written
};
}  // namespace fzgx::place
