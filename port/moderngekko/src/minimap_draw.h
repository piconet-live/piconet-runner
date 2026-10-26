// The course map's place and drawing (minimap.cpp keeps what the core told it and calls these before a present):
// ImGui only, no game and no emulator, so a bench can build it alone.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <imgui.h>

#include "fzgx/host.h"
#include "overlay.h"

namespace fzgx_port::minimap_draw
{
// The game's speed figure (main.rel fn_1_CADC4: "%4d" in font 15, cells 24 by 32, from x 484, y 424 of the HUD's
// 640 by 480): the top of its box, as a share of the picture's height. At 4:3 the HUD fills the picture; at 16:9 the
// game keeps the HUD's height and moves its right side out (measured on the runner's 2334x1344 picture: the figure's
// box from 0.39 of the height in from the right edge), so the top is the same share at both.
constexpr float SPEED_TOP = 424.0f / 480.0f;
// What is kept clear between the map's square and the speed figure's box, as a share of the picture's height.
constexpr float SPEED_GAP = 0.04f;

// The road's look, the game's own map's: its picture (C??_MAP's colour texture, CMPR) is white road, shown through
// its intensity mask (I4) at level 11 of 15 over the road on every course's texture file (stNN.tpl), drawn as a
// sprite of colour 0xFFFFFFFF (main.rel .rodata 0x32E4, fn_1_7926C) and opacity 1 (the sprite template, .rodata
// 0x26F8 + 0x2C): white at 187 of 255.
constexpr std::uint32_t ROAD = 0xFFFFFF;
constexpr float ROAD_OPACITY = 187.0f / 255.0f;

// The square the road's -1 to 1 fills, on the game's picture (`picture`, in the display's pixels): in the game's map
// corner (its middle 0.19 of the height in from the right edge, 0.71 down), 0.23 of the height across (two thirds of
// the game's own map picture), at most a fifth of a narrow picture's width, lifted when it would come nearer the
// speed figure than SPEED_GAP; on whole pixels (its picture is drawn pixel for pixel).
overlay::Box Place(const overlay::Box& picture);

// The road's silhouette and the start mark over it as coverage, `side` by `side` pixels each (row by row from the
// top, 0 to 255): every triangle of `course` at its corners' `points` (x, y in -1 to 1; not a number: behind the
// eye, the triangle left out) filled, then the mark's triangles at `mark` over it (a sample is the mark's or the
// road's, not both), at `samples` by `samples` a pixel (their edges anti-aliased, the same for both). One mask for
// the road, as the game's picture: a road over itself is the same white (a gap cut where the road crosses itself
// was tried and dropped: the preview models lay strips over each other along the same road, so their edges cut the
// road itself; the core gives no depth to tell).
struct Coverage
{
  std::vector<std::uint8_t> road, mark;
};
Coverage Silhouette(const fzgx::MinimapCourse& course, const std::vector<float>& points,
                    const std::vector<float>& mark, int side, int samples);

// The view's turn and tilt in degrees (fzgx/minimap.h ViewTurn; both 0: the game's camera).
struct View
{
  float angle = 0, tilt = 0;
};

// The road's picture, the start mark in it: made once for a course and a size (on the CPU at 4x4 samples: a few
// milliseconds), then one textured square a frame; made again at 2x2 samples every frame the view's angle or tilt
// changes (it turns, or eases back), and at 4x4 once it is back. The texture goes back through ImGui's own steps
// when it is replaced.
struct RoadPicture
{
  std::shared_ptr<const fzgx::MinimapCourse> course;  // (held: the key cannot be a freed course's address)
  int side = 0;
  View view;         // the view its pixels are at
  float scale = 1;   // the fit its pixels are drawn at (fzgx/minimap.h FitScale): the dots take it too
  ImTextureData* texture = nullptr;
  std::vector<ImTextureData*> old;  // (given back, waiting for the emulator's drawing to destroy its half)
};

// The map in `square`: the road's picture in the game's map colour at its opacity, the start mark in it in the
// game's yellow, opaque; then the dots, sized by the square; all scaled to fit the square in a turned or tilted view.
// `alpha` is the fade, `view` the view's turn and tilt.
void Draw(ImDrawList* list, RoadPicture& road, const std::shared_ptr<const fzgx::MinimapCourse>& course,
          const fzgx::MinimapShow& show, const overlay::Box& square, float alpha, View view);
}  // namespace fzgx_port::minimap_draw
