// What the core reads from files, as the readers give it: the race of a launch file or of a replay (Match), the mod
// bundle (BundleFiles), and a machine's stock texture files from the disc. The readers are files_*.cpp, and no
// other file of the core opens or parses a file: what comes from outside is checked there, in its range or refused,
// and the rest of the core takes it as it is. The formats: protocol/formats.md.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "game_facts.h"
#include "internal.h"
#include "mode.h"
#include "picojson.h"
#include "race_rules.g.h"   // the host's rules the games apply (protocol/rules.toml): their words
#include "replay_file.g.h"  // the replay file (protocol/replay.toml): its versions

namespace fzgx::boot
{
// Mod memory (port/image/routines/memmap.py; the bundle's manifest.json repeats what the fork checks).
constexpr u32 IMAGE = table::IMAGE;    // the mod image, 'FZMD'
constexpr u32 SLOT = table::SLOT;      // the seat routines' slot 'FZSI': +4 seat, +32 off, +36 uniform, +40 player
                                       // seats (bit i = seat i), +44 input delay
constexpr u32 NAMES = table::NAMES;    // 30 x 32 bytes, NUL-terminated, seat order; empty: the pilot's name
constexpr u32 NAME_SIZE = 32;
constexpr u32 RACE = table::RACE;      // 'FZRC', then words: +4 course, +8 port 0's machine, +12 CPU opponents,
                                       // +16 laps, +20 race type, +24 shuffle, +28 CPU seed; +32 30 machine bytes
constexpr u32 RACE_SIZE = 1280;
// the seats' custom machines (garage-built): 30 entries of 32 bytes, seat order (memmap.py RACE_CUSTOM). +0 a word,
// 0xC0000000 when the seat races its own parts; +4, +12, +20 the body's, cockpit's and booster's part id (a byte)
// and +8, +16, +24 each part's colour word (red, green, blue, the material byte): a garage record's three part
// entries; +28 the pilot (the stock machine 0-40 whose pilot drives it). A custom seat's machine is CUSTOM_MACHINE.
constexpr u32 RACE_CUSTOM = 320, CUSTOM_ENTRY = 32;
constexpr u32 CUSTOM_MACHINE = 0x32;
// the host's rules the games apply (protocol/rules.toml: their words, ranges and reader, race_rules.g.h): the CPU
// level, boost from the start, restore, the race's end for everyone so long after the first finish (in steps; the
// image's raceend.py; 0: no such end), a mine's coming back so long after it went off (in steps; the image's
// sites_mines.py; 0: a mine stays gone, the game's own way, and every recording made before the rule). A match start
// names the finish limit in seconds and the mines' delay in milliseconds, which are whole steps.
constexpr u32 RACE_CPULEVEL = race_rules::RACE_CPULEVEL, RACE_BOOST = race_rules::RACE_BOOST,
              RACE_RESTORE = race_rules::RACE_RESTORE, RACE_FINISH_LIMIT = race_rules::RACE_FINISH_LIMIT,
              RACE_MINE_REGEN = race_rules::RACE_MINE_REGEN;
constexpr u32 MAX_FINISH_LIMIT = race_rules::FINISH_LIMIT_MOST;
constexpr u32 MAX_MINE_REGEN_MS = race_rules::MINE_REGEN_MOST, MINE_REGEN_STEP_MS = race_rules::MINE_REGEN_STEP;
static_assert(RACE_SIZE == race_rules::RACE_SIZE);
// every seat's machine setting (bytes)
constexpr u32 RACE_SETTINGS = 268;
// the race's flow: 1, a machine is out by the game's own verdict and the race waits for the player seats alone (the
// image's outtest.py, raceend.py); 0, every recording made before them, which play as they were raced. A race from a
// match is 1; a replay's is its header's "flow" (none: 0).
constexpr u32 RACE_FLOW = 304;
constexpr u32 FLOW_GAME_OUT = 1;
constexpr u32 STEPS_A_SECOND = race_rules::STEPS_A_SECOND;
// the start grid (RaceSetup.Grid): a word, 1 when the race names its grid; then the seat of each slot (30 bytes, pole
// first). 0: the game's own sort (the CPUs ahead of seat 0), every match and recording without one (the image's
// sites_grid.py).
constexpr u32 RACE_GRID_ON = 64, RACE_GRID = 68;
static_assert(RACE_GRID + 30 <= RACE_CPULEVEL && 32 + 30 <= RACE_GRID_ON);
static_assert(MINE_REGEN_STEP_MS * STEPS_A_SECOND % 1000 == 0 && RACE_FLOW + 4 <= RACE_MINE_REGEN &&
              RACE_MINE_REGEN + 4 <= RACE_CUSTOM);
// the rubber band: a word, 1 the CPU drivers pace against the player seats (the game's own way), 0 every driver at
// its own pace (the image's sites_cpu.py). A race or practice from a match is 0 (0.4.0 on); a replay's is its
// header's "rubberBand" (none: 1, every recording made before 0.4.0, which plays as it was raced).
constexpr u32 RACE_RUBBER_BAND_ON = 100;
static_assert(RACE_GRID + 30 <= RACE_RUBBER_BAND_ON && RACE_RUBBER_BAND_ON + 4 <= RACE_CPULEVEL);
// the simulation's own generator: a word, 1 the race's random draws (the computer drivers' scripted driver, the hazard
// objects) take a word of their own that the image seeds before every step, 0 they take the executable's 0x801A63C0,
// which the effects and cameras draw from too (the image's sites_generator.py). A race or practice from a match is 1;
// a replay's is its header's "simGenerator" (none: 0, every recording made before it, which plays as it was raced).
constexpr u32 RACE_SIM_GENERATOR = 104;
static_assert(RACE_RUBBER_BAND_ON + 4 <= RACE_SIM_GENERATOR && RACE_SIM_GENERATOR + 4 <= RACE_CPULEVEL);
constexpr const char* GAME_ID = "GFZE01";  // the disc this core is for (NTSC-U)
// What a race can be made of. A match is the coordinator's word and a replay is a file somebody sent: their numbers
// go to the game's own set-up routines, which check nothing (a machine or a course the disc does not hold, a seat
// outside the race: the game reads past its tables). A set-up outside these is refused, never bent into shape, so a
// race the game is given is the race the file names.
constexpr u32 MAX_SEATS = 30;
constexpr u32 MAX_MACHINE = 40;        // the game's 41 machines (paint.cpp lists them)
constexpr u32 MAX_LAPS = 99;           // two digits, as the game's lap counter shows
constexpr u32 MAX_DELAY = 60;          // input delay in steps: 2 in play
constexpr size_t MAX_REPLAY_BYTES = size_t{256} << 20;  // an hour of 30 seats is 40 MiB
// Steps of a replay: six hours. The session keeps every step's records apart (about 56 bytes a step in memory for
// the 10 a step of one seat takes in the file), so a file of one seat at MAX_REPLAY_BYTES, 26.7 million steps, took
// 1.8 GB to read (security audit 030, G3); six hours of one seat take some 75 MB.
constexpr size_t MAX_REPLAY_STEPS = size_t{6} * 60 * 60 * STEPS_A_SECOND;
constexpr size_t MAX_STAT_NAME = 40;   // characters of a stat pack's name
constexpr size_t MAX_SEAT_PACKS = 4;   // packs a race's seats may race beside its own pack (setup.packs)

// One changed word of the game's car data (handbook/stat-packs.md): the block (0 to 40 a machine by id, 41
// to 115 a custom part), the word's offset in the block, and the 32 bits it is to hold, as the game holds them. Or
// one of the game's code constants: the block file::CONST_BLOCK, and the constant's slot (const_ranges.h).
struct StatEntry
{
  u8 block = 0;
  u8 at = 0;
  u32 value = 0;
};
// A race's stat pack, read and checked (files_stats.cpp): every entry names a word a pack may change and a value in
// its range, and the entries stand by block and offset, each once. The car data's entries and the constants' are
// kept apart; the fingerprint is of both, as they travel (the constants last).
struct StatPack
{
  std::string name;
  std::string fingerprint;  // SHA-1 (hex) of the format byte and the entries as they travel: what names the pack
  std::vector<StatEntry> entries;    // the car data's
  std::vector<StatEntry> constants;  // the code constants' (block file::CONST_BLOCK, by slot); none in a pack of before
};

// no stock colour chosen (the seat keeps what the game loads, or takes its colour)
inline std::array<int, 30> MakeNoStock()
{
  std::array<int, 30> stock;
  stock.fill(-1);
  return stock;
}

struct Match
{
  picojson::object json;  // the coordinator's MatchStart
  Relay relay;
  int seat = 0;
  u32 player_mask = 0;
  u32 delay = 2;
  std::array<u8, RACE_SIZE> race{};
  std::array<u8, 30 * NAME_SIZE> names{};
  std::vector<u8> image;
  std::string image_sha1;
  std::string bundle_commit;
  std::vector<Code> codes;
  std::array<std::optional<paint::Rgb>, 30> colors{};
  std::array<int, 30> stock = MakeNoStock();
  u32 other_sounds = 132;  // "quieter" (0.51)
  bool boost_from_start = false;
  u32 mode = 1;  // the native form's mode word: 1 a race (or a replay, played as the race)
  std::optional<StatPack> stats;  // the race's stat pack ("setup.stats"); none: the disc's own car data
  // A pack a seat (handbook/stat-pack-constants.md, "A pack a seat"): the packs a seat may race in place of the
  // race's ("setup.packs"), and each seat's choice ("seats[].pack"): -1 the race's pack, 0 the disc's own, k packs[k-1].
  // A stock machine with a choice of its own races a row of its own; a custom machine takes only its constants.
  std::vector<StatPack> packs;
  std::array<int, 30> seat_pack = MakeNoStock();
  std::vector<u8> seat_block;  // the seats' block as written before the boot (const_pack.cpp SeatBlock): its rows off
  // The race's game mode ("setup.mode": protocol/formats.md section 9; mode.h): its package, read and checked. None:
  // the race of the host's rules. A mode's setup words win over the match's (its laps are the race's).
  std::optional<mode::Package> game_mode;
};

// The mod bundle's files (the launch file's "bundle"). Two kinds: the fork's (mod.bin, codes.txt: Gecko codes, the reference
// configuration) and the native form's (mod.bin, patches.json; manifest.json says "form": "native").
struct BundleFiles
{
  bool native = false;
  std::vector<u8> image;
  std::string image_sha1;
  std::string commit;
  std::vector<Code> codes;
  std::vector<Patch> patches;
};

inline void Put32(u8* p, u32 v)
{
  p[0] = static_cast<u8>(v >> 24);
  p[1] = static_cast<u8>(v >> 16);
  p[2] = static_cast<u8>(v >> 8);
  p[3] = static_cast<u8>(v);
}

// files_match.cpp: a race's match, and the race's seats and set-up from a match start (a replay's header holds one
// too). The helpers read a member of JSON that somebody else wrote.
bool TooDeep(const std::string& json, int limit = 32);
// A number the parser would end the program on (1e999): asked before every text of somebody else's is parsed.
bool TooLarge(const std::string& json);
std::optional<u32> Whole(const picojson::object& o, const std::string& key, u32 fallback, u32 low, u32 high);
std::string Text(const picojson::object& o, const std::string& key);
const picojson::object* Object(const picojson::object& o, const std::string& key);
u32 OtherSoundsWord(const std::string& sounds);
bool FromMatch(Match& m, const picojson::object& json, const std::string& path, double default_setting = 50);
// `text`: the coordinator's match start as JSON (Options::match); `path`: the launch file it came in, for the log.
std::optional<Match> ReadMatch(const std::string& text, const std::string& path);
// files_stats.cpp: the stat pack of a race's set-up. False: the set-up names one and it is refused (the log says
// which rule); true with nothing in `out`: it names none.
bool StatsOfSetup(const picojson::object& setup, const std::string& path, std::optional<StatPack>* out);
// const_pack.cpp: the seats' block of a race (table::SEAT_BLOCK): every seat's constants, the pack its choice names
// or the game's own; the rows' bytes 0 (they are turned on by the frame tick, once the rows are made). Whether a race
// names a pack of a seat's own: a replay of it is version 6.
std::vector<u8> SeatBlock(const Match& m);
// The energy boost's length a pack gives (its BoostLengthBase and BoostLengthScale, or the game's 0 and 60), for the
// longest boost duration its car data gives a machine or a custom one (three parts summed): why it may not stand, as
// a sentence (more than the 255 frames the count's byte holds); nothing: it may, and so may every pack that holds
// neither constant (it races the game's int(60 x D)). `disc` is the disc's boost duration of every block (the car
// data's word +0x38, bits), the pack's own values laid over them. Launcher and server: StatPackConstants.
std::optional<std::string> BoostLengthRefusal(const StatPack& pack, const std::array<u32, file::CAR_MACHINES + file::CAR_PARTS>& disc);
bool HasSeatPacks(const Match& m);
// Whether a race starts on the coordinator's grid (setup.grid, RACE_GRID_ON): a replay of it is version 7.
bool HasGrid(const Match& m);
// Whether a race is 0.4.0's set-up, a replay of which is version 7: a start grid, or no rubber band
// (RACE_RUBBER_BAND_ON 0: every race and practice from 0.4.0).
bool OfVersion7(const Match& m);
// files_stats.cpp: "setup.packs", the packs the seats may choose; false: refused (the log says why).
bool PacksOfSetup(const picojson::object& setup, const std::string& path, std::vector<StatPack>* out);
// A pack's fingerprint, from its values as they travel (6 bytes an entry).
std::string StatFingerprint(const std::vector<u8>& values);
// A recording's version is one this core plays (2 to 7 and 9: replay.toml's versions, each the same file under another
// number; a compact file, version 8, is read as the recording it was made of: files_replay.cpp ReadReplayFile).
constexpr bool PlayedVersion(u32 version)
{
  return replay::Played(version);  // protocol/replay.toml's versions
}
// A seat's custom machine (files_match.cpp) into its entry of the race block; false: not one the game can build.
bool CustomOfSeat(const picojson::object& seat, u8* entry);
// The race block holds a custom machine for this seat.
inline bool IsCustom(const std::array<u8, RACE_SIZE>& race, u32 seat)
{
  const u8* e = race.data() + RACE_CUSTOM + seat * CUSTOM_ENTRY;
  return seat < MAX_SEATS && (e[0] | e[1] | e[2] | e[3]) != 0;
}
// files_bundle.cpp: the bundle's files, read once (ResetBundle: read again), and the bundle for one race.
const BundleFiles* TheBundle();
void ResetBundle();
bool Bundle(Match& m);
// files_replay.cpp: the race of a replay's playback (its steps: net::ReadRecording, replay.h).
std::optional<Match> ReadReplay(const std::string& path);
// files_save.cpp: the player's own controller options, from their saved game on this computer. The six blocks are
// the game's own, byte for byte as its save holds them and as its load routine (main.rel fn_1_C34F0) copies them
// into memory; each is four ports' worth (protocol/formats.md, section 5).
struct ControllerOptions
{
  std::array<u8, 0x40> stick_gate{};   // a port: 8 corner points of the stick's reach, x and y as signed bytes
  std::array<u8, 8> dead_zones{};      // a port: across, up; 0 to 50
  std::array<u8, 0x150> bindings{};    // a port: three sets (a kind of controller each) of nine bindings
  std::array<u8, 8> wheel_range{};     // a port: to the right, to the left
  std::array<u8, 8> wheel_play{};      // a port: the same value twice, 0 to 20
  std::array<u8, 4> scheme{};          // a port: 0 or 1
  bool operator==(const ControllerOptions&) const = default;
};
// What a save gave: the options, or why not, as one sentence. A save is refused whole: no block of a refused save
// is used.
struct SavedOptions
{
  std::optional<ControllerOptions> options;
  std::string refused;
  std::string path;                   // the file they were read from (ReadSavedOptions); empty: none was chosen
  std::vector<std::string> left_out;  // more files of the folder that hold a save of the game's: the emulator puts
                                      // the first by name on the card and leaves these out, and so does the reader
};
// The name the emulator gives the game's save file when the game makes it (a GCI file: <maker>-<game>-<name>.gci).
// A save imported into the folder may have any name that ends in .gci.
std::string SaveFileName();
// The options of a save file's bytes; and of the save in `card_folder`, the host's folder of saves (empty: none):
// the file the emulator's folder card gives the game, found by the directory entry inside the file.
// Refused: no folder, no file of the game's save, a file that is not this game's save by its size or its directory
// entry, a checksum that is not the bytes' (the game's own check), another version than the game writes, and any
// value the game's Options screens cannot give (a dead zone above 50 among them: the input layer divides by 100 less
// the dead zone).
SavedOptions OptionsOfSave(const std::vector<u8>& file);
SavedOptions ReadSavedOptions(const std::string& card_folder);
// The options of an open file named `path` (for its sentence). Refused, beside what OptionsOfSave refuses: a file
// of another size, which is never read into memory, and a read that fails (a file of the right size that another
// program holds, or a fault of the disk): each says which it was.
SavedOptions OptionsOfFile(File& file, const std::string& path);
// The save's checksum as the game computes it (main.rel fn_1_1563E8) over `size` bytes.
u16 SaveChecksum(const u8* data, std::size_t size);
// boot.cpp: the race this game was started with, read once (Reset: read again); none, or its file was refused: null.
const Match* Get();
}  // namespace fzgx::boot

namespace fzgx::paint
{
using Bytes = std::vector<u8>;

inline u32 Be32(const u8* p)
{
  return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

inline u16 Be16(const u8* p)
{
  return static_cast<u16>((p[0] << 8) | p[1]);
}

struct Slot
{
  u32 format, start, end;
};

// files_disc.cpp: the texture slots of a texture file, and a machine's set of four stock files from the disc.
std::vector<Slot> Slots(const Bytes& data);
std::optional<std::array<Bytes, 4>> ReadSet(const std::string& name, bool main);
}  // namespace fzgx::paint

namespace fzgx::minimap
{
// files_disc.cpp: a course's course-select model from the disc (stage/selstNN.gma.lz), unpacked; empty, and `why`
// says it, when it is not there or does not unpack (fzgx/minimap.h reads it as the road).
std::vector<u8> ReadPreviewFile(u32 course, std::string* why);
// files_disc.cpp: a course's course file from the disc (stage/COLI_COURSENN.lz), unpacked, the same way (fzgx/minimap.h
// ReadStart reads the start line from it).
std::vector<u8> ReadCourseFile(u32 course, std::string* why);
}  // namespace fzgx::minimap
