// SPDX-License-Identifier: GPL-3.0-or-later

#include "util.h"

#include <cstring>

namespace fzgx
{
std::vector<std::string> SplitString(const std::string& text, char separator)
{
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true)
  {
    const std::size_t at = text.find(separator, start);
    if (at == std::string::npos)
    {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, at - start));
    start = at + 1;
  }
}

std::string_view StripWhitespace(std::string_view text)
{
  const char* spaces = " \t\r\n";
  const std::size_t first = text.find_first_not_of(spaces);
  if (first == std::string_view::npos)
    return {};
  return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

std::string Sha1Hex(const u8* data, std::size_t size)
{
  u32 h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  const auto rol = [](u32 v, int n) { return (v << n) | (v >> (32 - n)); };
  const auto block = [&](const u8* p) {
    u32 w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (u32(p[4 * i]) << 24) | (u32(p[4 * i + 1]) << 16) | (u32(p[4 * i + 2]) << 8) | u32(p[4 * i + 3]);
    for (int i = 16; i < 80; ++i)
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i)
    {
      u32 f, k;
      if (i < 20)
        f = (b & c) | (~b & d), k = 0x5A827999u;
      else if (i < 40)
        f = b ^ c ^ d, k = 0x6ED9EBA1u;
      else if (i < 60)
        f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
      else
        f = b ^ c ^ d, k = 0xCA62C1D6u;
      const u32 t = rol(a, 5) + f + e + k + w[i];
      e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  };
  std::size_t at = 0;
  for (; at + 64 <= size; at += 64)
    block(data + at);
  u8 tail[128] = {};
  const std::size_t rest = size - at;
  if (rest)
    std::memcpy(tail, data + at, rest);
  tail[rest] = 0x80;
  const std::size_t total = rest + 9 <= 64 ? 64 : 128;
  const u64 bits = static_cast<u64>(size) * 8;
  for (int i = 0; i < 8; ++i)
    tail[total - 1 - i] = static_cast<u8>(bits >> (8 * i));
  block(tail);
  if (total == 128)
    block(tail + 64);
  std::string hex;
  for (const u32 word : h)
    hex += std::format("{:08x}", word);
  return hex;
}
std::optional<std::vector<u8>> Base64Bytes(std::string_view text)
{
  const auto six = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  if (text.size() % 4 != 0)
    return std::nullopt;
  std::vector<u8> bytes;
  bytes.reserve(text.size() / 4 * 3);
  for (std::size_t i = 0; i < text.size(); i += 4)
  {
    // '=' only at the end of the last group: one for two bytes, two for one
    const bool last = i + 4 == text.size();
    const int pad = last && text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0;
    u32 group = 0;
    for (int k = 0; k < 4; ++k)
    {
      const int value = k >= 4 - pad ? 0 : six(text[i + static_cast<std::size_t>(k)]);
      if (value < 0)
        return std::nullopt;
      group = group << 6 | static_cast<u32>(value);
    }
    bytes.push_back(static_cast<u8>(group >> 16));
    if (pad < 2)
      bytes.push_back(static_cast<u8>(group >> 8));
    if (pad < 1)
      bytes.push_back(static_cast<u8>(group));
  }
  return bytes;
}
}  // namespace fzgx
