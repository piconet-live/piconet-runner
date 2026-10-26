// The overlays' type (overlay.h): the launcher's font files from the Fonts folder beside the runner into the
// emulator's font atlas, and a run of text drawn with the two things the atlas cannot do by itself: the display
// role's lean (the glyphs' corners sheared after they are laid, about the line's foot) and figures on one pitch
// (each digit in a cell as wide as the widest, since Saira's digits are not of one width and ImGui applies no
// font feature). Nothing here reads or writes the game.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlay.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "fzgx/strip.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fzgx_port::overlay
{
namespace
{
std::filesystem::path FontFolder()
{
#ifdef _WIN32
  wchar_t program[MAX_PATH * 4] = {};
  GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program) - 1));
  return std::filesystem::path(program).parent_path() / L"Fonts";
#else
  std::error_code e;
  return std::filesystem::read_symlink("/proc/self/exe", e).parent_path() / "Fonts";
#endif
}

// A font file of the Fonts folder into the emulator's font atlas; none when it is missing.
ImFont* Load(const char* file)
{
  const std::filesystem::path path = FontFolder() / file;
  std::error_code e;
  if (!std::filesystem::is_regular_file(path, e))
    return nullptr;
  const std::u8string name = path.u8string();  // (ImGui opens a file by its UTF-8 name)
  ImFontConfig config;
  std::snprintf(config.Name, sizeof(config.Name), "%s", file);
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(name.c_str()), 16.0f, &config);
}

ImFont* FontOf(const Type& type)
{
  return type.font ? type.font : ImGui::GetFont();
}

float Wide(ImFont* font, float size, const char* begin, const char* end)
{
  return font->CalcTextSizeA(size, 1e9f, 0, begin, end).x;
}

// The pitch of a tabular run: the widest digit's width.
float Pitch(ImFont* font, float size)
{
  float pitch = 0;
  for (char d[2] = {'0', 0}; d[0] <= '9'; ++d[0])
    pitch = std::max(pitch, Wide(font, size, d, d + 1));
  return pitch;
}

bool Digit(char c)
{
  return c >= '0' && c <= '9';
}

// A tabular run, piece by piece: a run of letters as it is, a digit in its cell. `draw` takes each piece with its
// left edge; the whole width comes back.
template <class Draw>
float Pieces(ImFont* font, float size, const char* text, float x, Draw draw)
{
  const float pitch = Pitch(font, size);
  const float start = x;
  const char* p = text;
  while (*p)
  {
    if (Digit(*p))
    {
      const float w = Wide(font, size, p, p + 1);
      draw(p, p + 1, x + (pitch - w) / 2);
      x += pitch;
      ++p;
      continue;
    }
    const char* q = p;
    while (*q && !Digit(*q))
      ++q;
    draw(p, q, x);
    x += Wide(font, size, p, q);
    p = q;
  }
  return x - start;
}
}  // namespace

const Fonts& TheFonts()
{
  static Fonts fonts;
  static bool tried = false;
  if (!tried && ImGui::GetCurrentContext() != nullptr)
  {
    tried = true;
    fonts.body = Load(fzgx::strip::TEXT_FONT);
    fonts.semi = Load(fzgx::strip::FIGURE_FONT);
    fonts.display = Load(fzgx::strip::DISPLAY_FONT);
  }
  return fonts;
}

float Width(const Type& type, const char* text)
{
  ImFont* font = FontOf(type);
  if (!type.tabular)
    return Wide(font, type.size, text, nullptr);
  return Pieces(font, type.size, text, 0, [](const char*, const char*, float) {});
}

void Draw(ImDrawList* list, const Type& type, float x, float top, ImU32 colour, const char* text, const ImVec4* clip)
{
  ImFont* font = FontOf(type);
  const int first = list->VtxBuffer.Size;
  const ImVec2 at(x, top);
  if (!type.tabular)
    list->AddText(font, type.size, at, colour, text, nullptr, 0, clip);
  else
    Pieces(font, type.size, text, x, [&](const char* begin, const char* end, float left) {
      list->AddText(font, type.size, ImVec2(left, top), colour, begin, end, 0, clip);
    });
  if (!type.lean)
    return;
  // the shear about the line's foot: the top of the line moves right by its height times the lean
  const float foot = top + type.size;
  for (int i = first; i < list->VtxBuffer.Size; ++i)
  {
    ImDrawVert& v = list->VtxBuffer[i];
    v.pos.x += (foot - v.pos.y) * LEAN;
  }
}
}  // namespace fzgx_port::overlay
