// A practice's countdown figure (3, 2, 1) as a picture with its own transparency, for panel.cpp to draw over the
// game's picture: the figure in the launcher's display face (Saira Condensed ExtraBold, from Fonts\ beside the
// runner, leaning 12 degrees about its foot as the launcher's display words do; the system's bold face without it),
// anti-aliased, a flat fill in the text colour (overlay.h: no gradient, no glow), with a soft shadow under it (it
// floats over the picture) and nothing else around it. Drawn once a figure and picture height with GDI+; panel.cpp
// eases it in. Windows only: elsewhere no picture, and panel.cpp writes the figure as
// text in the same face.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "panel_figure.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <objidl.h>
// GDI+'s headers use min and max as the macros the emulator's build turns off
namespace Gdiplus
{
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "fzgx/strip.h"
#include "overlay.h"

namespace fzgx_port
{
namespace
{
// The figure's height (its digits' cap height) as a share of the picture's: about twice a count's capitals.
constexpr float FIGURE_SHARE = 0.046f;
constexpr float CAP_OF_EM = 0.70f;  // Saira's digits stand about this much of the em
constexpr std::uint32_t FILL = overlay::TEXT;
constexpr float SHADOW = 0.55f;     // the shadow's opacity at its densest
constexpr float SHADOW_DROP = 0.035f, SHADOW_BLUR = 0.06f;  // of the em

struct Face
{
  bool tried = false;
  ULONG_PTR library = 0;
  std::unique_ptr<Gdiplus::PrivateFontCollection> file;
  std::unique_ptr<Gdiplus::FontFamily> family;
  INT style = Gdiplus::FontStyleBold;
};
Face s_face;

void LoadFace()
{
  if (s_face.tried)
    return;
  s_face.tried = true;
  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&s_face.library, &input, nullptr) != Gdiplus::Ok)
    return;
  wchar_t program[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program) - 1));
  const std::filesystem::path path =
      std::filesystem::path(program).parent_path() / L"Fonts" / fzgx::strip::DISPLAY_FONT;
  auto file = std::make_unique<Gdiplus::PrivateFontCollection>();
  auto family = std::make_unique<Gdiplus::FontFamily>();
  INT found = 0;
  if (file->AddFontFile(path.c_str()) == Gdiplus::Ok && file->GetFamilies(1, family.get(), &found) == Gdiplus::Ok &&
      found == 1)
  {
    s_face.style = family->IsStyleAvailable(Gdiplus::FontStyleRegular) ? Gdiplus::FontStyleRegular
                                                                         : Gdiplus::FontStyleBold;
    s_face.file = std::move(file);
    s_face.family = std::move(family);
    return;
  }
  s_face.family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
  s_face.style = Gdiplus::FontStyleBold;
}

// How solid the path is at each pixel (0 to 1), drawn anti-aliased at (dx, dy).
std::vector<float> Coverage(const Gdiplus::GraphicsPath& path, int width, int height, float dx, float dy)
{
  std::vector<float> out(static_cast<std::size_t>(width) * height, 0.0f);
  Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
  {
    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.TranslateTransform(dx, dy);
    Gdiplus::SolidBrush ink(Gdiplus::Color(255, 255, 255, 255));
    g.FillPath(&ink, &path);
  }
  Gdiplus::BitmapData data{};
  Gdiplus::Rect all(0, 0, width, height);
  if (bitmap.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
    return out;
  for (int y = 0; y < height; ++y)
  {
    const auto* row = static_cast<const BYTE*>(data.Scan0) + static_cast<std::ptrdiff_t>(y) * data.Stride;
    for (int x = 0; x < width; ++x)
      out[static_cast<std::size_t>(y) * width + x] = row[x * 4 + 3] / 255.0f;
  }
  bitmap.UnlockBits(&data);
  return out;
}

// Three box blurs of `radius` across and down: near enough a Gaussian's softness.
void Blur(std::vector<float>& v, int width, int height, int radius)
{
  if (radius < 1)
    return;
  std::vector<float> tmp(v.size());
  const float share = 1.0f / (2 * radius + 1);
  for (int pass = 0; pass < 3; ++pass)
  {
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
      {
        float sum = 0;
        for (int k = -radius; k <= radius; ++k)
          sum += v[static_cast<std::size_t>(y) * width + std::clamp(x + k, 0, width - 1)];
        tmp[static_cast<std::size_t>(y) * width + x] = sum * share;
      }
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
      {
        float sum = 0;
        for (int k = -radius; k <= radius; ++k)
          sum += tmp[static_cast<std::size_t>(std::clamp(y + k, 0, height - 1)) * width + x];
        v[static_cast<std::size_t>(y) * width + x] = sum * share;
      }
  }
}

float Channel(std::uint32_t rgb, int shift)
{
  return static_cast<float>((rgb >> shift) & 0xFF);
}
}  // namespace

fzgx::panel::Picture FigurePicture(fzgx::u32 figure, fzgx::u32 picture_height)
{
  fzgx::panel::Picture picture;
  LoadFace();
  if (!s_face.family || picture_height < 60 || figure > 9)
    return picture;
  const float cap = picture_height * FIGURE_SHARE, em = cap / CAP_OF_EM;
  const std::wstring text(1, static_cast<wchar_t>(L'0' + figure));
  Gdiplus::GraphicsPath path;
  path.AddString(text.c_str(), -1, s_face.family.get(), s_face.style, em, Gdiplus::PointF(0, 0), nullptr);
  Gdiplus::RectF box;
  path.GetBounds(&box);
  if (box.Width <= 0 || box.Height <= 0)
    return picture;
  // the lean: a shear about the figure's foot, so its top moves right and its foot stays
  Gdiplus::Matrix lean(1, 0, -overlay::LEAN, 1, (box.Y + box.Height) * overlay::LEAN, 0);
  path.Transform(&lean);
  path.GetBounds(&box);
  // room for the shadow's blur and drop on every side
  const int blur = std::max(1, static_cast<int>(std::lround(em * SHADOW_BLUR)));
  const int margin = 3 * blur + static_cast<int>(std::ceil(em * SHADOW_DROP)) + 2;
  const int width = static_cast<int>(std::ceil(box.Width)) + 2 * margin;
  const int height = static_cast<int>(std::ceil(box.Height)) + 2 * margin;
  const float dx = margin - box.X, dy = margin - box.Y;
  const std::vector<float> fill = Coverage(path, width, height, dx, dy);
  std::vector<float> shadow = Coverage(path, width, height, dx, dy + em * SHADOW_DROP);
  Blur(shadow, width, height, blur);
  picture.width = static_cast<fzgx::u32>(width);
  picture.height = static_cast<fzgx::u32>(height);
  picture.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
  const float rgb[3] = {Channel(FILL, 16), Channel(FILL, 8), Channel(FILL, 0)};
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      const std::size_t i = static_cast<std::size_t>(y) * width + x;
      const float a = fill[i], s = shadow[i] * SHADOW * (1 - a);
      const float out = a + s;  // the fill over the shadow (black): straight, not premultiplied
      fzgx::u8* pixel = &picture.rgba[i * 4];
      if (out > 0)
        for (int c = 0; c < 3; ++c)
          pixel[c] = static_cast<fzgx::u8>(std::clamp(rgb[c] * a / out + 0.5f, 0.0f, 255.0f));
      pixel[3] = static_cast<fzgx::u8>(std::clamp(out * 255 + 0.5f, 0.0f, 255.0f));
    }
  }
  return picture;
}
}  // namespace fzgx_port

#else

namespace fzgx_port
{
fzgx::panel::Picture FigurePicture(fzgx::u32, fzgx::u32)
{
  return {};
}
}  // namespace fzgx_port

#endif
