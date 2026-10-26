// The panel over the picture before a race (fzgx/panel.h): LOADING with the game's energy meter under it as the
// load's bar, until the course's first picture; WAITING FOR PLAYERS at the start gate. Drawn by the runner with the
// emulator's own on-screen drawing (its ImGui frame, which goes over the game's picture at every present): a black
// rectangle over the whole window, the words as one texture (the venue name's lettering of the course intro, cut
// from the player's own game files; the game's small font when that cannot be had), and under LOADING the meter's
// frame and its fill, drawn as wide as the load is far. No emulator patch: the present event and the on-screen
// frame are the emulator's own.
//   - Every picture the game hands over while the board shows something gets the panel over it (DrawPanel, from
//     the hook before a present, on the thread that presents).
//   - While the game loads and at the start gate it hands no picture over at all. The game's thread asks for a
//     present of the picture that stands, with the panel over it (PresentPanel: queued, never waited for): every
//     field of the load, at most once in 8 ms, so that the meter moves (PanelField); and at the gate when the core
//     says the panel changed (Host::PanelChanged). A replay held to the race keeps LOADING over the intro's fade
//     and the countdown, which the game draws: there its own pictures carry the panel, and no present is added.
//   - The meter shows the board's progress, never faster than a whole bar in half a second: a load of a second
//     still fills it by eye.
//   - LOADING stays over the two pictures that follow the board's change to nothing (HELD), with the meter full.
//     The game's loop copies a picture out at the top of a pass, before the frame tick that finds the course
//     loaded, and the first such copy after the load is still the start-up picture (seen: each picture after the
//     load held on the screen for 0.8 s, run wp-x: the first is the logos, the four after it are the intro's
//     black). That copy may reach the presenting thread before the tick's change or after it. Whatever was copied
//     out before the change is at most that one picture and one of the intro's first, which are black: two cover
//     both in every order.
//   - Once the board shows nothing and those are past, a picture pays one load of a flag here, and the first such
//     picture gives the panel's textures back.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "VideoCommon/AsyncRequests.h"
#include "VideoCommon/Present.h"

#include "fzgx/core.h"
#include "fzgx/panel.h"
#include "overlay.h"
#include "panel_figure.h"

namespace fzgx_port
{
namespace
{
namespace panel = fzgx::panel;
namespace ov = overlay;
using Clock = std::chrono::steady_clock;

// Everything here belongs to the thread that presents, but s_asked.
panel::Font s_font;
panel::Lettering s_lettering;
panel::Meter s_meter;
bool s_art_asked = false;
// The textures and what they were made for: the words; under them the meter's frame and fill (LOADING) or the
// count (WAITING FOR PLAYERS, when there is one). And those no longer drawn, until the emulator has let go of them.
ImTextureData *s_words = nullptr, *s_frame = nullptr, *s_fill = nullptr, *s_count = nullptr;
fzgx::u32 s_shows = 0, s_ready = 0, s_of = 0, s_height = 0;
std::vector<ImTextureData*> s_old;
// The meter as it is shown: it follows the board's progress, at most a whole bar in half a second.
constexpr float FILL_A_SECOND = 2.0f;
float s_filled = 0;
Clock::time_point s_filled_at{};
// For the log: pictures of the game's that the panel went over, presents made for it, and since when it is up.
fzgx::u32 s_covered = 0, s_presented = 0;
Clock::time_point s_up_since{};
bool s_was_up = false;
// LOADING is held over this many pictures after the board's change to nothing; how many of them are left.
constexpr fzgx::u32 HELD = 2;
fzgx::u32 s_hold = 0;
// The game's thread's: when it last asked for a present of the load's panel.
Clock::time_point s_asked{};
// A practice's countdown figure eases in when it changes (scale and opacity, ease-out): since when it shows.
constexpr float FIGURE_IN_S = 0.16f, FIGURE_FROM_SCALE = 1.18f;
Clock::time_point s_figure_since{};
// A practice's save, marked briefly at the foot of the picture: the saves seen, and since when the last is marked.
constexpr float MARK_IN_S = 0.12f, MARK_OUT_S = 0.6f;
fzgx::u32 s_saves_seen = 0;
Clock::time_point s_marked_at{};
bool s_marking = false;

float EaseOut(float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

float Since(Clock::time_point at)
{
  return std::chrono::duration<float>(Clock::now() - at).count();
}

void LoadArt()
{
  if (s_art_asked)
    return;
  s_art_asked = true;
  std::string problem;
  panel::LoadArtFromTheGame(&s_lettering, &s_meter, &problem);
  std::size_t size = 0;
  if (!panel::LoadFromTheGame(&s_font, &size))
    problem += std::string(problem.empty() ? "" : "; ") + "the small font (" + panel::FONT_FILE + ") is not the file known";
  if (!problem.empty())
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: panel: " + problem + ": what is missing is drawn in the small "
                                             "font, or left out");
}

// A texture that is no longer drawn goes back in two steps: the emulator's drawing destroys its own half at the
// next present, then the rest is freed here.
void Retire(ImTextureData*& texture)
{
  if (!texture)
    return;
  if (texture->Status == ImTextureStatus_OK)
  {
    texture->SetStatus(ImTextureStatus_WantDestroy);
    texture->UnusedFrames = 1;
  }
  s_old.push_back(texture);
  texture = nullptr;
}

void FreeRetired()
{
  std::erase_if(s_old, [](ImTextureData* texture) {
    // (never drawn: the emulator made nothing for it)
    if (texture->Status != ImTextureStatus_Destroyed && texture->Status != ImTextureStatus_WantCreate)
      return false;
    ImGui::UnregisterUserTexture(texture);
    IM_DELETE(texture);
    return true;
  });
}

ImTextureData* TextureOf(const panel::Picture& picture)
{
  if (picture.rgba.empty())
    return nullptr;
  ImTextureData* texture = IM_NEW(ImTextureData)();
  texture->Create(ImTextureFormat_RGBA32, static_cast<int>(picture.width), static_cast<int>(picture.height));
  std::memcpy(texture->Pixels, picture.rgba.data(), picture.rgba.size());
  texture->UseColors = true;
  ImGui::RegisterUserTexture(texture);
  return texture;
}

void MakeTextures(panel::Shows shows, fzgx::u32 ready, fzgx::u32 of, fzgx::u32 height)
{
  Retire(s_words), Retire(s_frame), Retire(s_fill), Retire(s_count);
  s_shows = static_cast<fzgx::u32>(shows), s_ready = ready, s_of = of, s_height = height;
  LoadArt();
  const std::vector<std::string> lines = panel::Lines(shows, ready, of);
  if (lines.empty())
    return;
  // the words in the venue name's lettering; the small font when a letter is missing
  // (the small font beside lettering of capitals 16: its cell two thirds of the course name's)
  const fzgx::u32 cell = std::max<fzgx::u32>(8, panel::CellHeight(height) * 2 / 3);
  // the countdown's figure: the heading face with its own transparency (panel_figure.cpp), eased in
  if (shows == panel::Shows::Countdown)
  {
    s_figure_since = Clock::now();
    if ((s_words = TextureOf(FigurePicture(ready, height))))
      return;
  }
  s_words = s_lettering.Writes(lines[0]) ? TextureOf(panel::ForThePicture(s_lettering.Line(lines[0]), height, panel::LETTERS_SCALE))
                                         : TextureOf(panel::Draw(s_font, {lines[0]}, cell));
  if (shows == panel::Shows::Loading && s_meter.Loaded())
  {
    // (the fill is made as wide as the frame's inside, so that the two meet whatever the rounding)
    s_frame = TextureOf(panel::ForThePicture(s_meter.frame, height, panel::METER_SCALE));
    s_fill = TextureOf(panel::ForThePicture(s_meter.fill, height, panel::METER_SCALE));
  }
  // the count's digits and slash are in no strip: the small font
  if (lines.size() > 1)
    s_count = TextureOf(panel::Draw(s_font, {lines[1]}, cell));
}

void Image(ImDrawList* list, ImTextureData* texture, float left, float top, float share = 1.0f)
{
  list->AddImage(texture->GetTexRef(), ImVec2(left, top), ImVec2(left + texture->Width * share, top + texture->Height),
                 ImVec2(0, 0), ImVec2(share, 1));
}

// The panel into the on-screen frame that the next present draws; `full`: the load is over, the meter is full.
// False: nothing is shown.
bool Draw(panel::Shows shows, bool full)
{
  const panel::Board& board = panel::TheBoard();
  if (shows == panel::Shows::Nothing || ImGui::GetCurrentContext() == nullptr)
    return false;
  const ImVec2 window = ImGui::GetIO().DisplaySize;
  // where the game's picture is in the window (the bars beside a 4:3 picture are not part of it)
  MathUtil::Rectangle<int> picture = g_presenter->GetTargetRectangle();
  if (picture.GetWidth() <= 0 || picture.GetHeight() <= 0)
    picture = MathUtil::Rectangle<int>(0, 0, static_cast<int>(window.x), static_cast<int>(window.y));
  // (the countdown's figure is its one value)
  const fzgx::u32 ready = (shows == panel::Shows::Countdown ? board.figure : board.ready).load(std::memory_order_relaxed);
  const fzgx::u32 of = board.of.load(std::memory_order_relaxed);
  const fzgx::u32 height = static_cast<fzgx::u32>(picture.GetHeight());
  if (static_cast<fzgx::u32>(shows) != s_shows || ready != s_ready || of != s_of || height != s_height)
    MakeTextures(shows, ready, of, height);
  FreeRetired();
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  if (shows != panel::Shows::Countdown)  // (a practice's countdown stands over the game's own picture)
    list->AddRectFilled(ImVec2(0, 0), window, IM_COL32(0, 0, 0, 255));
  const float line = height / static_cast<float>(panel::SCREEN_LINES);  // a pixel of the game's 640 by 480 screen
  const float middle = picture.left + picture.GetWidth() / 2.0f;
  float below = picture.top + picture.GetHeight() / 2.0f;
  if (s_words && shows == panel::Shows::Countdown)
  {
    // over the picture's middle: from a little larger and clear to its size and solid, eased out
    const float eased = EaseOut(Since(s_figure_since) / FIGURE_IN_S);
    const float scale = FIGURE_FROM_SCALE + (1 - FIGURE_FROM_SCALE) * eased;
    const float w = s_words->Width * scale / 2, h = s_words->Height * scale / 2;
    const float y = picture.top + picture.GetHeight() / 2.0f;
    list->AddImage(s_words->GetTexRef(), ImVec2(middle - w, y - h), ImVec2(middle + w, y + h), ImVec2(0, 0),
                   ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(std::lround(eased * 255))));
  }
  else if (s_words)
  {
    const float top = static_cast<float>(picture.top + (picture.GetHeight() - s_words->Height) / 2);
    Image(list, s_words, std::floor(middle - s_words->Width / 2.0f), top);
    below = top + s_words->Height;
  }
  if (s_frame && s_fill)
  {
    // the meter follows the load, never back, and no faster than a whole bar in half a second
    const Clock::time_point now = Clock::now();
    const float target = full ? 1.0f : board.progress.load(std::memory_order_relaxed) / static_cast<float>(panel::FULL);
    const float most = s_filled_at == Clock::time_point{} ? 0.0f
                                                          : std::chrono::duration<float>(now - s_filled_at).count() * FILL_A_SECOND;
    s_filled = full ? 1.0f : std::max(s_filled, std::min(target, s_filled + most));
    s_filled_at = now;
    const float left = std::floor(middle - s_frame->Width / 2.0f), top = std::floor(below + panel::METER_GAP * line);
    Image(list, s_frame, left, top);
    Image(list, s_fill, left + (s_frame->Width - s_fill->Width) / 2, top + (s_frame->Height - s_fill->Height) / 2, s_filled);
  }
  if (s_count)
    Image(list, s_count, std::floor(middle - s_count->Width / 2.0f), std::floor(below + panel::METER_GAP * line));
  if (!s_was_up)
    s_up_since = Clock::now();
  s_was_up = true;
  return true;
}

// The first picture without the panel: its textures go back, and the log says what it covered.
void TakeDown()
{
  s_was_up = false;
  Retire(s_words), Retire(s_frame), Retire(s_fill), Retire(s_count);
  s_shows = 0;
  s_filled = 0;
  s_filled_at = {};
  char text[240];
  std::snprintf(text, sizeof(text), "fzgx: panel: down %.2f s after it came up; it was over %u pictures of the game's, and "
                                    "%u presents were made for it while the game drew none",
                std::chrono::duration<double>(Clock::now() - s_up_since).count(), s_covered, s_presented);
  s_core_host.Log(fzgx::LogLevel::Notice, text);
  s_covered = s_presented = 0;
}
// A practice's save, marked at the foot of the picture where the rewind's bar would show it: a short upright tick in
// the accent with a soft shadow, in over MARK_IN_S and out over MARK_OUT_S. Nothing else.
void DrawSaveMark()
{
  const panel::Board& board = panel::TheBoard();
  const fzgx::u32 saves = board.saves.load(std::memory_order_relaxed);
  if (saves != s_saves_seen)
  {
    s_saves_seen = saves;
    s_marked_at = Clock::now();
    s_marking = true;
  }
  if (!s_marking || ImGui::GetCurrentContext() == nullptr || !g_presenter)
    return;
  const float since = Since(s_marked_at);
  if (since > MARK_IN_S + MARK_OUT_S)
  {
    s_marking = false;
    return;
  }
  const float opacity = since < MARK_IN_S ? EaseOut(since / MARK_IN_S) : 1 - EaseOut((since - MARK_IN_S) / MARK_OUT_S);
  MathUtil::Rectangle<int> picture = g_presenter->GetTargetRectangle();
  if (picture.GetWidth() <= 0 || picture.GetHeight() <= 0)
    return;
  // the bar's track: its margins and readouts at either end (replay_bar_paint.cpp: 16 + 140 before it, 104 + 16 + 16
  // after it, in 1/96 inch), then the save's place along it
#ifdef _WIN32
  const float dpi = GetDpiForSystem() / 96.0f;
#else
  const float dpi = 1.0f;
#endif
  const float left = picture.left + 156 * dpi, right = picture.right - 136 * dpi;
  const float along = board.saved_along.load(std::memory_order_relaxed) / static_cast<float>(panel::FULL);
  const float x = right > left ? left + (right - left) * along : picture.left + picture.GetWidth() / 2.0f;
  const float unit = std::max(1.0f, picture.GetHeight() / 480.0f);  // a pixel of the game's 640 by 480 screen
  const float half = 1.0f * unit, tall = 7.0f * unit, foot = picture.bottom - 10.0f * unit;
  ImDrawList* list = ImGui::GetForegroundDrawList();
  const int a = static_cast<int>(std::lround(opacity * 255));
  list->AddRectFilled(ImVec2(x - half - unit, foot - tall - unit * 0.5f), ImVec2(x + half + unit, foot + unit * 1.5f),
                      IM_COL32(0, 0, 0, a * 2 / 5), half + unit);
  list->AddRectFilled(ImVec2(x - half, foot - tall), ImVec2(x + half, foot), ov::Colour(ov::ACCENT, opacity), half);
}
}  // namespace

void DrawPanel()
{
  DrawSaveMark();
  panel::Shows shows = panel::TheBoard().Now();
  if (shows == panel::Shows::Nothing && !s_was_up && s_old.empty())
    return;
  // LOADING over the pictures that were copied out before the course was found loaded (HELD, above)
  bool held = false;
  if (shows == panel::Shows::Loading)
    s_hold = HELD;
  else if (shows == panel::Shows::Nothing && s_hold > 0)
  {
    --s_hold;
    shows = panel::Shows::Loading;
    held = true;
  }
  else
    s_hold = 0;
  if (Draw(shows, held))
  {
    ++s_covered;
    return;
  }
  if (s_was_up)
    TakeDown();
  if (ImGui::GetCurrentContext() != nullptr)
    FreeRetired();
}

void PanelField()
{
  // (in a replay held to the race the game draws again under LOADING from the intro on: its own pictures carry it)
  if (!fzgx::LoadField())
    return;
  const Clock::time_point now = Clock::now();
  if (now - s_asked < std::chrono::milliseconds(8))
    return;
  s_asked = now;
  PresentPanel();
}

void PresentPanel()
{
  // not waited for: the game's thread is in the start gate's wait and goes on with it
  AsyncRequests::GetInstance()->PushEvent([] {
    // (the go may have come meanwhile: then the game's own next picture is the one to show)
    if (!g_presenter || !Draw(panel::TheBoard().Now(), false))
      return;
    ++s_presented;
    g_presenter->Present();
  });
}
}  // namespace fzgx_port
