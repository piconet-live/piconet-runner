// Pictures from the game's own files, for what the host draws in the game's look (fzgx/panel.h): the game's
// packed files unpacked (.lz), and a texture of one of its texture files (.tpl) as plain pixels. Read from the
// player's own disc files at run time; nothing of them is in the program.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "fzgx/host.h"

namespace fzgx::art
{
// Pixels of four bytes (red, green, blue, opacity), row by row from the top. Empty (no width): none.
struct Image
{
  u32 width = 0, height = 0;
  std::vector<u8> rgba;

  bool Empty() const { return width == 0 || height == 0; }
  const u8* At(u32 x, u32 y) const { return &rgba[(std::size_t{y} * width + x) * 4]; }
  u8* At(u32 x, u32 y) { return &rgba[(std::size_t{y} * width + x) * 4]; }
};

// A packed file of the game (.lz) unpacked: eight bytes of header (the packed size, with or without the header
// itself, and the unpacked size, both low byte first), then LZSS with a ring of 4,096 bytes that starts at 4,078,
// flag bits low first (1: a byte as it is; 0: two bytes, twelve bits of place in the ring and four of length less
// three). Empty when the file is not that, or would unpack to more than 64 MiB.
std::vector<u8> Unpack(const std::vector<u8>& packed);

// Texture `index` of a texture file (.tpl, unpacked): the file begins with a count and entries of 16 bytes
// (format, offset, width, height, levels, the mark 0x1234), high byte first. Only the format the panel's art is
// in is read: RGB5A3 (5), tiles of 4 by 4 pixels of 16 bits (the top bit set: five bits a colour, solid; clear:
// three bits of opacity and four a colour). Empty for any other texture, or one that does not lie in the file.
Image Texture(const std::vector<u8>& file, u32 index);
}  // namespace fzgx::art
