// Guest memory as the core reads and writes it: by guest address, big-endian, through one accessor.
//   - main memory (0x80000000 up to the retail 24 MiB), which is the host's RAM buffer;
//   - the mod's data window (layout::WINDOW: 0x81800000 to 0x8185C000), which has no guest memory behind it: it is a
//     buffer of this process (ModData). The game's routines reach it through the runtime's slow memory path, which
//     calls ModData too. No pointer into the window is ever handed out.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "fzgx/host.h"

// The game's and the mod's addresses (fzgx::layout, fzgx::game, ...): the facts table's generated header, by its
// place beside the core, so that no build needs a folder of its own named for it.
#include "../../../facts/generated/facts.h"

namespace fzgx
{
// The mod's data window. Bytes are stored as the game stores them (big-endian words); every access names a guest
// address. Not part of rollback or of the state checksum, exactly as the fork's mod memory was not: the undo log
// ends at 0x8155B000 (rollback.h), and what a rollback changes here is written explicitly by the session.
class ModData
{
public:
  ModData();

  static bool Contains(u32 address, std::size_t size = 1)
  {
    return address >= layout::WINDOW && size <= layout::WINDOW_END - layout::WINDOW &&
           address - layout::WINDOW <= layout::WINDOW_END - layout::WINDOW - size;
  }

  // Width 1, 2, 4 or 8 bytes. The runtime's memory hooks use these two.
  u64 Read(u32 address, unsigned width) const;
  void Write(u32 address, u64 value, unsigned width);

  void CopyTo(u32 address, const void* source, std::size_t size);
  void CopyFrom(void* destination, u32 address, std::size_t size) const;
  void Clear();

  // A copy of the whole window (tests compare it across a rollback; a test scene's saved state keeps it).
  std::vector<u8> Snapshot() const { return m_bytes; }
  bool Restore(const std::vector<u8>& snapshot);

private:
  std::vector<u8> m_bytes;
};

class Guest
{
public:
  Guest(Host& host, ModData& data) : m_host(host), m_data(data) {}

  Host& GetHost() const { return m_host; }
  ModData& Data() const { return m_data; }

  u8 Read8(u32 address) const { return static_cast<u8>(Read(address, 1)); }
  u16 Read16(u32 address) const { return static_cast<u16>(Read(address, 2)); }
  u32 Read32(u32 address) const { return static_cast<u32>(Read(address, 4)); }
  void Write8(u32 address, u8 value) { Write(address, value, 1); }
  void Write16(u32 address, u16 value) { Write(address, value, 2); }
  void Write32(u32 address, u32 value) { Write(address, value, 4); }

  // false (and nothing copied) when the range is in neither memory.
  bool CopyTo(u32 address, const void* source, std::size_t size);
  bool CopyFrom(void* destination, u32 address, std::size_t size) const;

  // Main memory only: `size` bytes at a guest address, for bulk work on the simulation's memory (the undo log, the
  // state checksum, textures). Null when the range is not wholly inside main memory.
  u8* Ram(u32 address, std::size_t size) const;

private:
  u64 Read(u32 address, unsigned width) const;
  void Write(u32 address, u64 value, unsigned width);

  Host& m_host;
  ModData& m_data;
};
}  // namespace fzgx
