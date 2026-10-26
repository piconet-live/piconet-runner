// The host of the replay scrubber's tests (test_scrubber.cpp, test_scrubber_seek.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>
#include <utility>
#include <vector>

#include "harness.h"

namespace scrub_test
{
// A host with a packer for the undo log (runs of zero bytes as a zero and their length, every other byte as it is:
// the log's units hold many zeros once each block is XORed with the step before's), that keeps every bar it is shown.
class ScrubHost : public TestHost
{
public:
  bool packs = true;
  std::vector<ScrubBar> bars;
  u32 keys = 0;

  void ReplayBar(const ScrubBar& bar) override { bars.push_back(bar); }
  u32 ReplayKeys() override { return std::exchange(keys, 0); }  // (presses: each is taken once)
  // The packed bytes begin with a hash of the prefix they were packed with, and every byte is XORed with the prefix's
  // byte at its place (where the prefix reaches): Unpack refuses a prefix that is not the same one, and with another
  // prefix the bytes would come out wrong. So every move through a packed unit checks the chain of prefixes.
  static u64 PrefixHash(const u8* prefix, std::size_t size)
  {
    u64 h = 1469598103934665603ull ^ size;
    for (std::size_t i = 0; i < size; ++i)
      h = (h ^ prefix[i]) * 1099511628211ull;
    return h;
  }
  bool Pack(const u8* in, std::size_t size, const u8* prefix, std::size_t prefix_size, std::vector<u8>* out) override
  {
    if (!packs)
      return false;
    ++packed_with_prefix;
    packed_with_prefix_bytes += prefix_size;
    std::vector<u8> mixed(in, in + size);
    for (std::size_t i = 0; i < size && i < prefix_size; ++i)
      mixed[i] ^= prefix[i];
    const u8* data = mixed.data();
    out->clear();
    const u64 h = PrefixHash(prefix, prefix_size);
    out->insert(out->end(), reinterpret_cast<const u8*>(&h), reinterpret_cast<const u8*>(&h) + 8);
    for (std::size_t i = 0; i < size;)
    {
      std::size_t run = 0;
      while (i + run < size && data[i + run] == 0 && run < 255)
        ++run;
      if (run != 0)
        out->insert(out->end(), {0, static_cast<u8>(run)});
      else
        out->push_back(data[i]);
      i += run != 0 ? run : 1;
    }
    return true;
  }
  bool Unpack(const u8* packed, std::size_t packed_size, const u8* prefix, std::size_t prefix_size, u8* out,
              std::size_t out_size) override
  {
    u64 h = 0;
    if (packed_size < 8 || (std::memcpy(&h, packed, 8), h != PrefixHash(prefix, prefix_size)))
    {
      ++wrong_prefix;
      return false;
    }
    const u8* data = packed + 8;
    const std::size_t size = packed_size - 8;
    std::size_t at = 0;
    for (std::size_t i = 0; i < size && at <= out_size; ++i)
    {
      const std::size_t run = data[i] == 0 && i + 1 < size ? data[++i] : 0;
      if (run != 0)
        std::fill_n(out + at, std::min(run, out_size - at), 0);
      else
        out[at] = data[i];
      at += run != 0 ? run : 1;
    }
    for (std::size_t i = 0; i < out_size && i < prefix_size; ++i)
      out[i] ^= prefix[i];
    return at == out_size;
  }
  u32 packed_with_prefix = 0, wrong_prefix = 0;
  std::size_t packed_with_prefix_bytes = 0;
};
}  // namespace scrub_test
