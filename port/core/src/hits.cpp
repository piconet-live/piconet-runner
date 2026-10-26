// Hits a rollback reveals, shown: the game's side of HitShow (hits.h, which says why). Before every step of a race
// the hook reads what the step before left; at a displayed frame's first step, what the frame before showed, and the
// hits and lunges its rollback put into steps the player never saw are shown now:
// - a hit: the game's own hit sound at the machine's voice, pushed into its sound queue as the game's sound call does
//   (the queue is the sound system's, which no rollback puts back and no step reads), once, a few steps late;
// - a lunge: the rival's smoothing offset (smooth.py, the mod's data window) is shrunk faster for a few frames, so
//   the drawing goes from where the rival was expected to where the attack took it in a short, fast jab, not the
//   slow glide that hid it.
// The sparks need nothing: the contact task spawns them in every step, the steps run again included, and the
// effects advance with every step, so a re-simulated hit's sparks are there, as far on as they would be.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <bit>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <format>
#include <initializer_list>

namespace fzgx::net
{
namespace
{
constexpr u32 CONTACT = 0x02000000;     // machine +0x0: in machine contact in this step (fn_1_239C4)
constexpr u32 SIDE_ATTACK = 0x20000;    // machine +0x0: in a side attack
constexpr u32 MACHINE_SIZE = 0x620;
// The game's sound command queue (main.dol fn_80067898, which every sound start goes through): its record's address
// at r13 - 0x7740; in the record +0x240 the 64 commands (0: free), +0x441 the commands waiting, +0x443 where the
// next goes. A command is a word with its top bit set. The game's 2D sound start (fn_1_A2D84) adds a command only
// while its mute byte (.bss 0x6F1D8) is 0 and its sound state (.bss 0x6F1DC) is at most 0x2D.
constexpr u32 SOUND_QUEUE = game::SOUND_QUEUE;
constexpr u32 SOUND_MUTE = BSS + 0x6F1D8;
constexpr u32 SOUND_STATE = BSS + 0x6F1DC;
// The hit sound (main.rel fn_1_A33F0, which the contact sparks fn_1_68B6C call for the first machine of a colliding
// pair, its strength the closing speed's square clamped to 0.01..0.6): for the machine's voice, if it has one (the
// voices' table at .bss 0x6F1E4, 0x14 bytes each, +1 the machine, +2 its volume; their count at .bss 0x6F2C4), the
// voice's volume (fn_80067344(1, 0x10, 0xB0270000, volume): the command 0xBF270001 + (volume << 8)), then the hit,
// 0xA9090200 up to strength 0.2, 0xA9090600 above it. A side attack's hit is the heavy one.
constexpr u32 VOICES = BSS + 0x6F1E4;
constexpr u32 VOICE_COUNT = BSS + 0x6F2C4;
constexpr u32 VOICE_VOLUME = 0xBF270001;
constexpr u32 HIT_SOUND = 0xA9090200;
constexpr u32 HARD_HIT_SOUND = 0xA9090600;
// The rumble the contact task asks for a hit (fn_1_239C4 0x245E8: fn_1_F7338(pad, 4, 12) for each machine of the pair,
// its pad +0x474; mute.py stops it in a step run again). fn_1_F7338 asks only for a machine without flag 0x10 at
// +0x58C, and fn_1_F73A8 only while the rumble is on (.bss 0x7F028 bit 0), the pad 0-3 has it on in the player's
// options (.data 0x3E8F0, a bit a pad, from the save) and the duration is at most 180; it writes the pad's request
// (.bss 0x7F02C + pad * 4: +0 kind, +2 duration), which the rumble task (fn_1_F7128, once a frame) turns into the
// motor's commands and clears: so the emulator's own rumble settings apply as to any rumble of the game. Asked here
// only when no request waits this frame (an empty request takes any). untracked.py keeps that memory out of
// rollbacks, so no rollback takes it back.
constexpr u32 RUMBLE_ON = BSS + 0x7F028;
constexpr u32 RUMBLE_REQUESTS = BSS + 0x7F02C;
constexpr u32 RUMBLE_PADS = 0x1ABEF0;  // .data 0x3E8F0, from the module's base
constexpr u32 PAD = 0x474, MACHINE_FLAGS = 0x58C;
constexpr u8 HIT_RUMBLE = 4;
constexpr u16 HIT_RUMBLE_FRAMES = 12;

// A lunging rival's smoothing (smooth.py): its seat's offset at SMOOTH + seat * 48 + 12 (three floats), which
// smooth_draw shrinks to 0.85 of itself every frame; for LUNGE_FRAMES frames it is shrunk by LUNGE_FACTOR more on
// each frame's entry, so about half goes in a frame: a correction of a few units is drawn in three or four frames.
constexpr u32 LUNGE_FRAMES = 5;
constexpr float LUNGE_FACTOR = 0.6f;

float AsFloat(u32 word)
{
  float f;
  std::memcpy(&f, &word, 4);
  return f;
}

u32 AsWord(float f)
{
  u32 word;
  std::memcpy(&word, &f, 4);
  return word;
}

// The sound commands the game added since the hook last looked (the step before, and on a frame's first step the
// presentation after the frame before's steps too), as text for the log.
std::string NewSounds(Guest& g, Session& s)
{
  const u32 queue = g.Read32(SOUND_QUEUE);
  if (g.Ram(queue, 0x468) == nullptr)
    return {};
  const int at = g.Read8(queue + 0x443) & 63;
  std::string out;
  if (s.queue_at >= 0)
    for (int k = s.queue_at; k != at; k = (k + 1) & 63)
      out += std::format(" {:08x}", g.Read32(queue + 0x240 + 4 * k));
  s.queue_at = at;
  return out;
}

// Commands pushed into the game's sound queue as fn_80067898 pushes each, all or none; false when the game would not
// have taken them (muted, past its sound state, the queue busy or full).
bool PushSounds(Guest& g, Session& s, u32 module, std::initializer_list<u32> ids)
{
  const u32 queue = g.Read32(SOUND_QUEUE);
  if (g.Ram(queue, 0x468) == nullptr)
    return false;
  if (g.Read8(module + SOUND_MUTE) != 0 || g.Read32(module + SOUND_STATE) > 0x2D || g.Read8(queue + 0x464) != 0)
    return false;
  const u32 waiting = g.Read8(queue + 0x441);
  const u32 at = g.Read8(queue + 0x443) & 63;
  const u32 n = static_cast<u32>(ids.size());
  if (waiting + n > 0x40)
    return false;
  for (u32 k = 0; k < n; ++k)
    if (g.Read32(queue + 0x240 + 4 * ((at + k) & 63)) != 0)
      return false;
  u32 k = 0;
  for (const u32 id : ids)
    g.Write32(queue + 0x240 + 4 * ((at + k++) & 63), id);
  g.Write8(queue + 0x443, static_cast<u8>((at + n) & 63));
  g.Write8(queue + 0x441, static_cast<u8>(waiting + n));
  if (s.queue_at == static_cast<int>(at))
    s.queue_at = static_cast<int>((at + n) & 63);  // (ours: not the game's, for the log)
  return true;
}

// The hit sound for the hits shown, as fn_1_A33F0 makes it: at the loudest voice among the machines hit (none near the
// camera: no sound, as in the game).
bool PlayHit(Guest& g, Session& s, u32 module, u32 hits, bool hard)
{
  const u32 count = std::min<u32>(g.Read8(module + VOICE_COUNT), 8);
  u32 volume = 0;
  for (u32 k = 0; k < count; ++k)
  {
    const u32 voice = module + VOICES + 0x14 * k;
    const u32 machine = g.Read8(voice + 1);
    if (machine < 30 && (hits >> machine & 1))
      volume = std::max<u32>(volume, g.Read8(voice + 2));
  }
  if (volume == 0)
    return false;
  return PushSounds(g, s, module, {VOICE_VOLUME + ((volume << 8) & 0x7F00), hard ? HARD_HIT_SOUND : HIT_SOUND});
}

// The game's hit rumble for the local machine, as fn_1_F7338 would ask for it: false when it would not have. In our
// races no machine has a pad (+0x474 is -1 for every machine, the local one too, live and in a playback: dumps of runs
// rv030-pd and rv030-hitd), so the machine's own pad would never rumble; the local player's controller
// is the runner's port 0, and that is the pad asked for then.
bool RumbleHit(Guest& g, u32 module, u32 machine)
{
  std::int8_t pad = static_cast<std::int8_t>(g.Read8(machine + PAD));
  if (pad < 0)
    pad = 0;
  if (pad > 3 || (g.Read32(machine + MACHINE_FLAGS) & 0x10) != 0 || (g.Read8(module + RUMBLE_ON) & 1) == 0 ||
      (g.Read8(module + RUMBLE_PADS) >> pad & 1) == 0)
    return false;
  const u32 request = module + RUMBLE_REQUESTS + 4 * static_cast<u32>(pad);
  if (g.Read8(request) != 0)
    return false;
  g.Write8(request, HIT_RUMBLE);
  g.Write16(request + 2, HIT_RUMBLE_FRAMES);
  return true;
}

// A lunging rival's offset, shrunk once more on this frame's entry.
void ShrinkLunges(Guest& g, Session& s)
{
  for (u32 seat = 0; seat < 30; ++seat)
  {
    if (s.lunge_frames[seat] == 0)
      continue;
    --s.lunge_frames[seat];
    // the drawn position (+24) moves with the offset: a rollback frame's smoothing starts from where the rival was
    // drawn last, and without it the next rollback (one a frame while the attack's inputs come in) undid the jab
    const u32 offset = layout::SMOOTH + seat * 48 + 12, drawn = offset + 12;
    for (u32 k = 0; k < 3; ++k)
    {
      const float was = AsFloat(g.Read32(offset + 4 * k)), now = was * LUNGE_FACTOR;
      g.Write32(offset + 4 * k, AsWord(now));
      g.Write32(drawn + 4 * k, AsWord(AsFloat(g.Read32(drawn + 4 * k)) - (was - now)));
    }
  }
}
// Tests (Debug::slow_from): the frames that show the steps asked for run slowly, for pictures of each.
void SlowForPictures(Session& s, u32 shown)
{
  const Debug& d = TheOptions().debug;
  const bool slow = d.slow_from != 0 && shown >= d.slow_from && shown <= d.slow_to;
  if (slow == s.slowed)
    return;
  if (slow)
    s.slowed_from = TheHost().EmulationSpeed();
  TheHost().SetPace(slow ? 0.01f : s.slowed_from, TheHost().CpuClock());
  s.slowed = slow;
}
}  // namespace

void ShowHits(Guest& g, Session& s, bool extra)
{
  const auto began = std::chrono::steady_clock::now();
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 step = g.Read32(STEP);
  if (!extra && step != 0)
    SlowForPictures(s, step - 1);
  if (module == 0 || step == 0 || g.Read32(RUNNING) == 0 || g.Read16(module + PHASE) != 0x29)
    return;
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  if (g.Ram(machines, count * MACHINE_SIZE) == nullptr)
    return;
  u32 contact = 0, attack = 0;
  for (u32 i = 0; i < count; ++i)
  {
    const u32 flags = g.Read32(machines + i * MACHINE_SIZE);
    contact |= (flags & CONTACT) ? 1u << i : 0;
    attack |= (flags & SIDE_ATTACK) ? 1u << i : 0;
  }
  s.hits.Observe(step - 1, contact, attack);
  const std::string sounds = s.hit_log ? NewSounds(g, s) : std::string();
  HitShow::Shown shown;
  if (!extra)
    shown = s.hits.Present(step - 1);
  if (!extra)
  {
    const u32 local = g.Read32(SEAT_SLOT + 4);
    for (u32 seat = 0; seat < 30; ++seat)
      if ((shown.lunges >> seat & 1) && seat != local)
        s.lunge_frames[seat] = LUNGE_FRAMES;
    ShrinkLunges(g, s);
    if (shown.hits != 0 && PlayHit(g, s, module, shown.hits, shown.hard))
      ++s.hit_sounds;
    if (local < count && HitShow::RumblesFor(shown, local) && RumbleHit(g, module, machines + local * MACHINE_SIZE))
      ++s.hit_rumbles;
  }
  s.hits_shown += std::popcount(shown.hits);
  s.lunges_shown += std::popcount(shown.lunges);
  if (s.hit_log && (contact != 0 || attack != 0 || !sounds.empty() || shown.hits != 0 || shown.lunges != 0))
  {
    const std::string line = std::format("{},{},{:08x},{:08x},{:08x},{:08x},{},{}\n", step - 1, extra ? "x" : "f",
                                         contact, attack, shown.hits, shown.lunges, shown.hit_age, sounds);
    s.hit_log->WriteBytes(line.data(), line.size());
  }
  s.hits_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count();
  ++s.hits_calls;
}

void OpenHitLog(Session& s)
{
  const std::string path = TheOptions().debug.hit_log;
  if (path.empty())
    return;
  s.hit_log = std::make_unique<File>(path, "wb");
  if (!s.hit_log->IsOpen())
  {
    Notice("fzgx: the hit log {} could not be opened", path);
    s.hit_log.reset();
    return;
  }
  const std::string head = "step,run,contact,attack,hits,lunges,hit_age,sounds\n";
  s.hit_log->WriteBytes(head.data(), head.size());
}

void HitsSummary(const Session& s)
{
  Notice("fzgx: hits a rollback revealed: {} shown ({} hit sounds, {} rumbles), {} lunges; {:.2f} us a step over {} "
         "steps",
         s.hits_shown, s.hit_sounds, s.hit_rumbles, s.lunges_shown, s.hits_calls ? s.hits_us / s.hits_calls : 0.0, s.hits_calls);
}
}  // namespace fzgx::net
