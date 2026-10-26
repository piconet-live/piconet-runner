// The mod bundle's files (the launch file's "bundle"; protocol/formats.md section 3): the image, the manifest, and the
// native form's patch list or the fork's codes. Read once and checked here; the patch list's words are written into
// the game's code here too. Lifted from the Dolphin fork (dolphin-fzgx Core/FzgxBoot.cpp at 5bdc7c640e).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <cstring>

namespace fzgx::boot
{
namespace
{
std::optional<BundleFiles> s_bundle;
bool s_bundle_read = false;
bool s_patch_refusals_logged = false;

std::optional<std::vector<Code>> ReadCodes(const std::string& path)
{
  std::string text;
  if (!ReadFileToString(path, text))
    return std::nullopt;
  std::vector<Code> codes;
  for (const std::string& raw : SplitString(text, '\n'))
  {
    const std::string line = std::string(StripWhitespace(raw));
    if (line.empty())
      continue;
    if (line[0] == '$')
    {
      Code code;
      code.name = line.substr(1);
      codes.push_back(std::move(code));
      continue;
    }
    const auto words = SplitString(line, ' ');
    std::pair<u32, u32> c;
    if (codes.empty() || words.size() != 2 || !TryParse(words[0], &c.first, 16) ||
        !TryParse(words[1], &c.second, 16))
    {
      Error("fzgx: {}: bad code line \"{}\"", path, line);
      return std::nullopt;
    }
    codes.back().lines.push_back(c);
  }
  return codes;
}

// The native form's list: {"complete": true, "image_sha1": ..., "patches": [{"address", "original", "new", "what"}]},
// every word eight hex digits. A list built without the game's files ("complete": false) names no original for some
// words and is refused: nothing could check the game's code before writing into it.
bool ReadPatches(const std::string& path, const std::string& image_sha1, std::vector<Patch>* out)
{
  std::string text;
  picojson::value root;
  if (!ReadFileToString(path, text) || TooLarge(text) || !picojson::parse(root, text).empty() ||
      !root.is<picojson::object>())
  {
    Error("fzgx: {} is missing or not a patch list", path);
    return false;
  }
  const picojson::object& list = root.get<picojson::object>();
  const auto complete = list.find("complete");
  const auto patches = list.find("patches");
  if (complete == list.end() || !complete->second.is<bool>() || !complete->second.get<bool>() ||
      patches == list.end() || !patches->second.is<picojson::array>())
  {
    Error("fzgx: {} was built without the game's files (incomplete): build the bundle with them", path);
    return false;
  }
  if (Text(list, "image_sha1") != image_sha1)
  {
    Error("fzgx: {} belongs to another image ({}) than this bundle's mod.bin ({})", path, Text(list, "image_sha1"),
          image_sha1);
    return false;
  }
  for (const picojson::value& entry : patches->second.get<picojson::array>())
  {
    Patch p;
    if (!entry.is<picojson::object>())
      return false;
    const picojson::object& o = entry.get<picojson::object>();
    if (!TryParse(Text(o, "address"), &p.address, 16) || !TryParse(Text(o, "original"), &p.original, 16) ||
        !TryParse(Text(o, "new"), &p.replacement, 16) || (p.address & 3) != 0)
    {
      Error("fzgx: {}: a bad entry (address \"{}\")", path, Text(o, "address"));
      return false;
    }
    p.what = Text(o, "what");
    out->push_back(std::move(p));
  }
  std::sort(out->begin(), out->end(), [](const Patch& x, const Patch& y) { return x.address < y.address; });
  return true;
}
}  // namespace

// The bundle's files, read once.
const BundleFiles* TheBundle()
{
  if (s_bundle_read)
    return s_bundle ? &*s_bundle : nullptr;
  s_bundle_read = true;
  if (TheOptions().bundle_dir.empty())
    return nullptr;
  const std::string dir = TheOptions().bundle_dir + "/";
  BundleFiles b;
  File image(dir + "mod.bin", "rb");
  b.image.resize(image.GetSize());
  if (!image.IsOpen() || b.image.size() < 0x40 || !image.ReadBytes(b.image.data(), b.image.size()) ||
      std::memcmp(b.image.data(), "FZMD", 4) != 0 || std::memcmp(b.image.data() + 0x10, "FZSI", 4) != 0 ||
      b.image.size() > layout::IMAGE_LIMIT - layout::IMAGE)
  {
    Error("fzgx: the mod bundle in {} is missing or broken", dir);
    return nullptr;
  }
  b.image_sha1 = Sha1Hex(b.image.data(), b.image.size());
  std::string manifest;
  picojson::value mv;
  if (ReadFileToString(dir + "manifest.json", manifest) && !TooLarge(manifest) &&
      picojson::parse(mv, manifest).empty() && mv.is<picojson::object>())
  {
    b.commit = Text(mv.get<picojson::object>(), "commit");
    b.native = Text(mv.get<picojson::object>(), "form") == "native";
  }
  if (b.native)
  {
    if (!ReadPatches(dir + "patches.json", b.image_sha1, &b.patches))
      return nullptr;
  }
  else
  {
    std::optional<std::vector<Code>> codes = ReadCodes(dir + "codes.txt");
    if (!codes)
    {
      Error("fzgx: the mod bundle in {} is missing or broken", dir);
      return nullptr;
    }
    b.codes = std::move(*codes);
  }
  s_bundle = std::move(b);
  return &*s_bundle;
}

void ResetBundle()
{
  s_bundle_read = false;
  s_bundle.reset();
  s_patch_refusals_logged = false;
}

// The bundle for one race (a replay plays as the race too): the fork's codes or the native form's mode word, and the
// seat routines' slot: this player's seat, the player seats (rubber-band references), the input delay.
bool Bundle(Match& m)
{
  const BundleFiles* b = TheBundle();
  if (!b)
    return false;
  m.image = b->image;
  m.image_sha1 = b->image_sha1;
  m.bundle_commit = b->commit;
  m.codes = b->codes;
  m.mode = 1;
  // a fork bundle made before 0.3.0 also holds the game's replay mode's boot, which never runs
  std::erase_if(m.codes, [](const Code& c) { return c.name == "Replay boot"; });
  u8* slot = m.image.data() + (SLOT - IMAGE);
  Put32(slot + 4, static_cast<u32>(m.seat));
  Put32(slot + 32, 0);
  Put32(slot + 36, 1);
  Put32(slot + 40, m.player_mask);
  Put32(slot + 44, m.delay);
  return true;
}

bool IsNative()
{
  const BundleFiles* b = TheBundle();
  return b != nullptr && b->native;
}

const std::vector<Patch>& Patches()
{
  static const std::vector<Patch> none;
  const BundleFiles* b = TheBundle();
  return b != nullptr && b->native ? b->patches : none;
}

// The list's words into the game's code: a word that holds the stock game's is written (and the host told), one
// that holds the new word already is counted, anything else is left alone and named in the log. The executable's
// words are in the list too: the copy of the executable that is booted has them already, a saved state made
// from another copy does not.
PatchReport ApplyPatches(Guest& g)
{
  PatchReport report;
  const bool force = TheOptions().debug.force_patches;
  for (const Patch& p : Patches())
  {
    if (!g.Ram(p.address, 4))
      continue;
    const u32 found = g.Read32(p.address);
    if (found == p.replacement)
    {
      ++report.already;
      continue;
    }
    if (found != p.original)
    {
      ++report.refused;
      if (!s_patch_refusals_logged || force)
      {
        Error("fzgx: {:08X} ({}) holds {:08X}, neither the stock game's {:08X} nor the patched {:08X}{}", p.address,
              p.what, found, p.original, p.replacement, force ? ": written (forced)" : ": left alone");
      }
      if (!force)
        continue;
    }
    g.Write32(p.address, p.replacement);
    TheHost().CodeChanged(p.address, 4);
    ++report.written;
  }
  if (report.written != 0 || (report.refused != 0 && !s_patch_refusals_logged))
  {
    Notice("fzgx: the game's code: {} words written, {} already patched, {} refused", report.written, report.already,
           report.refused);
  }
  s_patch_refusals_logged = s_patch_refusals_logged || report.refused != 0;
  return report;
}
}  // namespace fzgx::boot
