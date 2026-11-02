// The state checksum (rollback.h: StateChecksum, Rollback::InChecksum) and the words it leaves out.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback.h"

#include "mode.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace fzgx::net
{
// The state checksum (port/image/routines/netframe.py, which computes it on the emulated CPU outside a session):
// FNV-1a over the 32-bit words of every machine's block (0x620 bytes) and progress record ([+0x49C], 0x1FC
// bytes), leaving out the words whose bit is set in the masks game::BLOCK_SKIP and game::PROGRESS_SKIP (pointers and
// drawing-only fields; the facts table makes them from the machine-state map: the top bit of mask 0 is the first
// word). Computed here before a step for the state after the previous one, natively, so it costs the emulated CPU
// nothing. With a game mode, its block follows, whole.
constexpr u32 FIRST_WORD = 1u << 31;           // in a mask of 32 words: the bit of its first word
constexpr u32 FNV_OFFSET_BASIS = 2166136261u;  // FNV-1a over 32 bits: the value the hash starts from,
constexpr u32 FNV_PRIME = 16777619u;           // and what it is multiplied by after every word

u32 Big32(const u8* p)
{
  return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

// The byte offsets of the words a mask leaves in, made once from the facts table's masks: the hash then runs over
// a list, with no test of a mask's bit and no branch for each word. The same words in the same order as the masks
// say: the value is the same (TestChecksum compares it with the words taken one by one).
template <size_t N>
struct Words
{
  std::array<u16, 32 * N> at{};  // room for every word a mask of N parts can name
  u32 count = 0;
};

template <size_t N>
constexpr Words<N> Included(const std::array<u32, N>& skip, u32 size)
{
  Words<N> words;
  for (u32 k = 0; k < size / 4; ++k)
  {
    if (((skip[k >> 5] << (k & 31)) & FIRST_WORD) == 0)
      words.at[words.count++] = static_cast<u16>(k * 4);
  }
  return words;
}

constexpr auto BLOCK_WORDS = Included(game::BLOCK_SKIP, game::MACHINE_SIZE);
constexpr auto PROGRESS_WORDS = Included(game::PROGRESS_SKIP, game::PROGRESS_SIZE);

// A big-endian word: one load and one byte swap.
inline u32 LoadBig32(const u8* p)
{
  u32 v;
  std::memcpy(&v, p, 4);
#if defined(_MSC_VER)
  return _byteswap_ulong(v);
#else
  return __builtin_bswap32(v);
#endif
}

template <size_t N>
u32 HashWords(u32 h, const u8* p, const Words<N>& words)
{
  for (u32 i = 0; i < words.count; ++i)
    h = (h ^ LoadBig32(p + words.at[i])) * FNV_PRIME;
  return h;
}

bool Rollback::InChecksum(bool progress, u32 offset)
{
  const u32 k = offset / 4;
  if (progress)
    return offset < game::PROGRESS_SIZE && ((game::PROGRESS_SKIP[k >> 5] << (k & 31)) & FIRST_WORD) == 0;
  return offset < game::MACHINE_SIZE && ((game::BLOCK_SKIP[k >> 5] << (k & 31)) & FIRST_WORD) == 0;
}

u32 StateChecksum(Guest& g, u32 module)
{
  const u8* count = g.Ram((module + MACHINE_COUNT), 1);
  const u32 machines = Big32(g.Ram((module + MACHINES), 4));
  u32 h = FNV_OFFSET_BASIS;
  if (count == nullptr || machines == 0)
    return h;
  for (u32 i = 0; i < *count; ++i)
  {
    const u8* block = g.Ram((machines + i * game::MACHINE_SIZE), game::MACHINE_SIZE);
    h = HashWords(h, block, BLOCK_WORDS);
    const u32 progress = Big32(block + game::MACHINE_PROGRESS);
    h = HashWords(h, g.Ram(progress, game::PROGRESS_SIZE), PROGRESS_WORDS);
  }
  // a game mode's block, every word (mode.h): its state is the race's. A race without one hashes as before.
  if (const u32 block = g.Read32(layout::MODES + 4); block != 0)
  {
    // (the stats page only in a mode that has it: every other mode's block sums as it did)
    const u32 size = g.Read32(block + mode::STATS_ON) != 0 ? mode::BLOCK_WITH_STATS : mode::BLOCK;
    if (const u8* p = g.Ram(block, size))
      for (u32 at = 0; at < size; at += 4)
        h = (h ^ LoadBig32(p + at)) * FNV_PRIME;
    // M+0x5DA is outside the words above (BLOCK_SKIP). A mode's max energy counts: only a value the mode wrote is
    // summed, so a race that never writes it (the field is the game's 100) sums as before
    for (u32 i = 0; i < *count; ++i)
      if (const u32 most = g.Read16(machines + i * game::MACHINE_SIZE + game::MACHINE_MAX_ENERGY); most != 100 && most != 0)
        h = (h ^ most) * FNV_PRIME;
  }
  return h;
}
}  // namespace fzgx::net
