// Pictures from the game's own files (fzgx/art.h): the .lz unpacker and a .tpl texture as pixels. The packed
// format and the texture file's layout as piconet-tools reads them (Fzgx.GameCube: AvLz, AvTpl).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/art.h"

#include <array>
#include <cstddef>

namespace fzgx::art
{
namespace
{
constexpr std::size_t RING = 4096, RING_START = 4078, HEADER = 8, MOST = 64u << 20;
constexpr u32 RGB5A3 = 5;

u32 Le32(const u8* p)
{
  return u32{p[0]} | (u32{p[1]} << 8) | (u32{p[2]} << 16) | (u32{p[3]} << 24);
}

u32 Be32(const u8* p)
{
  return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | u32{p[3]};
}

u32 Be16(const u8* p)
{
  return (u32{p[0]} << 8) | u32{p[1]};
}

void Pixel(u32 value, u8* out)
{
  if (value & 0x8000)
  {
    const auto five = [](u32 v) { return static_cast<u8>((v << 3) | (v >> 2)); };
    out[0] = five((value >> 10) & 31), out[1] = five((value >> 5) & 31), out[2] = five(value & 31), out[3] = 255;
    return;
  }
  const u32 opacity = (value >> 12) & 7;
  out[0] = static_cast<u8>(((value >> 8) & 15) * 17);
  out[1] = static_cast<u8>(((value >> 4) & 15) * 17);
  out[2] = static_cast<u8>((value & 15) * 17);
  out[3] = static_cast<u8>((opacity << 5) | (opacity << 2) | (opacity >> 1));
}
}  // namespace

std::vector<u8> Unpack(const std::vector<u8>& packed)
{
  if (packed.size() < HEADER)
    return {};
  const std::size_t said = Le32(packed.data()), size = Le32(packed.data() + 4);
  if ((said != packed.size() && said != packed.size() - HEADER) || size > MOST)
    return {};
  std::vector<u8> out;
  out.reserve(size);
  std::array<u8, RING> ring{};
  std::size_t at = RING_START, in = HEADER;
  u32 flags = 0;
  while (in < packed.size() && out.size() < size)
  {
    flags >>= 1;
    if ((flags & 0x100) == 0)
      flags = packed[in++] | 0xFF00u;
    if (in >= packed.size())
      break;
    if (flags & 1)
    {
      out.push_back(ring[at++ % RING] = packed[in++]);
      continue;
    }
    if (in + 1 >= packed.size())
      return {};
    const std::size_t from = ((packed[in + 1] & 0xF0u) << 4) | packed[in], count = (packed[in + 1] & 15u) + 3;
    in += 2;
    for (std::size_t i = 0; i < count && out.size() < size; ++i)
      out.push_back(ring[at++ % RING] = ring[(from + i) % RING]);
  }
  if (out.size() != size)
    return {};
  return out;
}

Image Texture(const std::vector<u8>& file, u32 index)
{
  if (file.size() < 4 || index >= Be32(file.data()) || 4 + (std::size_t{index} + 1) * 16 > file.size())
    return {};
  const u8* entry = file.data() + 4 + std::size_t{index} * 16;
  const u32 format = Be32(entry), offset = Be32(entry + 4), width = Be16(entry + 8), height = Be16(entry + 10);
  if (format != RGB5A3 || Be16(entry + 12) == 0 || Be16(entry + 14) != 0x1234 || width == 0 || height == 0 ||
      width > 1024 || height > 1024)
    return {};
  // stored in whole tiles of 4 by 4
  const u32 tiles_x = (width + 3) / 4, tiles_y = (height + 3) / 4;
  if (offset > file.size() || std::size_t{tiles_x} * tiles_y * 32 > file.size() - offset)
    return {};
  Image image{width, height, std::vector<u8>(std::size_t{width} * height * 4)};
  const u8* data = file.data() + offset;
  for (u32 ty = 0; ty < tiles_y; ++ty)
    for (u32 tx = 0; tx < tiles_x; ++tx)
      for (u32 i = 0; i < 16; ++i, data += 2)
        if (const u32 x = tx * 4 + i % 4, y = ty * 4 + i / 4; x < width && y < height)
          Pixel(Be16(data), image.At(x, y));
  return image;
}
}  // namespace fzgx::art
