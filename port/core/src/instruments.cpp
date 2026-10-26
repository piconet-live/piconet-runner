// The tests' instruments in the relay session: the added lag and jitter, the trace and the memory dumps, the state
// altered on purpose, the forced rollbacks and their audit, the rollback log. Each is off unless the launch file's
// "debug" part names it (fzgx::Debug): nothing here reads anything else.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net_internal.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fzgx::net
{
namespace
{
// The machines' blocks (0x620 bytes each) and progress records (0x1FC) as they are now, one after the other.
std::vector<u8> MachineState(Guest& g)
{
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = std::min<u32>(g.Read8(module + MACHINE_COUNT), 30);
  std::vector<u8> state;
  if (machines == 0)
    return state;
  state.resize(count * (0x620 + 0x1FC), 0);
  for (u32 i = 0; i < count; ++i)
  {
    g.CopyFrom(state.data() + i * (0x620 + 0x1FC), machines + i * 0x620, 0x620);
    const u32 progress = g.Read32(machines + i * 0x620 + 0x49C);
    if (progress != 0)
      g.CopyFrom(state.data() + i * (0x620 + 0x1FC) + 0x620, progress, 0x1FC);
  }
  return state;
}
}  // namespace

// Test lag: how long one packet is held, the fixed part and (with jitter) a part that differs from packet to packet.
Clock::duration Held(Session& s, u32 fixed_ms)
{
  u32 jitter_us = 0;
  if (s.jitter_ms != 0)
  {
    s.jitter_state ^= s.jitter_state << 13;
    s.jitter_state ^= s.jitter_state >> 17;
    s.jitter_state ^= s.jitter_state << 5;
    jitter_us = s.jitter_state % (s.jitter_ms * 1000 + 1);
  }
  return std::chrono::microseconds(static_cast<long long>(fixed_ms) * 1000 + jitter_us);
}

void FlushOutgoing(Session& s)
{
  Release(s.outgoing, Clock::now(),
          [&](const std::vector<u8>& packet) { s.socket.Send(packet.data(), packet.size(), s.relay); });
}

// Tests (Debug::trace_file): one line for the checksum just taken, and the machines' state for the steps asked.
// `extra`: the hook was called before a further step of the frame, so the step just run was not the frame's last.
// The line's fourth number is the game's race clock as that step left it (before the frame function counts the
// frame's last step): the race-end rule's "now" (the image's raceend.py), which the checksum does not cover; two
// games' traces show whether they agree on it.
void Trace(Guest& g, Session& s, u32 step, u32 value, bool extra)
{
  if (!s.trace)
    return;
  const char kind = !extra ? 'L' : s.hooks_in_frame == 1 ? 'F' : 'M';
  const u32 module = g.Read32(MODULE_POINTER);
  s.trace_lines += std::format("{},{},{:08x},{}\n", step, kind, value, module != 0 ? g.Read32(module + RACE_CLOCK) : 0);
  if (s.trace_lines.size() > 60000)
  {
    s.trace->WriteBytes(s.trace_lines.data(), s.trace_lines.size());
    s.trace_lines.clear();
  }
  if (s.trace_states && step >= TheOptions().debug.trace_from && step < TheOptions().debug.trace_to)
  {
    const std::vector<u8> state = MachineState(g);
    const u32 head[4] = {Swap32(0x465A5354), Swap32(step), Swap32(static_cast<u32>(kind)), Swap32(static_cast<u32>(state.size()))};
    s.trace_states->WriteBytes(head, sizeof(head));
    s.trace_states->WriteBytes(state.data(), state.size());
    s.trace_states->Flush();
  }
}

// Tests (Debug::dump_step): the image keeps its test logs (the spectator's, the smoothing's, the generator words')
// only in a game that will dump them, which is how they are read; in every other game the word stays 0 and the
// image writes none of them. Called on every hook: the control block is the image's, and a boot clears it.
// Tests (Debug::handback_at): the rejoin's hand-back counter (steps left, 0 at that step) as a rejoin sets it, so
// its 3-2-1 monitors show in a playback (the image's picture only: no step reads the word).
void KeepImageLogs(Guest& g)
{
  if (TheOptions().debug.dump_step != 0)
    g.Write32(IMAGE_LOGS, 1);
  if (const u32 at = TheOptions().debug.handback_at; at != 0)
  {
    const u32 step = g.Read32(STEP);
    g.Write32(table::HANDBACK, step < at ? at - step : 0);
  }
}

// Tests (Debug::dump_step): the game's whole memory and the mod's data window into two files.
void Dump(Guest& g, const std::string& name)
{
  const std::string path = TheOptions().debug.dump_prefix + name;
  File ram(path + ".ram", "wb");
  const u32 size = TheHost().RamSize();
  if (ram.IsOpen() && g.Ram(layout::RAM, size) != nullptr)
    ram.WriteBytes(g.Ram(layout::RAM, size), size);
  File scratch(path + ".l1", "wb");
  if (scratch.IsOpen() && TheHost().Scratch() != nullptr)
    scratch.WriteBytes(TheHost().Scratch(), TheHost().ScratchSize());
  File window(path + ".win", "wb");
  const std::vector<u8> bytes = g.Data().Snapshot();
  if (window.IsOpen())
    window.WriteBytes(bytes.data(), bytes.size());
  Notice("fzgx: TEST: memory written to {}.ram and .win (step word {})", path, g.Read32(STEP));
}

// Tests (Debug::alter_step): this game's state made to differ from the others' on purpose, to see a desync found
// and reported. Called when the hook is about to return with the named step next to run, so every time that step
// runs (a rollback over it puts the change back; the step then runs from the altered state again): one bit of the
// local machine's position, a word the state checksum covers. The checksum of the step before is already taken.
void AlterState(Guest& g, Session& s)
{
  const u32 at = TheOptions().debug.alter_step;
  if (at == 0 || g.Read32(STEP) != at || s.my_seat < 0)
    return;
  const u32 machines = g.Read32(g.Read32(MODULE_POINTER) + MACHINES);
  if (machines == 0)
    return;
  const u32 word = machines + static_cast<u32>(s.my_seat) * 0x620 + 0x7C;
  const u32 value = g.Read32(word);
  if (!s.altered)
  {
    s.altered = true;
    Warn("fzgx: TEST: this game's state is altered before step {} (seat {}'s position, {:08x} to {:08x})", at,
         s.my_seat, value, value ^ 0x00010000);
  }
  g.Write32(word, value ^ 0x00010000);
}

Forcing ForcedRollbacks(const Guest&)
{
  Forcing f;
  if (TheOptions().debug.force_every != 0 && !TheOptions().debug.force_depths.empty())
  {
    f.every = TheOptions().debug.force_every;
    f.depths = TheOptions().debug.force_depths;
  }
  for (const u32 depth : f.depths)
    f.deepest = std::max(f.deepest, depth);
  return f;
}

// The audit (tests: a playback with forced rollbacks, where a step run again has the inputs it had). The machines'
// state after every step's first run is kept until the step is final, and compared word by word with the state
// after each later run of that step (the hook before the next step, with nothing but the step in between). Words
// that differ are what a restore and a re-simulation do not bring back to where the first run had them: by design
// the words the state checksum leaves out (pointers and what is drawn, which follows once a displayed frame). The
// count per word goes to the log when the playback ends.
// Every re-simulated step is compared, not only the state after a rollback's last one: a fault that puts one step
// wrong and heals in the next (as when the first step run again read the locked cache as the newest step had left it)
// would pass a comparison of the last step alone unseen at every depth but 1.
// `extra`: the hook was called before a further step of the frame.
void Audit(Guest& g, Session& s, bool extra)
{
  if (!s.playback || !s.rollback.Active() || TheOptions().debug.late_steps != 0 || ForcedRollbacks(g).every == 0)
    return;
  const u32 step = g.Read32(STEP);
  if (step == 0)
    return;
  const u32 ran = step - 1;
  const auto first = s.audit_first.find(ran);
  if (first == s.audit_first.end())
  {
    // the first run of a step is a frame's last (a playback adds one new step a frame)
    if (!extra)
      s.audit_first.emplace(ran, MachineState(g));
    return;
  }
  const std::vector<u8> now = MachineState(g);
  if (now.size() != first->second.size())
    return;
  ++s.audits;
  const std::size_t each = 0x620 + 0x1FC;
  for (std::size_t at = 0; at + 4 <= now.size(); at += 4)
  {
    if (std::memcmp(now.data() + at, first->second.data() + at, 4) == 0)
      continue;
    const u32 offset = static_cast<u32>(at % each);
    if (offset < 0x620)
      ++s.audit_block[offset];
    else
      ++s.audit_progress[offset - 0x620];
  }
}

void AuditSummary(const Session& s)
{
  if (s.audits == 0)
    return;
  u32 in_checksum = 0;
  std::string words;
  const auto list = [&](const std::map<u32, u32>& found, bool progress) {
    for (const auto& [offset, times] : found)
    {
      const bool counted = Rollback::InChecksum(progress, offset);
      in_checksum += counted ? 1 : 0;
      words += std::format(" {}+0x{:X}{} x{}", progress ? "progress" : "block", offset, counted ? " (IN THE CHECKSUM)" : "",
                           times);
    }
  };
  list(s.audit_block, false);
  list(s.audit_progress, true);
  Notice("fzgx: re-simulation audit: {} re-simulated steps compared; {} words of a machine's block and {} of its "
         "progress record came back different at least once, {} of them words the state checksum covers;{}",
         s.audits, s.audit_block.size(), s.audit_progress.size(), in_checksum, words.empty() ? " none" : words);
}

// Tests: the rollback log, a line for every displayed frame of the race phase (Debug::rollback_log).
void OpenWorkLog(Session& s)
{
  const std::string path = TheOptions().debug.rollback_log;
  if (path.empty())
    return;
  s.work_log = std::make_unique<File>(path, "wb");
  if (!s.work_log->IsOpen())
  {
    Error("fzgx: cannot write the rollback log {}", path);
    s.work_log.reset();
    return;
  }
  s.work_lines = "at_us,step,depth,forced,catch_up,extra,ahead,track_us,restore_us,blocks,hook_us,first_us,resim_us,"
                 "resim_n,sched,owed,held,watch,speed,buffer,watched,cam_x,cam_y,cam_z,drawn_x,drawn_y,drawn_z,"
                 "true_x,true_y,true_z\n";
}

// Tests: the trace of every checksum kept (Debug::trace_file).
void OpenTrace(Session& s)
{
  const std::string path = TheOptions().debug.trace_file;
  if (path.empty())
    return;
  s.trace = std::make_unique<File>(path, "wb");
  if (!s.trace->IsOpen())
  {
    Error("fzgx: cannot write the trace {}", path);
    s.trace.reset();
    return;
  }
  if (TheOptions().debug.trace_to != 0)
    s.trace_states = std::make_unique<File>(path + ".states", "wb");
  if (s.trace_states && !s.trace_states->IsOpen())
    s.trace_states.reset();
}
// Tests (Debug::blackout_step, blackout_seconds): a network drop with the game still open. From that step on, for so
// many seconds, nothing is sent to the relay and nothing it sends is taken.
bool Blackout(const Session& s)
{
  return s.blackout_until != Clock::time_point{} && Clock::now() < s.blackout_until;
}

void StartBlackout(Session& s, u32 step)
{
  const Debug& d = TheOptions().debug;
  if (d.blackout_step == 0 || step < d.blackout_step || s.blackout_until != Clock::time_point{})
    return;
  s.blackout_until = Clock::now() + std::chrono::seconds(d.blackout_seconds == 0 ? 10 : d.blackout_seconds);
  Notice("fzgx: test blackout at step {}: nothing to or from the relay for {} s", step,
         d.blackout_seconds == 0 ? 10 : d.blackout_seconds);
}

}  // namespace fzgx::net
