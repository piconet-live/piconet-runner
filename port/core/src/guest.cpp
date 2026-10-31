// SPDX-License-Identifier: GPL-3.0-or-later

#include "fzgx/guest.h"

#include <chrono>
#include <cstring>

namespace fzgx
{
bool Host::RamGuardable(std::string* why)
{
  *why = "the host does not say every write to main memory goes through its own mapping";
  return false;
}

u64 Host::PageHash(const u8* data, std::size_t size)
{
  // 8-byte words, each mixed into the state with a multiply and a rotate; the tail byte by byte.
  u64 h = 0x9E3779B97F4A7C15ull ^ size;
  std::size_t i = 0;
  for (; i + 8 <= size; i += 8)
  {
    u64 w;
    std::memcpy(&w, data + i, 8);
    h = (h ^ w) * 0xFF51AFD7ED558CCDull;
    h = (h << 29) | (h >> 35);
  }
  for (; i < size; ++i)
    h = (h ^ data[i]) * 0x100000001B3ull;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53ull;
  return h ^ (h >> 33);
}

void Host::Recorder(std::string*, std::string*)
{
}

std::chrono::steady_clock::time_point Host::Now()
{
  return std::chrono::steady_clock::now();
}

u32 Host::ReplayKeys()
{
  return 0;
}

u32 Host::ReplayScrub()
{
  return 0xFFFFFFFF;
}

void Host::ReplayBar(const ScrubBar&)
{
}

void Host::ShowSpectator(const SpectatorShow&)
{
}

SpectatorClick Host::SpectatorClicks()
{
  return {};
}

void Host::ReplayTimeline(const Timeline&)
{
}

void Host::MinimapCourseLoaded(const MinimapCourse&)
{
}

void Host::ShowMinimap(const MinimapShow&)
{
}

void Host::ShowInputs(const InputsShow&)
{
}

bool Host::Pack(const u8*, std::size_t, const u8*, std::size_t, std::vector<u8>*)
{
  return false;
}

bool Host::Unpack(const u8*, std::size_t, const u8*, std::size_t, u8*, std::size_t)
{
  return false;
}

bool Host::ProcessMemory(u64*)
{
  return false;
}

bool Host::ThreadClocks(GameClocks*)
{
  return false;
}

u8* Host::Scratch()
{
  return nullptr;
}

u32 Host::ScratchSize()
{
  return 0;
}

std::string Host::CardFolder()
{
  return {};
}

u32 Host::PictureIntervals(u32*, u32)
{
  return 0;
}

void Host::PictureHeld()
{
}

std::string Host::UserFolder()
{
  return {};
}

void Host::PanelChanged()
{
}

void Host::LoadPace(bool)
{
}

void Host::CodeChanged(u32, u32)
{
}

ModData::ModData() : m_bytes(layout::WINDOW_END - layout::WINDOW, 0)
{
}

u64 ModData::Read(u32 address, unsigned width) const
{
  if (!Contains(address, width))
    return 0;
  const u8* p = m_bytes.data() + (address - layout::WINDOW);
  u64 value = 0;
  for (unsigned i = 0; i < width; ++i)
    value = (value << 8) | p[i];
  return value;
}

void ModData::Write(u32 address, u64 value, unsigned width)
{
  if (!Contains(address, width))
    return;
  u8* p = m_bytes.data() + (address - layout::WINDOW);
  for (unsigned i = width; i > 0; --i)
  {
    p[i - 1] = static_cast<u8>(value);
    value >>= 8;
  }
}

void ModData::CopyTo(u32 address, const void* source, std::size_t size)
{
  if (Contains(address, size))
    std::memcpy(m_bytes.data() + (address - layout::WINDOW), source, size);
}

void ModData::CopyFrom(void* destination, u32 address, std::size_t size) const
{
  if (Contains(address, size))
    std::memcpy(destination, m_bytes.data() + (address - layout::WINDOW), size);
}

void ModData::Clear()
{
  std::memset(m_bytes.data(), 0, m_bytes.size());
}

bool ModData::Restore(const std::vector<u8>& snapshot)
{
  if (snapshot.size() != m_bytes.size())
    return false;
  m_bytes = snapshot;
  return true;
}

u8* Guest::Ram(u32 address, std::size_t size) const
{
  const u32 ram_size = m_host.RamSize();
  if (address < layout::RAM || size > ram_size || address - layout::RAM > ram_size - size)
    return nullptr;
  return m_host.Ram() + (address - layout::RAM);
}

u64 Guest::Read(u32 address, unsigned width) const
{
  if (const u8* p = Ram(address, width))
  {
    u64 value = 0;
    for (unsigned i = 0; i < width; ++i)
      value = (value << 8) | p[i];
    return value;
  }
  return m_data.Read(address, width);
}

void Guest::Write(u32 address, u64 value, unsigned width)
{
  if (u8* p = Ram(address, width))
  {
    for (unsigned i = width; i > 0; --i)
    {
      p[i - 1] = static_cast<u8>(value);
      value >>= 8;
    }
    return;
  }
  m_data.Write(address, value, width);
}

bool Guest::CopyTo(u32 address, const void* source, std::size_t size)
{
  if (u8* p = Ram(address, size))
  {
    std::memcpy(p, source, size);
    return true;
  }
  if (!ModData::Contains(address, size))
    return false;
  m_data.CopyTo(address, source, size);
  return true;
}

bool Guest::CopyFrom(void* destination, u32 address, std::size_t size) const
{
  if (const u8* p = Ram(address, size))
  {
    std::memcpy(destination, p, size);
    return true;
  }
  if (!ModData::Contains(address, size))
    return false;
  m_data.CopyFrom(destination, address, size);
  return true;
}
}  // namespace fzgx
