// What a race, or a replay's playback, has written into the game: the image and the race's values (before boot in
// the native form, once the game runs in the fork's), and every frame the words of the launcher's options and, in a
// race, the player's own controller options from their saved game. The race comes from a launch file's match or a
// replay file through the readers (files.h). Lifted from the Dolphin fork (dolphin-fzgx Core/FzgxBoot.cpp at
// 5bdc7c640e). A race's stat pack is laid over the game's car data from here too (stat_pack.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <cstring>

namespace fzgx::boot
{
namespace
{
// netframe.py: control block +520, rule flag 0x10 (boosting on the first lap) set from the race's first step
constexpr u32 BOOST_FROM_START = table::BOOST_FROM_START_AT;
// othersound.py: control block +548, the scale (of 256) + 1 for other racers' player-only sounds
constexpr u32 OTHER_SOUNDS = table::OTHER_SOUNDS_AT;
// True widescreen (port/image/sites_view.py): 0, the game's own Wide Screen; a float's bits, the aspect of true
// widescreen's race view. 16/9 is the game's own Wide Screen aspect to the bit (main.rel rodata 0x49F8), so nothing
// that reads the aspect sees another number; the view keeps Normal's whole vertical view.
constexpr u32 TRUE_ASPECT = layout::TRUE_ASPECT;
constexpr float TRUE_WIDESCREEN_ASPECT = 16.0f / 9.0f;
// The game's screen option: a word of main.rel's .bss, by its offset from the module (the first of the OS's module
// list, at 0x800030C8). 0: Normal (4:3); 1 or 2: Wide Screen, which the aspect getter at module + 0xA70C4 turns into
// 16/9 for the projection. The game sets it from the save about 480 fields into a boot and when the player changes
// Options, Screen Mode.
constexpr u32 MODULE_LIST = game::MODULE_LIST;
constexpr u32 SCREEN_OPTION = 0x229630;
// The largest offset from main.rel's header that this file reads or writes at: the header must leave that much of
// main memory after it, or nothing is touched.
constexpr u32 FARTHEST = SCREEN_OPTION + 4;
static_assert(game::OPTION_BINDINGS + 0x150 <= FARTHEST && game::OPTION_SCHEME + 4 <= FARTHEST);

std::optional<Match> s_match;
bool s_read = false;
bool s_painted = false;

// The player's controller options (files_save.cpp), read once from the host's folder of saves; none: the save was
// refused and the game's defaults stay. And how often they had to be written, for the log.
std::optional<ControllerOptions> s_controls;
bool s_controls_read = false;
u32 s_controls_written = 0;

// The player's own controller options in a race. The game loads them from the save in the title's memory card
// sequence (main.rel fn_1_C34F0, called by the title module and by Options), and these starts leave the title in
// its first frame: the save is mounted and never read. So they are written here where fn_1_C34F0 would copy them,
// before the race's first step. Every frame, and only the blocks that differ: when the title module is left for
// the first time its exit routine sets the save's defaults (fn_1_C3378), which puts the button settings back to the
// stock ones (fn_1_3B34), a frame or more after the first write.
// Three of the save's six blocks, the standard controller's: the button settings, the stick's calibration and its
// dead zones. They act on this computer alone: the game's input layer (fn_1_21E4) applies them while it makes
// port 0's 6-byte control record, the record is what the relay carries, and the state checksum covers the
// machines' blocks and progress records, none of these blocks.
// The steering wheel's three are not written (its range, its play, the control scheme): they do not stay on this
// computer. A step copies the scheme to .bss 0xD70 (fn_1_331EC, main.rel +0x33518 and +0x33584) and the machine's
// task branches on it in control modes 2 and 3 (fn_1_83858, +0x83AEC and +0x83B7C), and those modes are a
// steering wheel's: by the code a human machine's control mode (+0x590, a word the checksum covers) is the kind of
// controller in its port (in a match race every machine's port was none and its mode 0 at step 300, with a pad
// and with a wheel: runs ctlw1, ctlw3). A steering wheel is not supported in an online race yet; the runner gives
// a race a standard controller in port 1 (port/moderngekko/src/pins.cpp), where these three have no reader.
// Not in a replay's playback: every seat's records are the recording's.
static_assert(game::OPTION_WHEEL_RANGE + 8 == game::OPTION_WHEEL_PLAY && game::OPTION_WHEEL_PLAY + 8 == game::OPTION_SCHEME);
void WriteControllerOptions(Guest& g)
{
  if (!TheOptions().play_file.empty())
    return;
  if (!s_controls_read)
  {
    s_controls_read = true;
    SavedOptions saved = ReadSavedOptions(TheHost().CardFolder());
    s_controls = saved.options;
    if (s_controls)
    {
      std::string others;
      for (const std::string& name : saved.left_out)
        others += (others.empty() ? "" : ", ") + name;
      // (the file's name alone: the folder's path holds the name of the player's account)
      Notice("fzgx: controller options: the player's own, from {}{}", FileNameAlone(saved.path),
             others.empty() ? "" : " (the folder holds more files of the game's save, which the emulator leaves off the card: " + others + ")");
    }
    else
    {
      // (the refusal names files by their whole paths; in the log the folder goes, as above)
      std::string why = saved.refused;
      const std::string folder = TheHost().CardFolder();
      for (const std::string& part : {folder + "/", folder})
        for (std::size_t at; !folder.empty() && (at = why.find(part)) != std::string::npos;)
          why.replace(at, part.size(), part.size() == folder.size() ? "the card's folder" : "");
      Notice("fzgx: controller options: the game's defaults, the player's are not loaded: {}", why);
    }
  }
  if (!s_controls)
    return;
  const u32 module = MainRel(g);
  if (module == 0)
    return;
  const ControllerOptions& o = *s_controls;
  const struct
  {
    u32 at;
    const u8* bytes;
    std::size_t size;
    const char* name;
  } blocks[] = {
      {game::OPTION_BINDINGS, o.bindings.data(), o.bindings.size(), "button settings"},
      {game::OPTION_STICK_GATE, o.stick_gate.data(), o.stick_gate.size(), "stick calibration"},
      {game::OPTION_DEAD_ZONES, o.dead_zones.data(), o.dead_zones.size(), "dead zones"},
  };
  std::string written;
  for (const auto& block : blocks)
  {
    const u8* held = g.Ram(module + block.at, block.size);
    if (!held || std::memcmp(held, block.bytes, block.size) == 0)
      continue;
    g.CopyTo(module + block.at, block.bytes, block.size);
    written += std::string(written.empty() ? "" : ", ") + block.name;
  }
  // a line for the first writes only: the first, and the one after the title's exit; a game that kept putting its
  // own back would otherwise write a line a frame
  constexpr u32 LINES = 4;
  if (!written.empty() && ++s_controls_written <= LINES)
    Notice("fzgx: controller options written where the game held others: {}{}", written,
           s_controls_written == LINES ? " (no further lines)" : "");
}
}  // namespace

// The head of the system's module list is taken for main.rel only when it is: the loader links fze.sample.rel
// before main.rel in every boot (core.cpp, Linked), and on the reference configuration the frame tick runs from the
// boot's first frame, so the list's head is for a while another module's header, or nothing. main.rel's header
// begins with its module number, and everything this file reads or writes must lie in main memory after it.
u32 MainRel(const Guest& g)
{
  if (!g.Ram(MODULE_LIST, 4))
    return 0;
  const u32 module = g.Read32(MODULE_LIST);
  if (!g.Ram(module, FARTHEST) || g.Read32(module) != game::MAIN_REL_ID)
    return 0;
  return module;
}

const Match* Get()
{
  if (!s_read)
  {
    s_read = true;
    if (const std::string match = TheOptions().match; !match.empty())
      s_match = ReadMatch(match, TheOptions().launch_file);
    else if (const std::string play = TheOptions().play_file; !play.empty())
      s_match = ReadReplay(play);
  }
  return s_match ? &*s_match : nullptr;
}

void Reset()
{
  s_read = false;
  s_match.reset();
  s_painted = false;
  s_controls.reset();
  s_controls_read = false;
  s_controls_written = 0;
  ResetStats();
  ResetOptionsScreen();
  ResetBundle();
}

bool IsActive()
{
  return (!TheOptions().match.empty() || !TheOptions().play_file.empty()) && Get() != nullptr;
}

std::vector<Code> Codes()
{
  const Match* m = Get();
  if (!m)
    return {};
  return m->codes;
}

// The native form, before the game boots: what the game's routines read from the data window is there from the
// first instruction on. The image (its routines are native code; these bytes serve the words they read from their
// own image, the seat routines' slot among them), the mode word every routine at a patched site tests, and for a
// race or a replay the race's values and the names. In the reference configuration OnFrame writes the same things
// into real memory once the game runs.
void Prepare(ModData& data)
{
  const BundleFiles* b = TheBundle();
  if (!b || !b->native)
    return;
  const Match* m = Get();
  const std::vector<u8>& image = m ? m->image : b->image;
  data.CopyTo(IMAGE, image.data(), image.size());
  data.Write(layout::MODE, m ? m->mode : 0, 4);
  if (m)
  {
    data.CopyTo(RACE, m->race.data(), m->race.size());
    data.CopyTo(table::SEAT_BLOCK, m->seat_block.data(), m->seat_block.size());
    data.CopyTo(NAMES, m->names.data(), m->names.size());
  }
  Notice("fzgx: native form: image {} ({} bytes), {} patched words, mode {}", b->image_sha1.substr(0, 8), image.size(),
         b->patches.size(), m ? m->mode : 0);
}

u32 TrueAspectWord(const Options& o)
{
  if (!o.widescreen || !o.true_widescreen)
    return 0;
  u32 bits = 0;
  std::memcpy(&bits, &TRUE_WIDESCREEN_ASPECT, sizeof bits);
  return bits;
}

std::optional<bool> Widescreen(const Guest& g)
{
  const u32 module = MainRel(g);
  if (module == 0)
    return std::nullopt;
  const u32 option = g.Read32(module + SCREEN_OPTION);
  if (option > 2)
    return std::nullopt;
  return option != 0;
}

void OnFrame(Guest& g)
{
  const Match* m = Get();
  if (!m)
    return;
  g.Write32(OTHER_SOUNDS, m->other_sounds);
  g.Write32(BOOST_FROM_START, m->boost_from_start ? 1 : 0);
  // The game's own 16:9 mode: the screen option is set every frame, since these boots never load the player's
  // options. (The fork did this with a Gecko code.)
  if (TheOptions().widescreen)
  {
    if (const u32 module = MainRel(g); module != 0)
      g.Write32(module + SCREEN_OPTION, 1);
  }
  g.Write32(TRUE_ASPECT, TrueAspectWord(TheOptions()));
  WriteControllerOptions(g);
  // the seats' constants every frame until the race runs (in the native form Prepare wrote them before the boot)
  if (g.Read32(table::RUNNING_AT) == 0 && m->seat_block.size() == table::SEAT_BLOCK_SIZE)
    g.CopyTo(table::SEAT_CONSTANTS_AT, m->seat_block.data() + table::SEAT_CONSTANTS, MAX_SEATS * table::SEAT_CONSTANT);
  LaySeatRows(g, *m);  // before the race's pack goes over the car data: a row starts from the disc's block
  if (m->stats)
    LayStats(g, *m->stats);
  // the image is checked by its magic and the slot's seat word, the race block by its magic: the game never writes
  // above its 24 MiB, so this writes once after boot
  if (g.Read32(IMAGE) == 0x465A4D44 &&
      g.Read32(RACE) == 0x465A5243)
  {
    return;
  }
  g.CopyTo(RACE, m->race.data(), m->race.size());
  g.CopyTo(NAMES, m->names.data(), m->names.size());
  g.CopyTo(IMAGE, m->image.data(), m->image.size());
  Notice("fzgx: mod image ({} bytes) and race values written", m->image.size());
}

void PaintMachines(Guest& g)
{
  const Match* m = Get();
  if (!m || s_painted)
    return;
  s_painted = true;
  // the main (full-detail) set is port 0's machine's, racer 0 in every game (the local seat, when it is another,
  // drives an opponent model like the rest)
  paint::PaintMachines(g, m->colors, m->stock, 0);
}

std::optional<Relay> GetRelay()
{
  const Match* m = Get();
  if (!m || m->relay.host.empty() || m->relay.port == 0 || m->relay.token == 0)
    return std::nullopt;
  return m->relay;
}
}  // namespace fzgx::boot
