// The status board's file, mapped into this process (board_net.h Mapping): the one place the core maps a file.
// The launcher made it and keeps it; the game opens it for reading and writing, shared, and never changes its size.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board_net.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace fzgx::board
{
bool Mapping::Open(const std::string& path, std::string* error)
{
  Close();
  const std::filesystem::path p(std::u8string(path.begin(), path.end()));
#ifdef _WIN32
  HANDLE file = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
  {
    *error = "the file cannot be opened (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  LARGE_INTEGER size{};
  HANDLE mapping = nullptr;
  void* at = nullptr;
  if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= (1 << 20))
  {
    mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping)
      at = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  }
  if (!at)
  {
    *error = size.QuadPart <= 0 || size.QuadPart > (1 << 20) ? "the file is empty or larger than 1 MiB" :
                                                                "it cannot be mapped (error " + std::to_string(GetLastError()) + ")";
    if (mapping)
      CloseHandle(mapping);
    CloseHandle(file);
    return false;
  }
  m_file = file;
  m_mapping = mapping;
  m_at = at;
  m_size = static_cast<std::size_t>(size.QuadPart);
  return true;
#else
  const int fd = open(p.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0)
  {
    *error = std::string("the file cannot be opened (") + std::strerror(errno) + ")";
    return false;
  }
  struct stat st{};
  void* at = MAP_FAILED;
  if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size <= (1 << 20))
    at = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);  // (the mapping keeps the file)
  if (at == MAP_FAILED)
  {
    *error = st.st_size <= 0 || st.st_size > (1 << 20) ? "the file is empty or larger than 1 MiB" : "it cannot be mapped";
    return false;
  }
  m_at = at;
  m_size = static_cast<std::size_t>(st.st_size);
  return true;
#endif
}

void Mapping::Close()
{
  if (!m_at)
    return;
#ifdef _WIN32
  UnmapViewOfFile(m_at);
  CloseHandle(static_cast<HANDLE>(m_mapping));
  CloseHandle(static_cast<HANDLE>(m_file));
#else
  munmap(m_at, m_size);
#endif
  m_at = nullptr;
  m_size = 0;
  m_file = m_mapping = nullptr;
}

u32 ProcessId()
{
#ifdef _WIN32
  return static_cast<u32>(GetCurrentProcessId());
#else
  return static_cast<u32>(getpid());
#endif
}
}  // namespace fzgx::board
