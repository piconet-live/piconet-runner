// A file of this computer, by its UTF-8 path: the one place the core opens one (File and its two readers, util.h),
// to read or to write.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fzgx
{
bool File::Open(const std::string& path, const char* mode)
{
  Close();
  const std::filesystem::path p(std::u8string(path.begin(), path.end()));
#ifdef _WIN32
  const std::wstring wide_mode(mode, mode + std::strlen(mode));
  m_file = _wfopen(p.c_str(), wide_mode.c_str());
#else
  m_file = std::fopen(p.c_str(), mode);
#endif
  return m_file != nullptr;
}

bool IsFile(const std::string& path)
{
  std::error_code error;
  return std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), error);
}

bool MoveOver(const std::string& from, const std::string& to)
{
  std::error_code error;
  std::filesystem::rename(std::filesystem::path(std::u8string(from.begin(), from.end())),
                          std::filesystem::path(std::u8string(to.begin(), to.end())), error);
  return !error;
}

void RemoveQuietly(const std::string& path)
{
  std::error_code ignored;
  std::filesystem::remove(std::filesystem::path(std::u8string(path.begin(), path.end())), ignored);
}

std::vector<std::string> FolderFiles(const std::string& folder, const std::string& ending)
{
  std::vector<std::string> names;
  std::error_code error;
  const std::filesystem::path path(std::u8string(folder.begin(), folder.end()));
  for (std::filesystem::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
  {
    std::error_code ignored;
    if (it->is_directory(ignored))
      continue;
    const std::u8string u8name = it->path().filename().u8string();
    const std::string name(u8name.begin(), u8name.end());
    if (name.size() < ending.size())
      continue;
    bool ends = true;
    for (std::size_t i = 0; ends && i < ending.size(); ++i)
    {
      const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
      ends = lower(name[name.size() - ending.size() + i]) == lower(ending[i]);
    }
    if (ends)
      names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool File::Close()
{
  const bool closed = !m_file || std::fclose(m_file) == 0;
  m_file = nullptr;
  return closed;
}

std::size_t File::GetSize()
{
  if (!m_file)
    return 0;
  const long at = std::ftell(m_file);
  std::fseek(m_file, 0, SEEK_END);
  const long size = std::ftell(m_file);
  std::fseek(m_file, at, SEEK_SET);
  return size > 0 ? static_cast<std::size_t>(size) : 0;
}

bool File::ReadBytes(void* data, std::size_t size)
{
  return m_file && std::fread(data, 1, size, m_file) == size;
}

bool File::WriteBytes(const void* data, std::size_t size)
{
  return m_file && std::fwrite(data, 1, size, m_file) == size;
}

bool File::Flush()
{
  return m_file && std::fflush(m_file) == 0;
}

bool ReadFileToBytes(const std::string& path, std::vector<u8>& out)
{
  File file(path, "rb");
  if (!file.IsOpen())
    return false;
  out.resize(file.GetSize());
  return out.empty() || file.ReadBytes(out.data(), out.size());
}

bool ReadFileToString(const std::string& path, std::string& out)
{
  std::vector<u8> bytes;
  if (!ReadFileToBytes(path, bytes))
    return false;
  out.assign(bytes.begin(), bytes.end());
  return true;
}
}  // namespace fzgx
