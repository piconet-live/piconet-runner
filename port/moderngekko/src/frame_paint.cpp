// The game window's strip, drawn: the launcher's title bar (fzgx/strip.h holds its sizes, overlay.h its colours and
// type), with the name the launch file gives at the left in the display face, leaning, a test game's title after it
// in the reading face, the frame rate when the player asked for it (figures on one pitch), and the four window
// buttons as the launcher's line drawings. A button lights under the pointer (one tone up; red behind close) and
// goes out again in 120 ms, eased (the design language's quick motion). Drawn with GDI+ into a picture of its own
// and copied to the window in one go, so the strip never flickers. The fonts are the launcher's files, beside the
// runner in Fonts\; without them the system's font is used, and nothing else changes.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32

#include "frame.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <objidl.h>
// GDI+'s headers use min and max as the macros the emulator's build turns off
namespace Gdiplus
{
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "overlay.h"

namespace fzgx_port::frame
{
namespace
{
namespace strip = fzgx::strip;
namespace ov = fzgx_port::overlay;
using strip::Part;

// One font: a file of the launcher's, read into a collection of its own (the family is then whatever the file
// holds, asked of the file and not by a name that could silently give another font), or the system's font.
struct Face
{
  std::unique_ptr<Gdiplus::PrivateFontCollection> file;
  std::unique_ptr<Gdiplus::FontFamily> family;
  INT style = Gdiplus::FontStyleRegular;
};

// How lit each button is, 0 to 1, easing to where it is sent (ease-out, cubic), in the order of strip::Part.
constexpr ULONGLONG FADE_MS = 120;
constexpr UINT_PTR FADE_TIMER = 0x4653;  // while a light eases (the game window's other timers: frame.cpp)
struct Fade
{
  float from = 0, to = 0;
  ULONGLONG since = 0;
  float At(ULONGLONG now) const
  {
    const float p = std::clamp(static_cast<float>(now - since) / FADE_MS, 0.0f, 1.0f);
    const float q = 1 - p;
    return from + (to - from) * (1 - q * q * q);
  }
  void Send(float target, ULONGLONG now)
  {
    if (target == to)
      return;
    from = At(now);
    to = target;
    since = now;
  }
};

struct Look
{
  bool loaded = false;
  ULONG_PTR library = 0;
  Face name, figure, text;
  std::vector<BYTE> name_pixels;                // the name's picture, in the drawing library's byte order
  std::unique_ptr<Gdiplus::Bitmap> name_picture;  // over those bytes
  Fade fullscreen, minimize, maximize, close;     // the buttons' light
  bool fading = false;                            // a button's light is still easing
};
Look s_look;

std::filesystem::path FontFolder()
{
  wchar_t program[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program) - 1));
  return std::filesystem::path(program).parent_path() / L"Fonts";
}

// The file's own style where it has one (a bold file holds no regular style), bold or regular in the system's font.
void Load(Face* face, const char* file_name, bool bold_in_the_systems_font)
{
  auto file = std::make_unique<Gdiplus::PrivateFontCollection>();
  auto family = std::make_unique<Gdiplus::FontFamily>();
  INT found = 0;
  if (file->AddFontFile((FontFolder() / file_name).c_str()) == Gdiplus::Ok &&
      file->GetFamilies(1, family.get(), &found) == Gdiplus::Ok && found == 1)
  {
    face->style = family->IsStyleAvailable(Gdiplus::FontStyleRegular) ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold;
    face->file = std::move(file);
    face->family = std::move(family);
    return;
  }
  face->family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
  face->style = bold_in_the_systems_font ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
}

Gdiplus::Color Colour(std::uint32_t rgb, float opacity = 1.0f)
{
  return Gdiplus::Color(static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255)),
                        static_cast<BYTE>(rgb >> 16), static_cast<BYTE>(rgb >> 8), static_cast<BYTE>(rgb));
}

// A colour part of the way from `a` to `b`.
std::uint32_t Between(std::uint32_t a, std::uint32_t b, float part)
{
  std::uint32_t mixed = 0;
  for (int shift : {16, 8, 0})
  {
    const float from = static_cast<float>((a >> shift) & 0xFF), to = static_cast<float>((b >> shift) & 0xFF);
    mixed |= static_cast<std::uint32_t>(std::lround(from + (to - from) * part)) << shift;
  }
  return mixed;
}

Fade* FadeOf(Part part)
{
  switch (part)
  {
  case Part::Fullscreen: return &s_look.fullscreen;
  case Part::Minimize: return &s_look.minimize;
  case Part::Maximize: return &s_look.maximize;
  default: return &s_look.close;
  }
}

// A button's line drawing: the launcher's paths (MainWindow.axaml), on a square of strip::DRAWING units whose
// corner is (x, y); `unit` is one of them in pixels. Full screen has no drawing in the launcher: four corners.
void DrawLines(Gdiplus::Graphics& g, const Gdiplus::Pen& pen, Part part, bool maximized, float x, float y, float unit)
{
  const auto at = [&](float px, float py) { return Gdiplus::PointF(x + px * unit, y + py * unit); };
  const auto lines = [&](std::initializer_list<Gdiplus::PointF> points) {
    g.DrawLines(&pen, points.begin(), static_cast<INT>(points.size()));
  };
  switch (part)
  {
  case Part::Minimize:
    lines({at(0, 5.5f), at(10, 5.5f)});
    break;
  case Part::Maximize:
    if (maximized)
    {
      lines({at(2.5f, 2.5f), at(2.5f, 0.5f), at(9.5f, 0.5f), at(9.5f, 7.5f), at(7.5f, 7.5f)});
      lines({at(0.5f, 2.5f), at(7.5f, 2.5f), at(7.5f, 9.5f), at(0.5f, 9.5f), at(0.5f, 2.5f)});
    }
    else
    {
      lines({at(0.5f, 0.5f), at(9.5f, 0.5f), at(9.5f, 9.5f), at(0.5f, 9.5f), at(0.5f, 0.5f)});
    }
    break;
  case Part::Close:
    lines({at(0.5f, 0.5f), at(9.5f, 9.5f)});
    lines({at(9.5f, 0.5f), at(0.5f, 9.5f)});
    break;
  case Part::Fullscreen:
    lines({at(0.5f, 3.5f), at(0.5f, 0.5f), at(3.5f, 0.5f)});
    lines({at(6.5f, 0.5f), at(9.5f, 0.5f), at(9.5f, 3.5f)});
    lines({at(9.5f, 6.5f), at(9.5f, 9.5f), at(6.5f, 9.5f)});
    lines({at(3.5f, 9.5f), at(0.5f, 9.5f), at(0.5f, 6.5f)});
    break;
  default:
    break;
  }
}

// A button: lit under the pointer (and while the mouse is held on it), as the launcher's are: the raised tone
// behind it, red behind close, and its drawing in the text colour instead of the secondary one (white on red). The
// light comes and goes in FADE_MS.
void DrawButton(Gdiplus::Graphics& g, const Shown& shown, Part part, const strip::Box& box, ULONGLONG now)
{
  const bool lit = shown.over == part && (shown.down == Part::Outside || shown.down == part);
  Fade& fade = *FadeOf(part);
  fade.Send(lit && !box.Empty() ? 1.0f : 0.0f, now);
  const float light = fade.At(now);
  s_look.fading = s_look.fading || light != fade.to;
  if (box.Empty())
    return;
  if (light > 0)
  {
    const Gdiplus::SolidBrush ground(Colour(part == Part::Close ? ov::CLOSE_OVER : ov::RAISED, light));
    g.FillRectangle(&ground, box.left, box.top, box.right - box.left, box.bottom - box.top);
  }
  const float unit = static_cast<float>(shown.dpi) / 96.0f;
  const float side = strip::DRAWING * unit;
  const std::uint32_t drawing = Between(ov::SECONDARY, part == Part::Close ? ov::ON_CLOSE : ov::TEXT, light);
  const Gdiplus::Pen pen(Colour(drawing), static_cast<float>(strip::Scaled(1, shown.dpi)));
  DrawLines(g, pen, part, shown.maximized, static_cast<float>(box.left) + std::floor((box.right - box.left - side) / 2),
            static_cast<float>(box.top) + std::floor((box.bottom - box.top - side) / 2), unit);
}

// One line of text in a box, cut with an ellipsis where the box ends; how wide it came out. `lean`: the display
// role, sheared about the line's foot so that its top moves right (the launcher's 12 degree skew); the reach past
// the text's own width is counted in.
float DrawText(Gdiplus::Graphics& g, const std::wstring& text, const Face& face, int size, std::uint32_t colour,
               const Gdiplus::RectF& box, Gdiplus::StringAlignment across, bool lean = false)
{
  if (text.empty() || box.Width <= 0 || !face.family)
    return 0;
  const Gdiplus::Font font(face.family.get(), static_cast<float>(size), face.style, Gdiplus::UnitPixel);
  Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
  format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
  format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  format.SetAlignment(across);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  const Gdiplus::SolidBrush brush(Colour(colour));
  const Gdiplus::GraphicsState before = g.Save();
  float reach = 0;
  if (lean)
  {
    // the line stands centred in the box: its foot is the box's middle plus half the line's height
    const float line = font.GetHeight(&g);
    const float foot = box.Y + (box.Height + line) / 2;
    Gdiplus::Matrix shear(1, 0, -ov::LEAN, 1, foot * ov::LEAN, 0);
    g.MultiplyTransform(&shear);
    reach = line * 0.7f * ov::LEAN;  // the capitals' top (about 0.7 of the line) moves right by this
  }
  g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, box, &format, &brush);
  g.Restore(before);
  Gdiplus::RectF drawn;
  g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, box, &format, &drawn);
  return drawn.Width + reach;
}

// A number with figures on one pitch (each digit in a cell as wide as the widest, since Saira's digits are not of
// one width): drawn right-aligned in the box, the line centred.
void DrawFigures(Gdiplus::Graphics& g, const std::wstring& text, const Face& face, int size, std::uint32_t colour,
                 const Gdiplus::RectF& box)
{
  if (text.empty() || box.Width <= 0 || !face.family)
    return;
  const Gdiplus::Font font(face.family.get(), static_cast<float>(size), face.style, Gdiplus::UnitPixel);
  Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
  format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  const auto wide = [&](const wchar_t* begin, INT count) {
    Gdiplus::RectF measured;
    g.MeasureString(begin, count, &font, Gdiplus::RectF(0, 0, 1e6f, 1e6f), &format, &measured);
    return measured.Width;
  };
  float pitch = 0;
  for (wchar_t d = L'0'; d <= L'9'; ++d)
    pitch = std::max(pitch, wide(&d, 1));
  const auto digit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
  // the pieces: a digit in its cell, a run of other letters as it is
  struct Piece { std::size_t from, to; float width; };
  std::vector<Piece> pieces;
  float total = 0;
  for (std::size_t i = 0; i < text.size();)
  {
    std::size_t j = i + 1;
    float w = pitch;
    if (!digit(text[i]))
    {
      while (j < text.size() && !digit(text[j]))
        ++j;
      w = wide(text.c_str() + i, static_cast<INT>(j - i));
    }
    pieces.push_back({i, j, w});
    total += w;
    i = j;
  }
  const Gdiplus::SolidBrush brush(Colour(colour));
  float x = box.X + box.Width - total;
  for (const Piece& piece : pieces)
  {
    float left = x;
    if (digit(text[piece.from]))
      left += (pitch - wide(text.c_str() + piece.from, 1)) / 2;
    g.DrawString(text.c_str() + piece.from, static_cast<INT>(piece.to - piece.from), &font,
                 Gdiplus::RectF(left, box.Y, box.Width, box.Height), &format, &brush);
    x += piece.width;
  }
}

// The name as a picture: as high as the name's text would be, each of its pixels a block of whole colour (the
// launcher draws its lettering so), clipped where the words' room ends; how wide it came out.
float DrawNamePicture(Gdiplus::Graphics& g, const Shown& shown, const Gdiplus::RectF& room)
{
  Gdiplus::Bitmap& picture = *s_look.name_picture;
  const float height = static_cast<float>(strip::Scaled(strip::NAME_SIZE, shown.dpi));
  const float width = height * static_cast<float>(picture.GetWidth()) / static_cast<float>(picture.GetHeight());
  const Gdiplus::RectF at(room.X, room.Y + std::round((room.Height - height) / 2), width, height);
  const Gdiplus::GraphicsState before = g.Save();
  g.SetClip(room);
  g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
  g.DrawImage(&picture, at);
  g.Restore(before);
  return std::min(width, room.Width);
}

void DrawStrip(Gdiplus::Graphics& g, const Shown& shown)
{
  const strip::Layout& l = shown.layout;
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);  // a line at x.5 is then one whole pixel, as in the launcher
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
  g.Clear(Colour(ov::BAR));
  const Gdiplus::SolidBrush line(Colour(ov::RAISED));
  g.FillRectangle(&line, 0, l.height - l.line, l.width, l.line);
  const ULONGLONG now = GetTickCount64();
  s_look.fading = false;
  DrawButton(g, shown, Part::Fullscreen, l.fullscreen, now);
  DrawButton(g, shown, Part::Minimize, l.minimize, now);
  DrawButton(g, shown, Part::Maximize, l.maximize, now);
  DrawButton(g, shown, Part::Close, l.close, now);
  const auto box = [](const strip::Box& b) {
    return Gdiplus::RectF(static_cast<float>(b.left), static_cast<float>(b.top), static_cast<float>(b.right - b.left),
                          static_cast<float>(b.bottom - b.top));
  };
  if (!l.words.Empty())
  {
    Gdiplus::RectF words = box(l.words);
    const float name = s_look.name_picture ? DrawNamePicture(g, shown, words) :
                                             DrawText(g, shown.name, s_look.name, strip::Scaled(strip::NAME_SIZE, shown.dpi),
                                                      ov::TEXT, words, Gdiplus::StringAlignmentNear, true);
    const float used = name > 0 ? name + static_cast<float>(strip::Scaled(strip::GAP, shown.dpi)) : 0;
    words.X += used;
    words.Width -= used;
    DrawText(g, shown.title, s_look.text, strip::Scaled(strip::SMALL_SIZE, shown.dpi), ov::SECONDARY, words,
             Gdiplus::StringAlignmentNear);
  }
  if (!l.frame_rate.Empty() && shown.frame_rate >= 0)
  {
    DrawFigures(g, std::to_wstring(std::min(shown.frame_rate, 999)) + L" FPS", s_look.figure,
                strip::Scaled(strip::SMALL_SIZE, shown.dpi), ov::SECONDARY, box(l.frame_rate));
  }
}
void PaintStrip(HDC dc, const Shown& shown);
}  // namespace

void LoadLook(const fzgx::WindowPicture& name)
{
  if (s_look.loaded)
    return;
  s_look.loaded = true;
  const Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&s_look.library, &input, nullptr) != Gdiplus::Ok)
    return;  // nothing is drawn then but the ground (Paint)
  Load(&s_look.name, strip::DISPLAY_FONT, true);
  Load(&s_look.figure, strip::FIGURE_FONT, true);
  Load(&s_look.text, strip::TEXT_FONT, false);
  // the reader took the picture only with exactly width x height x 4 bytes (red, green, blue, opacity); the library
  // wants blue first
  if (name.width == 0 || name.rgba.size() != std::size_t{name.width} * name.height * 4)
    return;
  s_look.name_pixels.assign(name.rgba.begin(), name.rgba.end());
  for (std::size_t i = 0; i + 3 < s_look.name_pixels.size(); i += 4)
    std::swap(s_look.name_pixels[i], s_look.name_pixels[i + 2]);
  auto picture = std::make_unique<Gdiplus::Bitmap>(static_cast<INT>(name.width), static_cast<INT>(name.height),
                                                   static_cast<INT>(name.width * 4), PixelFormat32bppARGB,
                                                   s_look.name_pixels.data());
  if (picture->GetLastStatus() == Gdiplus::Ok)
    s_look.name_picture = std::move(picture);
}

void Paint(HWND window, HDC dc, const Shown& shown)
{
  const bool was_fading = s_look.fading;
  s_look.fading = false;
  if (shown.layout.width > 0 && shown.layout.height > 0)
    PaintStrip(dc, shown);
  if (s_look.fading != was_fading)
  {
    if (s_look.fading)
      SetTimer(window, FADE_TIMER, 15, nullptr);
    else
      KillTimer(window, FADE_TIMER);
  }
}

bool FadeTick(HWND window, UINT_PTR timer, int strip_height)
{
  if (timer != FADE_TIMER)
    return false;
  if (IsIconic(window))
  {
    // a minimized window gets no paint, so the fade that ends in one would never stop
    KillTimer(window, FADE_TIMER);
    s_look.fading = false;
    return true;
  }
  RECT strip_area{};
  GetClientRect(window, &strip_area);
  strip_area.bottom = strip_height;
  InvalidateRect(window, &strip_area, FALSE);
  return true;
}

namespace
{
void PaintStrip(HDC dc, const Shown& shown)
{
  const int width = shown.layout.width, height = shown.layout.height;
  const HDC memory = CreateCompatibleDC(dc);
  const HBITMAP picture = CreateCompatibleBitmap(dc, width, height);
  if (memory && picture)
  {
    const HGDIOBJ before = SelectObject(memory, picture);
    if (s_look.library != 0)
    {
      Gdiplus::Graphics g(memory);
      DrawStrip(g, shown);
    }
    else
    {
      const RECT all{0, 0, width, height};
      FillRect(memory, &all, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, before);
  }
  if (picture)
    DeleteObject(picture);
  if (memory)
    DeleteDC(memory);
}
}  // namespace
}  // namespace fzgx_port::frame

#endif
