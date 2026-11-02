// "Show inputs" (the launch file's "showInputs", off by default): m-overlay's default skin (bkacjios/m-overlay, MIT:
// art/inputs/LICENSE.txt; only its art and layout) over the picture, showing the local player's pad, modest, left of
// the course map at the picture's bottom. Its order, places and colours are the skin's (fzgx/inputs.h).
//   - The core tells the pad on the game's thread when it changed (fzgx/host.h InputsShow; it only reads PADStatus);
//     it is drawn before every present with the other overlays (DrawOverlays), on the emulator's on-screen drawing, so
//     it stands over whichever picture the presenter shows, Aurora's too. Nothing here reads or writes the game.
//   - The pictures are made at the size they are drawn (one texture for all but the gates).
//   - ImGui has no stencil. The skin cuts the stick's shape out of the gate with one; here each gate is a texture of
//     its own whose alpha is cut by the mask under the stick's map, again only when that stick moved. A leaning stick
//     is a grid of quads on the same projective map (ImGui's triangles alone would bend it along their seam).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>  // (ImGui::RegisterUserTexture, as panel.cpp)

#include "Common/Image.h"
#include "fzgx/inputs.h"
#include "overlay.h"

namespace fzgx_port
{
namespace
{
namespace ov = overlay;
namespace in = fzgx::inputs;

struct ArtFile
{
  const char* name;
  const unsigned char* bytes;
  std::size_t size;
};
#include "inputs_art.g.inc"

constexpr float WIDTH_OF_HEIGHT = 0.28f;  // the skin's width, of the picture's height
constexpr int GRID = 6;                   // a leaning stick's quads, each way

struct Alpha
{
  int w = 0, h = 0;
  std::vector<std::uint8_t> a;
};

// One picture's coverage (the art is white, tinted when drawn), read once.
const Alpha& Original(const std::string& name)
{
  static std::map<std::string, Alpha> read;
  if (const auto it = read.find(name); it != read.end())
    return it->second;
  Alpha& out = read[name];
  for (const ArtFile& file : ART)
  {
    if (name != file.name)
      continue;
    Common::UniqueBuffer<u8> rgba;
    u32 w = 0, h = 0;
    if (!Common::LoadPNG(std::span<const u8>(file.bytes, file.size), &rgba, &w, &h))
      break;
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.a.resize(std::size_t(w) * h);
    for (std::size_t i = 0; i < out.a.size(); ++i)
      out.a[i] = rgba[i * 4 + 3];
    break;
  }
  return out;
}

// Averaged down (or across) to the size it is drawn.
Alpha Sized(const Alpha& from, int w, int h)
{
  Alpha to{w, h, std::vector<std::uint8_t>(std::size_t(w) * h, 0)};
  if (from.w == 0 || from.h == 0)
    return to;
  for (int y = 0; y < h; ++y)
  {
    const int y0 = y * from.h / h, y1 = std::max(y0 + 1, (y + 1) * from.h / h);
    for (int x = 0; x < w; ++x)
    {
      const int x0 = x * from.w / w, x1 = std::max(x0 + 1, (x + 1) * from.w / w);
      int sum = 0;
      for (int j = y0; j < y1; ++j)
        for (int i = x0; i < x1; ++i)
          sum += from.a[std::size_t(j) * from.w + i];
      to.a[std::size_t(y) * w + x] = static_cast<std::uint8_t>(sum / ((x1 - x0) * (y1 - y0)));
    }
  }
  return to;
}

ImTextureData* NewTexture(int w, int h)
{
  ImTextureData* texture = IM_NEW(ImTextureData)();
  texture->Create(ImTextureFormat_RGBA32, w, h);
  std::fill_n(texture->Pixels, std::size_t(w) * h * 4, static_cast<unsigned char>(255));
  texture->UseColors = true;
  return texture;
}

void SetAlpha(ImTextureData* texture, int x, int y, const Alpha& alpha)
{
  for (int j = 0; j < alpha.h; ++j)
    for (int i = 0; i < alpha.w; ++i)
      texture->Pixels[(std::size_t(y + j) * texture->Width + x + i) * 4 + 3] = alpha.a[std::size_t(j) * alpha.w + i];
}

struct Gate
{
  ImTextureData* texture = nullptr;
  Alpha base, mask;
  std::int8_t x = 0, y = 0;
  bool cut = false;
};

struct Cell
{
  ImVec2 uv0, uv1;
};

struct Skin
{
  int side = 0;  // a cell's pixels, the skin's 128
  ImTextureData* atlas = nullptr;
  std::map<std::string, Cell> cells;
  std::array<Gate, 2> gates;
  std::vector<ImTextureData*> old;
};

struct Shown
{
  std::mutex lock;
  fzgx::InputsShow pad;
  ov::Eased fade;
};
Shown s_shown;
Skin s_skin;  // (the drawing thread's alone)

void Retire(ImTextureData*& texture)
{
  if (!texture)
    return;
  if (texture->Status == ImTextureStatus_OK)
  {
    texture->SetStatus(ImTextureStatus_WantDestroy);
    texture->UnusedFrames = 1;
  }
  s_skin.old.push_back(texture);
  texture = nullptr;
}

void FreeRetired()
{
  std::erase_if(s_skin.old, [](ImTextureData* texture) {
    if (texture->Status != ImTextureStatus_Destroyed && texture->Status != ImTextureStatus_WantCreate)
      return false;
    ImGui::UnregisterUserTexture(texture);
    IM_DELETE(texture);
    return true;
  });
}

void MakeSkin(int side)
{
  Retire(s_skin.atlas);
  for (Gate& gate : s_skin.gates)
    Retire(gate.texture);
  s_skin.side = side;
  s_skin.cells.clear();

  std::vector<std::string> names{"joystick", "c-stick", "d-pad-gate"};
  for (const in::DPadPress& press : in::DPAD)
    names.emplace_back(press.art);
  for (const in::Button& button : in::BUTTONS)
  {
    names.push_back(std::string(button.art) + "-outline");
    names.push_back(std::string(button.art) + "-pressed");
  }
  names.emplace_back("analog-outline");  // (116 by 24 in the skin's units)

  constexpr int ACROSS = 5, GUTTER = 2;
  const int pitch = side + GUTTER;
  const int rows = (static_cast<int>(names.size()) + ACROSS - 1) / ACROSS;
  ImTextureData* atlas = NewTexture(ACROSS * pitch, rows * pitch);
  std::fill_n(atlas->Pixels, std::size_t(atlas->Width) * atlas->Height * 4, static_cast<unsigned char>(255));
  for (int i = 0; i < atlas->Width * atlas->Height; ++i)
    atlas->Pixels[std::size_t(i) * 4 + 3] = 0;
  for (std::size_t n = 0; n < names.size(); ++n)
  {
    const bool bar = names[n] == "analog-outline";
    const int w = bar ? side * 116 / 128 : side, h = bar ? std::max(1, side * 24 / 128) : side;
    const int x = static_cast<int>(n) % ACROSS * pitch, y = static_cast<int>(n) / ACROSS * pitch;
    SetAlpha(atlas, x, y, Sized(Original(names[n]), w, h));
    const float W = static_cast<float>(atlas->Width), H = static_cast<float>(atlas->Height);
    s_skin.cells[names[n]] = {ImVec2(x / W, y / H), ImVec2((x + w) / W, (y + h) / H)};
  }
  ImGui::RegisterUserTexture(atlas);
  s_skin.atlas = atlas;

  const std::array<const char*, 2> gates{"joystick-gate", "c-stick-gate"};
  const std::array<const char*, 2> masks{"joystick-mask", "c-stick-mask"};
  for (std::size_t k = 0; k < 2; ++k)
  {
    Gate& gate = s_skin.gates[k];
    gate.base = Sized(Original(gates[k]), side, side);
    gate.mask = Sized(Original(masks[k]), side, side);
    gate.texture = NewTexture(side, side);
    SetAlpha(gate.texture, 0, 0, gate.base);
    ImGui::RegisterUserTexture(gate.texture);
    gate.cut = false;
  }
}

// The gate with the stick's shape cut out (the skin's stencil: any of the mask under the map takes the gate's pixel).
void CutGate(Gate& gate, const in::StickPlace& place, const std::array<in::Point, 4>& quad, std::int8_t x, std::int8_t y)
{
  if (gate.cut && gate.x == x && gate.y == y)
    return;
  gate.cut = true;
  gate.x = x;
  gate.y = y;
  const int side = s_skin.side;
  const float k = side / in::CELL;
  const in::Projective map(quad);
  unsigned char* pixels = gate.texture->Pixels;
  for (int j = 0; j < side; ++j)
  {
    for (int i = 0; i < side; ++i)
    {
      const std::size_t at = std::size_t(j) * side + i;
      std::uint8_t a = gate.base.a[at];
      if (a != 0)
      {
        const in::Point uv = map.Back(place.gate_x + (i + 0.5f) / k, place.gate_y + (j + 0.5f) / k);
        if (uv.x >= 0 && uv.x < 1 && uv.y >= 0 && uv.y < 1 &&
            gate.mask.a[std::size_t(uv.y * side) * side + static_cast<std::size_t>(uv.x * side)] != 0)
          a = 0;
      }
      pixels[at * 4 + 3] = a;
    }
  }
  ImTextureData* t = gate.texture;
  if (t->Status != ImTextureStatus_OK && t->Status != ImTextureStatus_WantUpdates)
    return;  // (not made yet: it is made from these pixels)
  const ImTextureRect all{0, 0, static_cast<unsigned short>(side), static_cast<unsigned short>(side)};
  t->Updates.clear();
  t->Updates.push_back(all);
  t->UpdateRect = all;
  t->SetStatus(ImTextureStatus_WantUpdates);
}
}  // namespace

void ShowInputs(const fzgx::InputsShow& show)
{
  std::lock_guard<std::mutex> hold(s_shown.lock);
  if (show.on)
    s_shown.pad = show;
  s_shown.fade.Send(show.on ? 1.0f : 0.0f, ov::Eased::Clock::now());
}

void DrawInputs(float right, float bottom, float picture_height)
{
  fzgx::InputsShow pad;
  float alpha;
  {
    std::lock_guard<std::mutex> hold(s_shown.lock);
    alpha = s_shown.fade.At(ov::Eased::Clock::now());
    pad = s_shown.pad;
  }
  if (alpha <= 0.004f)
    return;
  const int side = std::max(16, static_cast<int>(std::lround(WIDTH_OF_HEIGHT * picture_height / in::WIDTH * in::CELL)));
  FreeRetired();
  if (side != s_skin.side)
    MakeSkin(side);
  const float k = side / in::CELL;
  const float ox = std::round(right - in::WIDTH * k), oy = std::round(bottom - in::HEIGHT * k);
  const auto at = [&](float x, float y) { return ImVec2(ox + x * k, oy + y * k); };
  const auto tint = [&](std::uint32_t rgb) { return ov::Colour(rgb, alpha); };
  ImDrawList* list = ImGui::GetBackgroundDrawList();

  const auto picture = [&](const std::string& name, float x, float y, std::uint32_t rgb, float w = in::CELL,
                           float h = in::CELL) {
    const Cell& cell = s_skin.cells[name];
    list->AddImage(s_skin.atlas->GetTexRef(), at(x, y), at(x + w, y + h), cell.uv0, cell.uv1, tint(rgb));
  };
  const auto stick = [&](std::size_t which, const in::StickPlace& place, in::Point pos, std::int8_t x, std::int8_t y,
                         const char* art, std::uint32_t rgb) {
    const auto quad = in::Quad(place, pos);
    Gate& gate = s_skin.gates[which];
    CutGate(gate, place, quad, x, y);
    list->AddImage(gate.texture->GetTexRef(), at(place.gate_x, place.gate_y),
                   at(place.gate_x + in::CELL, place.gate_y + in::CELL), ImVec2(0, 0), ImVec2(1, 1), tint(rgb));
    const in::Projective map(quad);
    const Cell& cell = s_skin.cells[art];
    const auto corner = [&](int i, int j) {
      const in::Point p = map.At(i / static_cast<float>(GRID), j / static_cast<float>(GRID));
      return at(p.x, p.y);
    };
    const auto uv = [&](int i, int j) {
      return ImVec2(cell.uv0.x + (cell.uv1.x - cell.uv0.x) * i / GRID, cell.uv0.y + (cell.uv1.y - cell.uv0.y) * j / GRID);
    };
    for (int j = 0; j < GRID; ++j)
      for (int i = 0; i < GRID; ++i)
        list->AddImageQuad(s_skin.atlas->GetTexRef(), corner(i, j), corner(i + 1, j), corner(i + 1, j + 1),
                           corner(i, j + 1), uv(i, j), uv(i + 1, j), uv(i + 1, j + 1), uv(i, j + 1), tint(rgb));
  };
  stick(0, in::STICK, in::Translate(pad.stick_x, pad.stick_y, in::STICK_RANGE), pad.stick_x, pad.stick_y, "joystick", 0xFFFFFF);
  stick(1, in::CSTICK, in::Translate(pad.cstick_x, pad.cstick_y, in::CSTICK_RANGE), pad.cstick_x, pad.cstick_y, "c-stick",
        in::CSTICK_COLOUR);

  // L and R: the outline, the press's mark, the bar (the skin's stencil is the outline's rounded inside)
  const ImU32 white = tint(0xFFFFFF);
  const float round = 6 * k;
  const bool held_l = pad.buttons & in::PAD_L, held_r = pad.buttons & in::PAD_R;
  const float l = in::Trigger(pad.l, held_l), r = in::Trigger(pad.r, held_r);
  picture("analog-outline", 24 + 6, 14, 0xFFFFFF, 116, 24);
  list->AddLine(at(24 + 14 + 88, 20), at(24 + 14 + 88, 32), white, 4 * k);
  if (held_l)
    list->AddRectFilled(at(24 + 14 + 88, 20), at(24 + 14 + 100, 32), white, round, ImDrawFlags_RoundCornersRight);
  if (l > 0)
    list->AddRectFilled(at(24 + 14, 20), at(24 + 14 + 88 * l, 32), white, round, ImDrawFlags_RoundCornersLeft);
  picture("analog-outline", 48 + 128 + 6, 14, 0xFFFFFF, 116, 24);
  list->AddLine(at(48 + 128 + 14 + 12, 20), at(48 + 128 + 14 + 12, 32), white, 4 * k);
  if (held_r)
    list->AddRectFilled(at(48 + 128 + 14, 20), at(48 + 128 + 14 + 12, 32), white, round, ImDrawFlags_RoundCornersLeft);
  if (r > 0)
    list->AddRectFilled(at(48 + 128 + 14 + 12 + 88 * (1 - r), 20), at(48 + 128 + 14 + 100, 32), white, round,
                        ImDrawFlags_RoundCornersRight);

  picture("d-pad-gate", in::DPAD_X, in::DPAD_Y, 0xFFFFFF);
  for (const in::DPadPress& press : in::DPAD)
    if (pad.buttons & press.bit)
      picture(press.art, in::DPAD_X, in::DPAD_Y, 0xFFFFFF);
  for (const in::Button& button : in::BUTTONS)
    picture(std::string(button.art) + (pad.buttons & button.bit ? "-pressed" : "-outline"), button.x, button.y, button.colour);
}
}  // namespace fzgx_port
