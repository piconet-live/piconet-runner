// Tests of the player's controller options, the reader of the game's save file (files_save.cpp): the options of a
// save made here (save_files.h), and every save that is refused. A race's frame tick: test_save_race.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "save_files.h"

using namespace save_files;

namespace
{
// TestASaveThatIsRefused's step: a save file that is there and cannot be had. Each case says what happened: not
// "no save file" for a file that does not open, and not a size for a file of the right size whose read fails.
void AFileThatDoesNotGiveItsBytes(const std::string& dir, const std::vector<u8>& good)
{
  const std::string path = dir + "/" + boot::SaveFileName();
  WriteBytes(path, good);
  CHECK(boot::ReadSavedOptions(dir).options.has_value());
  {
    // of the right size, and its bytes cannot be read: opened here for writing only, as a read fault stands in
    File unreadable(path, "ab");
    CHECK(unreadable.IsOpen() && unreadable.GetSize() == good.size());
    const boot::SavedOptions saved = boot::OptionsOfFile(unreadable, path);
    CHECK(!saved.options && Says(saved.refused, path + ": it could not be read (a read error, or another program holds the file)"));
    CHECK(!Says(saved.refused, "bytes"));
  }
  // there, and it does not open (no permission stands in for another program's lock; an administrator's run opens
  // every file, and has nothing to check here)
  std::filesystem::permissions(path, std::filesystem::perms::none);
  if (!File(path, "rb").IsOpen())
  {
    const boot::SavedOptions saved = boot::ReadSavedOptions(dir);
    CHECK(!saved.options && Says(saved.refused, path + ": it could not be opened (another program holds the file, or it may not be read)"));
    CHECK(!Says(saved.refused, "no save file"));
  }
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  CHECK(boot::ReadSavedOptions(dir).options.has_value());
}

// TestASaveThatIsRefused's step: which file of the folder is the game's save. The emulator's folder card takes every
// .gci file by the directory entry inside it, in the order of the names, and of several files of one save the first
// that loads; the reader takes the file the game would be given.
void TheSaveIsFoundByItsEntry(const std::string& dir)
{
  const std::vector<u8> first = SaveOf(Options0()), second = SaveOf(Options1());
  // what is no save of the game's, whatever its name: another game's save, a file of zeros under the save's own
  // name, a file too short to hold an entry, a file that is no .gci, a folder
  std::vector<u8> other = first;
  std::memcpy(other.data(), "GALE01", 6);
  WriteBytes(dir + "/0-melee.gci", other);
  WriteBytes(dir + "/" + boot::SaveFileName(), std::vector<u8>(100, 0));
  WriteBytes(dir + "/1-short.gci", std::vector<u8>(first.begin(), first.begin() + 63));
  WriteBytes(dir + "/2-f_zero.dat.txt", second);
  std::filesystem::create_directories(dir + "/3-folder.gci");
  boot::SavedOptions saved = boot::ReadSavedOptions(dir);
  CHECK(!saved.options && saved.path.empty() && saved.refused == "no save file of F-Zero GX in " + dir + " (1 of its .gci files could not be read)");
  // a save imported under another name, its ending in capitals
  WriteBytes(dir + "/imported save.GCI", second);
  saved = boot::ReadSavedOptions(dir);
  CHECK(saved.options && *saved.options == Options1() && saved.path == dir + "/imported save.GCI" && saved.left_out.empty());
  // two files of the save: the first by name (by its bytes: a capital letter comes before a small one) is the
  // card's, and the other is named as left out
  WriteBytes(dir + "/z-later.gci", first);
  saved = boot::ReadSavedOptions(dir);
  CHECK(saved.options && *saved.options == Options1() && saved.path == dir + "/imported save.GCI");
  CHECK(saved.left_out == std::vector<std::string>{"z-later.gci"});
  WriteBytes(dir + "/A-earlier.gci", first);
  saved = boot::ReadSavedOptions(dir);
  CHECK(saved.options && *saved.options == Options0() && saved.path == dir + "/A-earlier.gci");
  CHECK((saved.left_out == std::vector<std::string>{"imported save.GCI", "z-later.gci"}));
  // a file the emulator does not put on the card (its size is not its entry's) is passed over for the next
  WriteBytes(dir + "/A-earlier.gci", std::vector<u8>(first.begin(), first.end() - 1));
  std::filesystem::remove(dir + "/z-later.gci");
  saved = boot::ReadSavedOptions(dir);
  CHECK(saved.options && *saved.options == Options1() && saved.path == dir + "/imported save.GCI" && saved.left_out.empty());
  // the first by name is the card's also when it is refused: the game would be given that file
  std::vector<u8> damaged = first;
  damaged[BLOCK + 0x48] ^= 1;
  WriteBytes(dir + "/A-earlier.gci", damaged);
  saved = boot::ReadSavedOptions(dir);
  CHECK(!saved.options && Says(saved.refused, dir + "/A-earlier.gci: its checksum is") && saved.path == dir + "/A-earlier.gci");
  std::filesystem::remove_all(dir);
}

// TestASaveThatIsRefused's step: the stick's calibration. A corner outside its eighth of the circle or nearer than
// the screen starts, and two neighbours that do not go round.
void CornersTheScreenCannotWrite()
{
  const auto corner = [](int port, int corner, int x, int y) {
    return Refusal([=](ControllerOptions& o) {
      o.stick_gate[port * 16 + corner * 2] = static_cast<u8>(x);
      o.stick_gate[port * 16 + corner * 2 + 1] = static_cast<u8>(y);
    });
  };
  CHECK(Says(corner(0, 0, 36, 0), "port 1's stick calibration has corner 1 at 36, 0"));    // nearer than the screen starts
  CHECK(Says(corner(0, 0, 75, 40), "corner 1 at 75, 40"));                                 // off its axis
  CHECK(Says(corner(0, 0, -75, 0), "corner 1 at -75, 0"));                                 // the other side
  CHECK(Says(corner(1, 2, 0, 0), "port 2's stick calibration has corner 3 at 0, 0"));
  CHECK(Says(corner(2, 4, 75, 0), "port 3's stick calibration has corner 5 at 75, 0"));
  CHECK(Says(corner(3, 6, 0, 75), "port 4's stick calibration has corner 7 at 0, 75"));
  CHECK(Says(corner(0, 1, -53, 53), "corner 2 at -53, 53"));                               // another quarter
  CHECK(Says(corner(0, 1, 25, 26), "corner 2 at 25, 26"));                                 // nearer than the screen starts
  CHECK(Says(corner(0, 1, 100, 30), "corner 2 at 100, 30"));                               // on the cardinal's side
  CHECK(Says(corner(0, 3, 0, 75), "corner 4 at 0, 75"));
  CHECK(Says(corner(0, 5, 53, -53), "corner 6 at 53, -53"));
  CHECK(Says(corner(0, 7, 0, 0), "corner 8 at 0, 0"));
  // neighbouring corners at one angle, or the wrong way round: each is where its own eighth of the circle allows it
  // (the two eighths overlap by a hair at 22.5 degrees), and the game's screen puts no two corners so
  const auto corners = [](int port, int first, int x0, int y0, int x1, int y1) {
    return Refusal([=](ControllerOptions& o) {
      u8* gate = o.stick_gate.data() + port * 16;
      gate[first * 2] = static_cast<u8>(x0);
      gate[first * 2 + 1] = static_cast<u8>(y0);
      gate[(first + 1) % 8 * 2] = static_cast<u8>(x1);
      gate[(first + 1) % 8 * 2 + 1] = static_cast<u8>(y1);
    });
  };
  CHECK(corners(0, 0, 60, 24, 60, 25).empty());                                            // a hair apart: taken
  CHECK(Says(corners(0, 0, 60, 24, 60, 24), "port 1's stick calibration has corner 1 at 60, 24 and corner 2 at 60, 24: at one angle"));
  CHECK(Says(corners(0, 0, 60, 25, 60, 24), "corner 1 at 60, 25 and corner 2 at 60, 24"));
  CHECK(Says(corners(0, 0, 120, 50, 60, 25), "corner 1 at 120, 50 and corner 2 at 60, 25"));   // one angle, another reach
  CHECK(Says(corners(2, 7, 60, -24, 60, -25), "port 3's stick calibration has corner 8 at 60, -24 and corner 1 at 60, -25"));
  CHECK(Says(corners(1, 3, -60, 24, -60, 25), "port 2's stick calibration has corner 4 at -60, 24 and corner 5 at -60, 25"));
}
}  // namespace

void TestASavesControllerOptions()
{
  // the game's checksum: the reflected CRC-16 with polynomial 0x8408 from 0xFFFF, inverted (its check value)
  CHECK(boot::SaveChecksum(reinterpret_cast<const u8*>("123456789"), 9) == 0x906E);
  CHECK(boot::SaveFileName() == "8P-GFZE-f_zero.dat.gci");

  // a save's options come back byte for byte, from their places in the file
  const std::vector<u8> file = SaveOf(Options1());
  const boot::SavedOptions saved = boot::OptionsOfSave(file);
  CHECK(saved.options && saved.refused.empty() && *saved.options == Options1());
  CHECK(file[BLOCK + 0x48] == 2 && file[BLOCK + 0x50 + 8 + 2] == 0x02 && file[BLOCK + 0x5556] == 0);
  CHECK(Refusal([](ControllerOptions&) {}).empty());

  // the edges of what the game's screens write
  CHECK(Refusal([](ControllerOptions& o) { o.dead_zones.fill(0); }).empty());
  CHECK(Refusal([](ControllerOptions& o) { o.dead_zones.fill(50); }).empty());
  const auto corner = [](int port, int corner, int x, int y) {
    return [=](ControllerOptions& o) {
      o.stick_gate[port * 16 + corner * 2] = static_cast<u8>(x);
      o.stick_gate[port * 16 + corner * 2 + 1] = static_cast<u8>(y);
    };
  };
  CHECK(Refusal(corner(0, 0, 37, 0)).empty() && Refusal(corner(0, 0, 127, 52)).empty() && Refusal(corner(0, 0, 127, -52)).empty());
  CHECK(Refusal(corner(1, 2, -15, 37)).empty() && Refusal(corner(2, 4, -128, 0)).empty() && Refusal(corner(3, 6, 53, -128)).empty());
  CHECK(Refusal(corner(0, 1, 26, 26)).empty() && Refusal(corner(0, 1, 127, 51)).empty() && Refusal(corner(0, 1, 51, 127)).empty());
  CHECK(Refusal(corner(1, 3, -26, 26)).empty() && Refusal(corner(2, 5, -128, -128)).empty() && Refusal(corner(3, 7, 26, -26)).empty());
  CHECK(Refusal([](ControllerOptions& o) { o.wheel_range = {21, 21, 127, 128, 21, 128, 127, 21}; }).empty());
  CHECK(Refusal([](ControllerOptions& o) { o.wheel_play = {0, 0, 20, 20, 1, 1, 19, 19}; }).empty());
  CHECK(Refusal([](ControllerOptions& o) { o.scheme = {0, 1, 0, 1}; }).empty());
  // every control a set allows, in a 16-bit and a 32-bit entry; the top bit is "nothing"
  const std::array<u16, 3> axis_mask{0x81FF, 0x811F, 0x831F};
  const std::array<u32, 3> button_mask{1u << 31 | 0x3FFF1F, 1u << 31 | 0x303F1F, 1u << 31 | 0xF03F1F};
  bool all_taken = true, others_refused = true;
  for (int set = 0; set < 3; ++set)
  {
    for (int bit = 0; bit < 32; ++bit)
    {
      const std::string wide = Refusal([&](ControllerOptions& o) { boot::Put32(o.bindings.data() + 0x54 + set * 0x1C + 24, 1u << bit); });
      ((button_mask[set] >> bit & 1) != 0 ? all_taken : others_refused) &= wide.empty() == ((button_mask[set] >> bit & 1) != 0);
      if (bit > 15)
        continue;
      const std::string narrow = Refusal([&](ControllerOptions& o) { Put16(o.bindings.data() + 3 * 0x54 + set * 0x1C + 6, static_cast<u16>(1u << bit)); });
      ((axis_mask[set] >> bit & 1) != 0 ? all_taken : others_refused) &= narrow.empty() == ((axis_mask[set] >> bit & 1) != 0);
    }
  }
  CHECK(all_taken);
  CHECK(others_refused);

  // from the host's folder
  const std::string dir = Temp();
  WriteBytes(dir + "/" + boot::SaveFileName(), file);
  const boot::SavedOptions read = boot::ReadSavedOptions(dir);
  CHECK(read.options && *read.options == Options1());
  std::filesystem::remove_all(dir);
}

void TestASaveThatIsRefused()
{
  const auto refused = [](std::vector<u8> file, const std::string& why) {
    const boot::SavedOptions saved = boot::OptionsOfSave(file);
    return !saved.options && Says(saved.refused, why);
  };
  const std::vector<u8> good = SaveOf(Options1());
  const auto with = [&](std::size_t at, u8 value, bool seal) {
    std::vector<u8> file = good;
    file[at] = value;
    if (seal)
      Seal(file);
    return file;
  };
  // not the game's save: by its size, and by its directory entry (game, maker, file name, blocks)
  CHECK(refused({}, "it is 0 bytes, and the game's save is 32832"));
  CHECK(refused(std::vector<u8>(good.begin(), good.end() - 1), "it is 32831 bytes"));
  std::vector<u8> longer = good;
  longer.push_back(0);
  CHECK(refused(longer, "it is 32833 bytes"));
  CHECK(refused(with(3, 'P', true), "it is not the save of F-Zero GX (GFZE01)"));    // the game of another region
  CHECK(refused(with(4, '0', true), "it is not the save of F-Zero GX"));
  CHECK(refused(with(8, 'g', true), "it is not the save of F-Zero GX"));
  CHECK(refused(with(8 + 10, '2', true), "it is not the save of F-Zero GX"));         // f_zero.dat2
  CHECK(refused(with(0x39, 5, true), "it is not the save of F-Zero GX"));
  // the game's own check: a byte of the save changed, anywhere from its third byte on, or the checksum itself
  CHECK(refused(with(BLOCK + 0x48, 3, false), "the game would call it damaged"));
  CHECK(refused(with(ENTRY + 2, 0, false), "the game would call it damaged"));
  CHECK(refused(with(ENTRY + SAVE - 1, 1, false), "the game would call it damaged"));
  CHECK(refused(with(ENTRY, static_cast<u8>(good[ENTRY] ^ 0x80), false), "its checksum is"));
  CHECK(!refused(with(0x28, 9, false), ""));   // the directory entry's time is no part of the save
  // another version of the save
  CHECK(refused(with(ENTRY + 3, 10, true), "its version is 10, and the game's is 11"));

  // values the game's Options cannot give, in any port. The dead zone first: the input layer divides by 100 less it
  CHECK(Says(Refusal([](ControllerOptions& o) { o.dead_zones[0] = 100; }), "port 1's dead zone is 100 (the game sets 0 to 50)"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.dead_zones[7] = 51; }), "port 4's dead zone is 51"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.dead_zones[3] = 255; }), "port 2's dead zone is 255"));
  CornersTheScreenCannotWrite();
  CHECK(Says(Refusal([](ControllerOptions& o) { o.wheel_range[0] = 20; }), "port 1's steering wheel range is 20 and 120"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.wheel_range[2] = 128; }), "port 2's steering wheel range is 128 and 120"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.wheel_range[7] = 129; }), "port 4's steering wheel range is 120 and 129"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.wheel_play[4] = o.wheel_play[5] = 21; }), "port 3's steering wheel play is 21 and 21"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.wheel_play[1] = 7; }), "port 1's steering wheel play is 6 and 7"));
  CHECK(Says(Refusal([](ControllerOptions& o) { o.scheme[2] = 2; }), "port 3's control scheme is 2 (the game sets 0 or 1)"));
  // a binding of two controls, of none, and of a control the set does not allow
  CHECK(Says(Refusal([](ControllerOptions& o) { boot::Put32(o.bindings.data() + 8, 0x300); }),
             "port 1's button settings (set 1, entry 5) hold 00000300, which names no one control"));
  CHECK(Says(Refusal([](ControllerOptions& o) { boot::Put32(o.bindings.data() + 3 * 0x54 + 2 * 0x1C + 24, 0); }),
             "port 4's button settings (set 3, entry 9) hold 00000000"));
  CHECK(Says(Refusal([](ControllerOptions& o) { boot::Put32(o.bindings.data() + 0x1C + 12, 0x4000); }),
             "port 1's button settings (set 2, entry 6) hold 00004000"));
  CHECK(Says(Refusal([](ControllerOptions& o) { Put16(o.bindings.data() + 0x54, 0x0030); }),
             "port 2's button settings (set 1, entry 1) hold 0030"));
  CHECK(Says(Refusal([](ControllerOptions& o) { Put16(o.bindings.data() + 0x1C + 6, 0x0020); }),
             "port 1's button settings (set 2, entry 4) hold 0020"));
  CHECK(Says(Refusal([](ControllerOptions& o) { Put16(o.bindings.data() + 4, 0); }), "(set 1, entry 3) hold 0000"));

  // from the host's folder: no folder, no file, a file of another size (never read into memory)
  CHECK(Says(boot::ReadSavedOptions("").refused, "the memory card is not a folder of save files here"));
  const std::string dir = Temp();
  CHECK(boot::ReadSavedOptions(dir).refused == "no save file of F-Zero GX in " + dir);
  WriteBytes(dir + "/" + boot::SaveFileName(), std::vector<u8>(good.begin(), good.begin() + 100));
  CHECK(Says(boot::ReadSavedOptions(dir).refused, dir + "/8P-GFZE-f_zero.dat.gci: it is 100 bytes and its directory entry names 4 "
                                                        "blocks of 8192: the emulator does not put such a file on the card"));
  WriteBytes(dir + "/" + boot::SaveFileName(), with(BLOCK + 0x60, 9, false));
  const boot::SavedOptions damaged = boot::ReadSavedOptions(dir);
  CHECK(!damaged.options && Says(damaged.refused, dir + "/8P-GFZE-f_zero.dat.gci: its checksum is"));
  AFileThatDoesNotGiveItsBytes(dir, good);
  TheSaveIsFoundByItsEntry(Temp());
  std::filesystem::remove_all(dir);
}

// Damaged saves, a few thousand of them: a good save with bytes changed at random (one byte or a run of them, with
// the checksum written again or not, or the file cut or lengthened). The reader may refuse or take each; what it
// takes must be options the game's screens can write, by a second, plainer statement of the ranges than the
// reader's own, and a change outside the three things it reads (the entry, the version, the six blocks) must leave
// the options as they were.
void TestSavesDamagedAtRandom()
{
  const ControllerOptions good = Options1();
  const std::vector<u8> file = SaveOf(good);
  const auto in_blocks = [](std::size_t at) {
    const std::array<std::pair<std::size_t, std::size_t>, 6> blocks{{{0x8, 0x40}, {0x48, 8}, {0x50, 0x150}, {0x5542, 8}, {0x554A, 8}, {0x5556, 4}}};
    return std::any_of(blocks.begin(), blocks.end(), [&](const auto& b) { return at >= BLOCK + b.first && at < BLOCK + b.first + b.second; });
  };
  std::mt19937 rng(20261010);
  int taken = 0, refused = 0, unsound = 0, moved = 0;
  for (int round = 0; round < 4000; ++round)
  {
    std::vector<u8> damaged = file;
    bool outside = true;   // every changed byte is outside what the reader reads
    const int kind = static_cast<int>(rng() % 8);
    if (kind == 0)
      damaged.resize(rng() % (damaged.size() + 64));
    else
    {
      // most changes go into the six blocks, where a value decides; the others anywhere in the file
      const std::size_t run = kind == 1 ? 1 + rng() % 64 : 1;
      std::size_t at = rng() % damaged.size();
      if (kind >= 4)
        at = BLOCK + (kind == 4 ? 0x8 + rng() % 0x198 : 0x5542 + rng() % 0x18);
      for (std::size_t i = at; i < std::min(at + run, damaged.size()); ++i)
      {
        damaged[i] = kind == 7 ? static_cast<u8>(damaged[i] ^ (1u << (rng() % 8))) : static_cast<u8>(rng());
        outside = outside && i >= ENTRY + 4 && !in_blocks(i);
      }
      if (rng() % 4 != 0)
        Seal(damaged);
    }
    const boot::SavedOptions saved = boot::OptionsOfSave(damaged);
    if (!saved.options)
    {
      ++refused;
      unsound += saved.refused.empty() ? 1 : 0;   // a refusal says why
      continue;
    }
    ++taken;
    const ControllerOptions& o = *saved.options;
    bool sound = damaged.size() == file.size();
    for (int port = 0; port < 4; ++port)
    {
      sound = sound && o.dead_zones[port * 2] <= 50 && o.dead_zones[port * 2 + 1] <= 50 && o.scheme[port] <= 1;
      sound = sound && o.wheel_play[port * 2] <= 20 && o.wheel_play[port * 2] == o.wheel_play[port * 2 + 1];
      sound = sound && o.wheel_range[port * 2] >= 21 && o.wheel_range[port * 2] <= 127 && o.wheel_range[port * 2 + 1] >= 21 &&
              o.wheel_range[port * 2 + 1] <= 128;
      for (int entry = 0; entry < 3 * 9; ++entry)
      {
        const u8* set = o.bindings.data() + port * 0x54 + entry / 9 * 0x1C;
        const u32 binding = entry % 9 < 4 ? paint::Be16(set + entry % 9 * 2) : paint::Be32(set + 8 + (entry % 9 - 4) * 4);
        sound = sound && std::popcount(binding) == 1;
      }
      for (int corner = 0; corner < 8; ++corner)
      {
        // a corner well away from the middle, and the next one counter-clockwise of it
        const signed char* a = reinterpret_cast<const signed char*>(o.stick_gate.data()) + port * 16 + corner * 2;
        const signed char* b = reinterpret_cast<const signed char*>(o.stick_gate.data()) + port * 16 + (corner + 1) % 8 * 2;
        sound = sound && a[0] * a[0] + a[1] * a[1] >= 26 * 26 && a[0] * b[1] - a[1] * b[0] > 0;
      }
    }
    unsound += sound ? 0 : 1;
    moved += outside && !(o == good) ? 1 : 0;
  }
  CHECK(unsound == 0);
  CHECK(moved == 0);
  // the rounds met both answers in numbers: the test is of the reader, not of one path through it
  CHECK(taken > 200 && refused > 1000);
}
