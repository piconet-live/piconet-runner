// The disc's files the core reads, from the host (the extracted game, not a disc image): a machine's stock texture
// files under vehicle/, and where the textures lie in one, for paint.cpp; and the game's 24 by 24 font, for the
// panel before a race (fzgx/panel.h). Lifted from the Dolphin fork (dolphin-fzgx
// Core/FzgxPaint.cpp at 5bdc7c640e).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include "fzgx/art.h"
#include "fzgx/panel.h"

#include <algorithm>
#include <format>

namespace fzgx::paint
{
namespace
{
std::optional<Bytes> ReadDiscFile(const std::string& path)
{
  Bytes data;
  if (!TheHost().ReadGameFile(path, &data))
    return std::nullopt;
  return data;
}
}  // namespace

// The texture slots (format, byte range with every mip level) of a texture file: count at +0, then 16-byte entries
// (format, offset, width, height, levels, ...); slots without levels are empty
std::vector<Slot> Slots(const Bytes& data)
{
  std::vector<std::pair<u32, u32>> entries;  // (offset, format)
  if (data.size() < 4)
    return {};
  const u32 n = Be32(data.data());
  for (u32 i = 0; i < n && 4 + i * 16 + 16 <= data.size(); ++i)
  {
    const u8* e = data.data() + 4 + i * 16;
    if (Be16(e + 12) != 0)
      entries.emplace_back(Be32(e + 4), Be32(e));
  }
  std::ranges::sort(entries);
  std::vector<Slot> slots;
  for (size_t k = 0; k < entries.size(); ++k)
  {
    const u32 end = k + 1 < entries.size() ? entries[k + 1].first : static_cast<u32>(data.size());
    if (entries[k].first < end && end <= data.size())
      slots.push_back({entries[k].second, entries[k].first, end});
  }
  return slots;
}

// A set of four stock files (main: <name>.tpl, _cv1-3; opponent: _sv0-3)
std::optional<std::array<Bytes, 4>> ReadSet(const std::string& name, bool main)
{
  std::array<Bytes, 4> set;
  for (int i = 0; i < 4; ++i)
  {
    const std::string file = main ? (i == 0 ? name + ".tpl" : std::format("{}_cv{}.tpl", name, i)) :
                                     std::format("{}_sv{}.tpl", name, i);
    auto data = ReadDiscFile("vehicle/" + name + "/" + file);
    if (!data || (i > 0 && data->size() != set[0].size()))
      return std::nullopt;
    set[i] = std::move(*data);
  }
  return set;
}
}  // namespace fzgx::paint

namespace fzgx::panel
{
bool LoadFromTheGame(Font* font, std::size_t* size_read)
{
  std::vector<u8> file;
  const bool read = TheHost().ReadGameFile(FONT_FILE, &file);
  if (size_read)
    *size_read = read ? file.size() : 0;
  return read && font->Load(file);
}

void LoadArtFromTheGame(Lettering* lettering, Meter* meter, std::string* problem)
{
  const auto unpacked = [&](const char* path) {
    std::vector<u8> file;
    if (!TheHost().ReadGameFile(path, &file))
      *problem += std::string(problem->empty() ? "" : "; ") + path + " is not there";
    else if (file = art::Unpack(file); file.empty())
      *problem += std::string(problem->empty() ? "" : "; ") + path + " does not unpack";
    return file;
  };
  if (const std::vector<u8> letters = unpacked(LETTERS_FILE); !letters.empty() && !lettering->Load(art::Texture(letters, 0)))
    *problem += std::string(problem->empty() ? "" : "; ") + "the venue names' strips are not texture 0 of " + LETTERS_FILE;
  if (const std::vector<u8> hud = unpacked(METER_FILE); !hud.empty())
  {
    meter->frame = art::Texture(hud, METER_FRAME);
    meter->fill = art::Texture(hud, METER_FILL);
    if (!meter->Loaded() || meter->frame.width != meter->fill.width + 2 * METER_EDGE ||
        meter->frame.height != meter->fill.height + 2 * METER_EDGE)
    {
      *meter = {};
      *problem += std::string(problem->empty() ? "" : "; ") + "the energy meter is not textures 22 and 23 of " + METER_FILE;
    }
  }
}
}  // namespace fzgx::panel

namespace fzgx::minimap
{
namespace
{
std::vector<u8> ReadPacked(const std::string& file, std::string* why)
{
  std::vector<u8> packed;
  if (!TheHost().ReadGameFile(file, &packed))
  {
    *why = file + " is not on the disc";
    return {};
  }
  std::vector<u8> unpacked = art::Unpack(packed);
  if (unpacked.empty())
    *why = file + " does not unpack";
  return unpacked;
}
}  // namespace

std::vector<u8> ReadPreviewFile(u32 course, std::string* why)
{
  return ReadPacked(std::format("stage/selst{:02}.gma.lz", course), why);
}

std::vector<u8> ReadCourseFile(u32 course, std::string* why)
{
  return ReadPacked(std::format("stage/COLI_COURSE{:02}.lz", course), why);
}
}  // namespace fzgx::minimap
