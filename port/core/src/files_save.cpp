// The player's saved game, read for one thing: the controller options the player set in the game's own Options
// (button settings, the stick's calibration and dead zones, a steering wheel's range and play, the control scheme).
// A start that goes straight into a race never runs the title's memory card sequence, which is what loads them, so
// the core reads them from the save file itself and boot.cpp writes them where the game's load routine would.
//
// The file is the emulator's: one GCI file a save in its folder for the memory card (a 64-byte directory entry, then
// the save's blocks of 8,192 bytes). The game's save is four blocks. What is checked, and where each fact is from:
//   - the checksum: main.rel's load task (fn_1_B03CC) computes fn_1_1563E8 over the save from its third byte on and
//     compares it with the first two; a file that fails is "damaged" to the game. Reflected CRC-16, polynomial
//     0x8408, start 0xFFFF, inverted at the end.
//   - the version: the save's fourth byte against the save type's (11; main.rel's table of save types, .rodata
//     0x4B2C + 0x3E). The same task refuses another.
//   - not the card's serial number: the game also compares four words of the save with the card it is on, and the
//     emulator rewrites those words (and the checksum) in its own copy when it loads the file, so a file from
//     another card is the player's all the same. The file's checksum always fits the file's own bytes.
//   - the place: the save type's block of 0x59E0 bytes is copied to and from the save at 0x2060 (the same table:
//     size at +0x40; fn_1_B03CC, fn_1_AD1E4), and fn_1_C34F0 copies the six blocks out of it at the offsets below.
//   - the ranges: what the two Options screens write (fze.option.rel fn_4_3568 with fn_4_EF8: calibration;
//     fn_4_6720: button settings). The stick's corners are a superset by a hair (the game sorts a reading into a
//     corner by an angle from its own arctangent; here by whole-number ratios just outside 22.5 degrees), and
//     neighbouring corners must go round counter-clockwise, each strictly past the one before.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace fzgx::boot
{
namespace
{
// The emulator's GCI file: a memory card directory entry, then the blocks.
constexpr std::size_t ENTRY = 0x40, BLOCK = 0x2000;
constexpr std::size_t ENTRY_GAME = 0, ENTRY_MAKER = 4, ENTRY_NAME = 8, ENTRY_NAME_SIZE = 32, ENTRY_BLOCKS = 0x38;
constexpr const char* MAKER = "8P";
constexpr const char* SAVE_NAME = "f_zero.dat";
// The game's save.
constexpr std::size_t SAVE_BLOCKS = 4, SAVE_SIZE = SAVE_BLOCKS * BLOCK;
constexpr std::size_t SAVE_CHECKSUM = 0, SAVE_VERSION = 3, SAVE_CHECKED_FROM = 2;
constexpr u8 VERSION = 11;
constexpr std::size_t SAVE_BLOCK = 0x2060, SAVE_BLOCK_SIZE = 0x59E0;
// In the save's block, as fn_1_C34F0 reads them.
constexpr std::size_t AT_STICK_GATE = 0x8, AT_DEAD_ZONES = 0x48, AT_BINDINGS = 0x50, AT_WHEEL_RANGE = 0x5542,
                      AT_WHEEL_PLAY = 0x554A, AT_SCHEME = 0x5556;
static_assert(AT_SCHEME + 4 <= SAVE_BLOCK_SIZE && SAVE_BLOCK + SAVE_BLOCK_SIZE <= SAVE_SIZE);

constexpr int PORTS = 4;
// Calibration (fn_4_EF8, fn_4_3568). The stick: the screen starts from a small gate (cardinal corners 37 out,
// diagonal ones at 26, 26) and moves a corner out to the stick's reading when the reading is farther than the
// corner and lies in the corner's eighth of the circle; the game's stock gate (75; 53, 53) is such a gate too.
constexpr int GATE_CORNERS = 8, GATE_CARDINAL_LEAST = 37, GATE_DIAGONAL_LEAST = 26 * 26 + 26 * 26;
constexpr int DEAD_ZONE_MOST = 50;                      // stepped between 0 and 50
constexpr int WHEEL_RANGE_LEAST = 21, WHEEL_PLAY_MOST = 20;  // a reading above 20 is taken; stepped between 0 and 20
// Button settings (fn_4_6720): a binding is one bit of its set's mask (fze.option.rel .data 0x1520: the four 16-bit
// bindings; 0x1528: the five 32-bit ones). The top bit is "nothing". A set is a kind of controller: the standard
// pad, and two steering wheels.
constexpr int SETS = 3, SET_SIZE = 0x1C, PORT_BINDINGS = SETS * SET_SIZE;
constexpr u32 NOTHING = u32{1} << 31;
constexpr std::array<u16, SETS> AXIS_MASK{0x81FF, 0x811F, 0x831F};
constexpr std::array<u32, SETS> BUTTON_MASK{NOTHING | 0x3FFF1F, NOTHING | 0x303F1F, NOTHING | 0xF03F1F};

bool OneBitOf(u32 value, u32 mask)
{
  return value != 0 && (value & (value - 1)) == 0 && (value & mask) != 0;
}

// A corner of the stick's gate where the calibration screen can have put it. Even corners are the cardinal ones
// (right, up, left, down), odd ones the diagonals between them, counter-clockwise from the right.
bool GateCorner(int corner, int x, int y)
{
  if (corner % 2 == 0)
  {
    const int along = corner % 4 == 0 ? x : y, across = corner % 4 == 0 ? y : x;
    const int out = corner < 4 ? along : -along;
    // within 22.5 degrees of its axis: tan 22.5 is 0.4142, and 5/12 is just above
    return out >= GATE_CARDINAL_LEAST && 12 * std::abs(across) <= 5 * out;
  }
  const bool right = corner == 1 || corner == 7, up = corner == 1 || corner == 3;
  const int ax = right ? x : -x, ay = up ? y : -y;
  if (ax <= 0 || ay <= 0 || ax * ax + ay * ay < GATE_DIAGONAL_LEAST)
    return false;
  // between 22.5 and 67.5 degrees: 2/5 is just below tan 22.5
  return 5 * std::min(ax, ay) >= 2 * std::max(ax, ay);
}

// The first value the Options screens cannot have written, as a sentence; empty: every value is one they can.
std::string NotAnOption(const ControllerOptions& o)
{
  for (int port = 0; port < PORTS; ++port)
  {
    for (int corner = 0; corner < GATE_CORNERS; ++corner)
    {
      const int x = static_cast<signed char>(o.stick_gate[port * 16 + corner * 2]);
      const int y = static_cast<signed char>(o.stick_gate[port * 16 + corner * 2 + 1]);
      if (!GateCorner(corner, x, y))
        return std::format("port {}'s stick calibration has corner {} at {}, {}, where the game's calibration cannot put it",
                           port + 1, corner + 1, x, y);
    }
    // The corners go round counter-clockwise, each strictly past the one before: the screen sorts a reading into
    // one eighth of the circle, so two neighbours never stand at one angle, nor the later one before the earlier.
    // GateCorner's eighths overlap by a hair at their edges (60, 24 passes for the right corner and for the one
    // above it), which let such a pair through.
    for (int corner = 0; corner < GATE_CORNERS; ++corner)
    {
      const u8* a = o.stick_gate.data() + port * 16 + corner * 2;
      const u8* b = o.stick_gate.data() + port * 16 + (corner + 1) % GATE_CORNERS * 2;
      const int ax = static_cast<signed char>(a[0]), ay = static_cast<signed char>(a[1]);
      const int bx = static_cast<signed char>(b[0]), by = static_cast<signed char>(b[1]);
      if (ax * by - ay * bx <= 0)
        return std::format("port {}'s stick calibration has corner {} at {}, {} and corner {} at {}, {}: at one angle, or "
                           "the wrong way round, which the game's calibration cannot give",
                           port + 1, corner + 1, ax, ay, (corner + 1) % GATE_CORNERS + 1, bx, by);
    }
    for (int axis = 0; axis < 2; ++axis)
    {
      if (const int zone = o.dead_zones[port * 2 + axis]; zone > DEAD_ZONE_MOST)
        return std::format("port {}'s dead zone is {} (the game sets 0 to {})", port + 1, zone, DEAD_ZONE_MOST);
    }
    // to the right: a reading of the stick's byte, at most 127; to the left: its size, at most 128
    const int range_right = o.wheel_range[port * 2], range_left = o.wheel_range[port * 2 + 1];
    if (range_right < WHEEL_RANGE_LEAST || range_right > 127 || range_left < WHEEL_RANGE_LEAST || range_left > 128)
      return std::format("port {}'s steering wheel range is {} and {} (the game sets {} to 127 and to 128)", port + 1,
                         range_right, range_left, WHEEL_RANGE_LEAST);
    const int play = o.wheel_play[port * 2];
    if (play > WHEEL_PLAY_MOST || o.wheel_play[port * 2 + 1] != play)
      return std::format("port {}'s steering wheel play is {} and {} (the game sets one value, 0 to {})", port + 1, play,
                         o.wheel_play[port * 2 + 1], WHEEL_PLAY_MOST);
    if (o.scheme[port] > 1)
      return std::format("port {}'s control scheme is {} (the game sets 0 or 1)", port + 1, o.scheme[port]);
    for (int set = 0; set < SETS; ++set)
    {
      const u8* s = o.bindings.data() + port * PORT_BINDINGS + set * SET_SIZE;
      for (int i = 0; i < 4; ++i)
      {
        if (const u16 binding = paint::Be16(s + i * 2); !OneBitOf(binding, AXIS_MASK[set]))
          return std::format("port {}'s button settings (set {}, entry {}) hold {:04X}, which names no one control of "
                             "the game's",
                             port + 1, set + 1, i + 1, binding);
      }
      for (int i = 0; i < 5; ++i)
      {
        if (const u32 binding = paint::Be32(s + 8 + i * 4); !OneBitOf(binding, BUTTON_MASK[set]))
          return std::format("port {}'s button settings (set {}, entry {}) hold {:08X}, which names no one control of "
                             "the game's",
                             port + 1, set + 1, i + 5, binding);
      }
    }
  }
  return {};
}

SavedOptions Refused(std::string why)
{
  SavedOptions saved;
  saved.refused = std::move(why);
  return saved;
}
}  // namespace

std::string SaveFileName()
{
  return std::string(MAKER) + "-" + std::string(GAME_ID).substr(0, 4) + "-" + SAVE_NAME + ".gci";
}

u16 SaveChecksum(const u8* data, std::size_t size)
{
  u16 crc = 0xFFFF;
  for (std::size_t i = 0; i < size; ++i)
  {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = static_cast<u16>((crc & 1) != 0 ? (crc >> 1) ^ 0x8408 : crc >> 1);
  }
  return static_cast<u16>(~crc);
}

SavedOptions OptionsOfSave(const std::vector<u8>& file)
{
  if (file.size() != ENTRY + SAVE_SIZE)
    return Refused(std::format("it is {} bytes, and the game's save is {}", file.size(), ENTRY + SAVE_SIZE));
  const u8* entry = file.data();
  const std::string game(GAME_ID, 4);
  const std::size_t name_size = std::strlen(SAVE_NAME);
  static_assert(sizeof("f_zero.dat") <= ENTRY_NAME_SIZE);
  if (std::memcmp(entry + ENTRY_GAME, game.data(), 4) != 0 || std::memcmp(entry + ENTRY_MAKER, MAKER, 2) != 0 ||
      std::memcmp(entry + ENTRY_NAME, SAVE_NAME, name_size + 1) != 0 || paint::Be16(entry + ENTRY_BLOCKS) != SAVE_BLOCKS)
    return Refused(std::format("it is not the save of F-Zero GX ({}): its directory entry names another game, file or size", GAME_ID));
  const u8* save = file.data() + ENTRY;
  const u16 stored = paint::Be16(save + SAVE_CHECKSUM);
  const u16 computed = SaveChecksum(save + SAVE_CHECKED_FROM, SAVE_SIZE - SAVE_CHECKED_FROM);
  if (stored != computed)
    return Refused(std::format("its checksum is {:04X} and its bytes give {:04X}: the game would call it damaged", stored, computed));
  if (save[SAVE_VERSION] != VERSION)
    return Refused(std::format("its version is {}, and the game's is {}", save[SAVE_VERSION], VERSION));
  const u8* block = save + SAVE_BLOCK;
  ControllerOptions o;
  std::memcpy(o.stick_gate.data(), block + AT_STICK_GATE, o.stick_gate.size());
  std::memcpy(o.dead_zones.data(), block + AT_DEAD_ZONES, o.dead_zones.size());
  std::memcpy(o.bindings.data(), block + AT_BINDINGS, o.bindings.size());
  std::memcpy(o.wheel_range.data(), block + AT_WHEEL_RANGE, o.wheel_range.size());
  std::memcpy(o.wheel_play.data(), block + AT_WHEEL_PLAY, o.wheel_play.size());
  std::memcpy(o.scheme.data(), block + AT_SCHEME, o.scheme.size());
  if (std::string why = NotAnOption(o); !why.empty())
    return Refused(std::move(why));
  SavedOptions saved;
  saved.options = o;
  return saved;
}

SavedOptions OptionsOfFile(File& file, const std::string& path)
{
  // one size is the save's: a file of another is never read into memory
  const std::size_t size = file.GetSize();
  if (size != ENTRY + SAVE_SIZE)
    return Refused(std::format("{}: it is {} bytes, and the game's save is {}", path, size, ENTRY + SAVE_SIZE));
  std::vector<u8> bytes(ENTRY + SAVE_SIZE);
  if (!file.ReadBytes(bytes.data(), bytes.size()))
    return Refused(path + ": it could not be read (a read error, or another program holds the file)");
  SavedOptions saved = OptionsOfSave(bytes);
  if (!saved.options)
    saved.refused = path + ": " + saved.refused;
  return saved;
}

// Which file of the folder is the game's save: the one the emulator's folder card gives the game (its
// GCMemcardDirectory). It takes every file whose name ends in .gci, in the order of their paths' bytes, by the
// directory entry inside the file and not by its name: a save imported under another name is the game's save all the
// same. A file whose entry cannot be read is passed over. Of several files with one identity (game, maker, file
// name) the first that loads is on the card and the others are left out; a file loads when its size is its entry's
// block count (2,043 at most) of 8,192 bytes after the entry.
SavedOptions ReadSavedOptions(const std::string& card_folder)
{
  if (card_folder.empty())
    return Refused("the memory card is not a folder of save files here");
  constexpr std::size_t CARD_BLOCKS_MOST = 2043;
  const std::string game(GAME_ID, 4);
  std::string chosen, unopened, unloaded;
  std::vector<std::string> left_out;
  int closed = 0;
  for (const std::string& name : FolderFiles(card_folder, ".gci"))
  {
    const std::string path = card_folder + "/" + name;
    File file(path, "rb");
    std::array<u8, ENTRY> entry{};
    if (!file.IsOpen() || !file.ReadBytes(entry.data(), entry.size()))
    {
      // whose save it is cannot be said; by the name the emulator gives the game's save, it is likely the player's
      ++closed;
      if (name == SaveFileName() && !file.IsOpen())
        unopened = path;
      continue;
    }
    if (std::memcmp(entry.data() + ENTRY_GAME, game.data(), 4) != 0 || std::memcmp(entry.data() + ENTRY_MAKER, MAKER, 2) != 0 ||
        std::memcmp(entry.data() + ENTRY_NAME, SAVE_NAME, std::strlen(SAVE_NAME) + 1) != 0)
      continue;
    const std::size_t blocks = paint::Be16(entry.data() + ENTRY_BLOCKS), size = file.GetSize();
    if (blocks > CARD_BLOCKS_MOST || size != ENTRY + blocks * BLOCK)
    {
      if (unloaded.empty())
        unloaded = std::format("{}: it is {} bytes and its directory entry names {} blocks of {}: the emulator does not put "
                               "such a file on the card", path, size, blocks, BLOCK);
      continue;
    }
    if (chosen.empty())
      chosen = name;
    else
      left_out.push_back(name);
  }
  if (chosen.empty())
  {
    if (!unopened.empty())
      return Refused(unopened + ": it could not be opened (another program holds the file, or it may not be read)");
    if (!unloaded.empty())
      return Refused(unloaded);
    return Refused("no save file of F-Zero GX in " + card_folder +
                   (closed != 0 ? std::format(" ({} of its .gci files could not be read)", closed) : ""));
  }
  const std::string path = card_folder + "/" + chosen;
  File file(path, "rb");
  SavedOptions saved = file.IsOpen() ? OptionsOfFile(file, path) :
                                       Refused(path + ": it could not be opened (another program holds the file, or it may not be read)");
  saved.path = path;
  saved.left_out = std::move(left_out);
  return saved;
}
}  // namespace fzgx::boot
