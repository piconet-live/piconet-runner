// A mod pack's files over the extracted disc (the launch file's "overlay"; fzgx/mod_files.h says which the runner
// takes). Nothing is copied and the game folder is not written: the disc the runner boots is the game folder as a
// DirectoryBlob, and Dolphin's own file patches (the ones its Riivolution support applies, DiscIO/RiivolutionPatcher)
// put each chosen file in its place in that disc's file table, of whatever size, read from the pack's file whenever
// the game reads it. A start without an overlay boots exactly as before: no patch is added.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter.h"
#include "adapter_internal.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Common/FileUtil.h"
#include "Core/Boot/Boot.h"
#include "DiscIO/RiivolutionParser.h"
#include "DiscIO/RiivolutionPatcher.h"

#include "fzgx/mod_files.h"

namespace fzgx_port
{
namespace
{
std::filesystem::path PathOf(const std::string& utf8)
{
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

bool SamePath(std::string_view a, std::string_view b)
{
  return a.size() == b.size() && std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
           return std::tolower(x) == std::tolower(y);
         });
}

// The patches' file loader for a pack's files: each is named by its whole path, as the launch file gives it (the
// loader Dolphin has, FileDataLoaderHostFS, reads paths relative to a Riivolution XML or a virtual SD card).
class WholePaths final : public DiscIO::Riivolution::FileDataLoader
{
public:
  std::optional<u64> GetExternalFileSize(std::string_view path) override
  {
    const ::File::FileInfo info{std::string(path)};
    return info.IsFile() ? std::optional<u64>(info.GetSize()) : std::nullopt;
  }
  std::vector<u8> GetFileContents(std::string_view path) override
  {
    std::string bytes;
    return ::File::ReadFileToString(std::string(path), bytes) ? std::vector<u8>(bytes.begin(), bytes.end()) :
                                                                std::vector<u8>();
  }
  std::vector<Node> GetFolderContents(std::string_view) override { return {}; }  // files only: no folder patches
  DiscIO::BuilderContentSource MakeContentSource(std::string_view path, u64 external_offset, u64 external_size,
                                                 u64 disc_offset) override
  {
    return DiscIO::BuilderContentSource{disc_offset, external_size,
                                        DiscIO::ContentFile{std::string(path), external_offset}};
  }
  std::optional<std::string> ResolveSavegameRedirectPath(std::string_view) override { return std::nullopt; }
};
}  // namespace

void ChooseModFiles()
{
  const fzgx::Options& o = s_state.options;
  s_state.mod_files.clear();
  if (o.overlay.empty())
    return;
  // the offline game: a start with no race and no playback ("Play F-Zero GX"); every other start is a race's
  const bool offline_game = o.match.empty() && o.play_file.empty();
  const std::filesystem::path files = s_state.game_root / "files";
  const fzgx::mod_files::Plan plan = fzgx::mod_files::Choose(
      o.overlay, offline_game,
      [&](const std::string& disc) {
        std::error_code e;
        return std::filesystem::is_regular_file(files / PathOf(disc), e);
      },
      [](const std::string& from) {
        std::error_code e;
        return std::filesystem::is_regular_file(PathOf(from), e);
      });
  for (const std::string& refused : plan.refused)
    s_core_host.Log(fzgx::LogLevel::Warning, "fzgx: a mod pack's file is refused: " + refused);
  s_core_host.Log(fzgx::LogLevel::Notice, fzgx::mod_files::Summary(plan, offline_game));
  s_state.mod_files = plan.files;
}

void BeforeDisc(BootParameters& boot)
{
  if (!InUse() || s_state.mod_files.empty())
    return;
  DiscIO::Riivolution::Patch patch;
  patch.m_id = "piconet-mod-files";
  patch.m_file_data_loader = std::make_shared<WholePaths>();
  for (const fzgx::OverlayFile& file : s_state.mod_files)
  {
    DiscIO::Riivolution::File replace;
    replace.m_disc = "/" + file.disc;  // a path from the disc's root: that file, never the first of its name
    replace.m_external = file.from;
    replace.m_resize = true;   // the file is the pack's, of its own size
    replace.m_create = false;  // only files the disc has (Choose looked)
    patch.m_file_patches.push_back(std::move(replace));
  }
  AddRiivolutionPatches(&boot, {std::move(patch)});
}

std::filesystem::path GameFilePath(const std::string& path)
{
  for (const fzgx::OverlayFile& file : s_state.mod_files)
  {
    if (SamePath(file.disc, path))
      return PathOf(file.from);
  }
  return s_state.game_root / "files" / PathOf(path);
}
}  // namespace fzgx_port
