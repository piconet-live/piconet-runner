// What the tests of the player's controller options share (test_save.cpp: the save file's reader; test_save_race.cpp:
// a race's frame tick): options the game's screens can write, and a save file made of them. Every save here is made
// by the test: a file of zeros with a directory entry, a version, six blocks of options and the checksum the game
// would write. No byte of a game's save.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "harness.h"

#include "files.h"

namespace save_files
{
using boot::ControllerOptions;

constexpr std::size_t ENTRY = 0x40, SAVE = 0x8000, BLOCK = ENTRY + 0x2060;

inline void Put16(u8* p, u16 v)
{
  p[0] = static_cast<u8>(v >> 8);
  p[1] = static_cast<u8>(v);
}

// Options the game's screens can write, the same for every port: an eight-cornered gate 75 out, dead zones of 8, a
// binding of one allowed control in every entry of every set, a wheel's range of 120 with a play of 6, scheme 1.
inline ControllerOptions Options0()
{
  ControllerOptions o;
  const std::array<signed char, 16> gate{75, 0, 53, 53, 0, 75, -53, 53, -75, 0, -53, -53, 0, -75, 53, -53};
  const std::array<u16, 4> axes{0x0010, 0x0100, 0x0001, 0x0002};
  const std::array<u32, 5> buttons{0x100, 0x200, 0x400, 0x10, 0x800};
  for (int port = 0; port < 4; ++port)
  {
    std::memcpy(o.stick_gate.data() + port * 16, gate.data(), 16);
    for (int set = 0; set < 3; ++set)
    {
      u8* s = o.bindings.data() + port * 0x54 + set * 0x1C;
      for (int i = 0; i < 4; ++i)
        Put16(s + i * 2, axes[i]);
      for (int i = 0; i < 5; ++i)
        boot::Put32(s + 8 + i * 4, buttons[i]);
    }
  }
  o.dead_zones.fill(8);
  o.wheel_range.fill(120);
  o.wheel_play.fill(6);
  o.scheme.fill(1);
  return o;
}

// What a player changed: port 1's accelerator and brake swapped on the pad, its dead zones 2 and 50, a corner of its
// gate where a worn stick reaches, port 4's wheel, schemes 0.
inline ControllerOptions Options1()
{
  ControllerOptions o = Options0();
  boot::Put32(o.bindings.data() + 8, 0x200);
  boot::Put32(o.bindings.data() + 12, 0x100);
  o.dead_zones[0] = 2;
  o.dead_zones[1] = 50;
  o.stick_gate[0] = 100;
  o.stick_gate[1] = 10;
  o.wheel_range[6] = 90;
  o.wheel_range[7] = 128;
  o.wheel_play[6] = o.wheel_play[7] = 20;
  o.scheme[0] = o.scheme[3] = 0;
  return o;
}

// The save's checksum written into a save file's bytes.
inline void Seal(std::vector<u8>& file)
{
  Put16(file.data() + ENTRY, boot::SaveChecksum(file.data() + ENTRY + 2, SAVE - 2));
}

// A save file with these options, as the game would have written it (and as the emulator keeps it).
inline std::vector<u8> SaveOf(const ControllerOptions& o)
{
  std::vector<u8> file(ENTRY + SAVE, 0);
  std::memcpy(file.data(), "GFZE8P", 6);
  std::memcpy(file.data() + 8, "f_zero.dat", 10);
  Put16(file.data() + 0x38, 4);
  file[ENTRY + 2] = 1;
  file[ENTRY + 3] = 11;
  std::memcpy(file.data() + BLOCK + 0x8, o.stick_gate.data(), o.stick_gate.size());
  std::memcpy(file.data() + BLOCK + 0x48, o.dead_zones.data(), o.dead_zones.size());
  std::memcpy(file.data() + BLOCK + 0x50, o.bindings.data(), o.bindings.size());
  std::memcpy(file.data() + BLOCK + 0x5542, o.wheel_range.data(), o.wheel_range.size());
  std::memcpy(file.data() + BLOCK + 0x554A, o.wheel_play.data(), o.wheel_play.size());
  std::memcpy(file.data() + BLOCK + 0x5556, o.scheme.data(), o.scheme.size());
  Seal(file);
  return file;
}

inline void WriteBytes(const std::string& path, const std::vector<u8>& bytes)
{
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The reader's answer to a save made of `o` after `change`: empty when it gave the options back, else its sentence.
inline std::string Refusal(const std::function<void(ControllerOptions&)>& change)
{
  ControllerOptions o = Options0();
  change(o);
  const boot::SavedOptions saved = boot::OptionsOfSave(SaveOf(o));
  if (saved.options)
    return *saved.options == o ? "" : "other options than the save's";
  return saved.refused.empty() ? "refused without a word" : saved.refused;
}

inline bool Says(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

inline int Count(const std::string& text, const std::string& part)
{
  int n = 0;
  for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
    ++n;
  return n;
}
}  // namespace save_files
