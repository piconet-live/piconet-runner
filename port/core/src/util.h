// Small helpers the lifted fork code used from Dolphin's Common: strings, files, SHA-1, formatting and logging.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <charconv>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fzgx/host.h"

namespace fzgx
{
std::vector<std::string> SplitString(const std::string& text, char separator);
std::string_view StripWhitespace(std::string_view text);

template <typename T>
bool TryParse(const std::string& text, T* out, int base = 10)
{
  std::string_view t = StripWhitespace(text);
  if (base == 16 && t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
    t.remove_prefix(2);
  T value{};
  const auto result = std::from_chars(t.data(), t.data() + t.size(), value, base);
  if (result.ec != std::errc{} || result.ptr != t.data() + t.size() || t.empty())
    return false;
  *out = value;
  return true;
}

// A file by its UTF-8 path (files_os.cpp, the one place the core opens a file).
class File
{
public:
  File() = default;
  File(const std::string& path, const char* mode) { Open(path, mode); }
  ~File() { Close(); }
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  bool Open(const std::string& path, const char* mode);
  bool Close();  // false: the file's last bytes did not reach it
  bool IsOpen() const { return m_file != nullptr; }
  std::size_t GetSize();
  bool ReadBytes(void* data, std::size_t size);
  bool WriteBytes(const void* data, std::size_t size);
  bool Flush();

private:
  std::FILE* m_file = nullptr;
};

// The names (not the paths) of a folder's files whose name ends in `ending`, whatever the case of its letters, in
// the order of their bytes; folders inside it are not named. None when the folder cannot be listed.
std::vector<std::string> FolderFiles(const std::string& folder, const std::string& ending);
bool ReadFileToString(const std::string& path, std::string& out);
bool IsFile(const std::string& path);  // a file is there under that name (not a folder)
bool MoveOver(const std::string& from, const std::string& to);  // `from` renamed to `to`, replacing what was there
void RemoveQuietly(const std::string& path);  // the file removed if it is there; nothing said either way
bool ReadFileToBytes(const std::string& path, std::vector<u8>& out);

// Lower-case hexadecimal SHA-1.
std::string Sha1Hex(const u8* data, std::size_t size);
// Lower-case hexadecimal SHA-256 (FIPS 180-4): a game mode's content hash (protocol/formats.md section 9).
std::string Sha256Hex(const u8* data, std::size_t size);
// The bytes a base64 text stands for (the usual alphabet, padded with '='); nothing when the text is not base64.
std::optional<std::vector<u8>> Base64Bytes(std::string_view text);

inline u32 Swap32(u32 v)
{
  return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

// The host's log, set by fzgx::Init. Formatted with std::format.
void LogText(LogLevel level, const std::string& text);

template <typename... Args>
void Notice(std::format_string<Args...> f, Args&&... args)
{
  LogText(LogLevel::Notice, std::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void Warn(std::format_string<Args...> f, Args&&... args)
{
  LogText(LogLevel::Warning, std::format(f, std::forward<Args>(args)...));
}
template <typename... Args>
void Error(std::format_string<Args...> f, Args&&... args)
{
  LogText(LogLevel::Error, std::format(f, std::forward<Args>(args)...));
}
}  // namespace fzgx
