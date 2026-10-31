// SPDX-License-Identifier: GPL-3.0-or-later

#include "replay.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <format>

#include "files.h"
#include "internal.h"
#include "replay_compact.g.h"
#include "replay_file.g.h"
#include "rollback.h"

namespace fzgx::net
{
u32 RaceMs(Guest& g, u32 at)
{
  return g.Read8(at + 8) * 60000u + g.Read8(at + 9) * 1000u + g.Read16(at + 10);
}

std::optional<RacerRecord> ReadRacer(Guest& g, u32 machines, u32 i)
{
  RacerRecord r;
  r.block = machines + i * game::MACHINE_SIZE;
  r.progress = g.Read32(r.block + game::MACHINE_PROGRESS);
  if (r.progress == 0)
    return std::nullopt;
  r.flags = g.Read32(r.block);
  r.finished = (r.flags & 0x10000) != 0;
  r.out = (r.flags & 0x80) != 0;
  r.rank = g.Read8(r.progress + game::PROGRESS_RANK);
  r.laps = static_cast<s32>(g.Read32(r.progress + game::PROGRESS_LAPS));
  r.lap_sum_ms = RaceMs(g, r.progress + game::PROGRESS_LAP_SUM);
  r.best_lap_ms = RaceMs(g, r.progress + game::PROGRESS_BEST_LAP);
  return r;
}

bool Recorder::Open(const std::string& path)
{
  m_path = path;
  m_file = std::make_unique<File>(path, "ab");
  if (!m_file->IsOpen())
    m_file.reset();
  return m_file != nullptr;
}

// Every step that became final: the inputs it ran with (the official ones: a step is final once it has run with
// them) and the checksum kept for it, both through the rollback interface.
void Recorder::Record(Guest& g, u32 final_steps, const std::vector<int>& seats)
{
  if (!m_file || m_recorded >= final_steps)
    return;
  std::vector<u8> row(seats.size() * replay::RECORD_SIZE);
  std::vector<u8> steps;
  for (; m_recorded < final_steps; ++m_recorded)
  {
    Rollback::UsedInputs(g, m_recorded, seats, row.data());
    replay::EncodeStep(steps, row.data(), row.size(), Rollback::KeptChecksum(g, m_recorded));
  }
  m_file->WriteBytes(steps.data(), steps.size());
  m_file->Flush();
}

void Recorder::WriteStep(const std::vector<u8>& row, u32 checksum)
{
  if (!m_file)
    return;
  std::vector<u8> step;
  replay::EncodeStep(step, row.data(), row.size(), checksum);
  m_file->WriteBytes(step.data(), step.size());
  ++m_recorded;
}

void Recorder::Flush()
{
  if (m_file)
    m_file->Flush();
}

// Recording: every racer's result once the race is over (the session calls this when the race has settled and every
// step so far has run on official inputs: frame.cpp, NormalStep), as the game holds it (ReadRacer). Written when the
// recording ends (Shutdown), so the launcher can list finish times without playing the replay.
void Recorder::TakeResults(Guest& g)
{
  m_results = ResultsOfTheRace(g);
  if (!m_results.empty())
    Notice("fzgx: race results taken for the replay before step {} ({} racers)", Rollback::Step(g),
           g.Read8(g.Read32(MODULE_POINTER) + MACHINE_COUNT));
}

std::string ResultsOfTheRace(Guest& g)
{
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  const u32 count = g.Read8(module + MACHINE_COUNT);
  if (machines == 0 || count == 0)
    return "";
  std::vector<replay::Result> results;
  for (u32 i = 0; i < std::min<u32>(count, 30); ++i)
  {
    const std::optional<RacerRecord> r = ReadRacer(g, machines, i);
    if (!r)
      continue;
    results.push_back({.seat = i, .rank = r->rank, .finished = r->finished, .out = r->out, .laps = r->laps,
                       .time_ms = r->finished ? std::optional<u32>(r->lap_sum_ms) : std::nullopt,
                       .best_lap_ms = r->best_lap_ms});
  }
  return replay::ResultsJson(results);
}

bool SameResults(const std::string& recorded, const std::string& raced)
{
  picojson::value a, b;
  return picojson::parse(a, recorded).empty() && picojson::parse(b, raced).empty() && a == b;
}

bool RecordedChecksumMatches(const std::vector<u32>& checksums, bool compact, size_t step, u32 computed)
{
  return step < checksums.size() &&
         replay::ChecksumMatches(checksums[step], !compact || replay::IsFull(step, checksums.size()), computed);
}

bool CompactReplay(const std::string& path)
{
  std::vector<u8> data, compact;
  std::string why;
  File in(path, "rb");
  const size_t size = in.IsOpen() ? in.GetSize() : 0;
  data.resize(size <= replay::MAX_BYTES ? size : 0);
  const bool read = !data.empty() && in.ReadBytes(data.data(), data.size());
  in.Close();
  if (!read || !replay::Compact(data.data(), data.size(), &compact, &why))
  {
    Notice("fzgx: the replay {} stays as it was recorded (version {}): {}", path, replay::VERSION_WRITTEN,
           read ? why : "it cannot be read");
    return false;
  }
  const std::string aside = path + ".compact";
  File out(aside, "wb");
  const bool written = out.IsOpen() && out.WriteBytes(compact.data(), compact.size()) && out.Close();
  if (!written || !MoveOver(aside, path))
  {
    RemoveQuietly(aside);
    Error("fzgx: cannot write the compact replay beside {}: it stays as it was recorded", path);
    return false;
  }
  Notice("fzgx: the replay {} made compact (version {}): {} bytes, {} as recorded", path, replay::COMPACT_VERSION,
         compact.size(), data.size());
  return true;
}

// Recording: the lineup at go (the file's format since version 2; a playback
// as the race reads its machines, files_replay.cpp): 'FZRL', u32 length (124), every seat's machine (.bss
// 0x2625C, 30 bytes), starting rank (progress record +0x115, 30 bytes) and setting (.bss 0x25C48, 30 s16), machine 0's
// control mode (+0x590) and .bss 0xD70, 2 bytes of padding.
void Recorder::WriteLineup(Guest& g)
{
  if (!m_file)
    return;
  const u32 module = g.Read32(MODULE_POINTER);
  const u32 machines = g.Read32(module + MACHINES);
  replay::Lineup lineup;
  g.CopyFrom(lineup.machines.data(), (module + BSS + 0x2625C), 30);
  for (u32 i = 0; i < 30; ++i)
  {
    const u32 progress = machines != 0 ? g.Read32(machines + i * game::MACHINE_SIZE + game::MACHINE_PROGRESS) : 0;
    lineup.ranks[i] = progress != 0 ? g.Read8(progress + game::PROGRESS_RANK) : 0;
    lineup.settings[i] = static_cast<s16>(g.Read16(module + BSS + 0x25C48 + i * 2));
  }
  lineup.control_mode = machines != 0 ? g.Read8(machines + 0x590) : 0;
  lineup.rel_d70 = g.Read8(module + BSS + 0xD70);
  const std::vector<u8> block = replay::EncodeLineup(lineup);
  m_file->WriteBytes(block.data(), block.size());
  m_file->Flush();
}

// The race's results after the last step (JSON, u32 its length, 'FZRS'), when they were taken.
void Recorder::Close(bool compact)
{
  const bool open = m_file != nullptr;
  if (m_file && !m_results.empty())
  {
    const std::vector<u8> results = replay::EncodeResults(m_results);
    m_file->WriteBytes(results.data(), results.size());
    m_file->Flush();
  }
  m_file.reset();
  // At the recording's end the file is made compact: it was written as the race ran, step by step, so that a game
  // that stops on the way leaves a whole recording behind (formats.md section 1).
  if (open && compact)
    CompactReplay(m_path);
}


}  // namespace fzgx::net

namespace fzgx::boot
{
bool StartReplay(const std::string& path)
{
  const Match* m = Get();
  if (!m)
    return false;
  picojson::object match = m->json;
  match.erase("relay");
  match.erase("token");
  picojson::object header;
  header[replay::header::MATCH] = picojson::value(match);
  header[replay::header::SEAT] = picojson::value(static_cast<double>(m->seat));
  header[replay::header::MASK] = picojson::value(static_cast<double>(m->player_mask));
  header[replay::header::DELAY] = picojson::value(static_cast<double>(m->delay));
  // the race's flow (files.h RACE_FLOW, a big-endian word of the race block): its playback keeps it
  const u8* flow = m->race.data() + RACE_FLOW;
  header[replay::header::FLOW] = picojson::value(static_cast<double>(u32{flow[0]} << 24 | u32{flow[1]} << 16 | u32{flow[2]} << 8 | flow[3]));
  // the rubber band (RACE_RUBBER_BAND_ON): a header without the key is a race that had it (before 0.4.0)
  const u8* band = m->race.data() + RACE_RUBBER_BAND_ON;
  header[replay::header::RUBBER_BAND] = picojson::value((band[0] | band[1] | band[2] | band[3]) != 0);
  // the simulation's own generator (RACE_SIM_GENERATOR): a header without the key is a race that drew from the
  // executable's word (before it)
  const u8* sim = m->race.data() + RACE_SIM_GENERATOR;
  header[replay::header::SIM_GENERATOR] = picojson::value((sim[0] | sim[1] | sim[2] | sim[3]) != 0);
  header[replay::header::MOD_SHA1] = picojson::value(m->image_sha1);
  header[replay::header::MOD_COMMIT] = picojson::value(m->bundle_commit);
  // the game, and what ran it: a replay is played back only by this game, and its row in the table of images that
  // are the same race (port/image/same-race.json) names the engine the recording was made on
  header[replay::header::GAME] = picojson::value(std::string(GAME_ID));
  std::string engine, module_sha1;
  TheHost().Recorder(&engine, &module_sha1);
  if (!engine.empty())
    header[replay::header::ENGINE] = picojson::value(engine);
  if (!module_sha1.empty())
    header[replay::header::MODULE_SHA1] = picojson::value(module_sha1);
  const std::time_t now = std::time(nullptr);
  std::array<char, 32> recorded{};
  std::strftime(recorded.data(), recorded.size(), "%Y-%m-%dT%H:%M:%S+00:00", std::gmtime(&now));
  header[replay::header::RECORDED] = picojson::value(std::string(recorded.data()));
  // a practice run (Play's Practice): the run kept, rewinds and loads leave nothing in it (practice_session.cpp)
  if (TheOptions().practice)
    header[replay::header::PRACTICE] = picojson::value(true);
  // Version 9 whatever the race holds (replay.toml's written): every race and practice since 0.6 is 0.6's set-up, on
  // the simulation's own generator and with no rubber band (files_match.cpp), a game mode or not; a program from before
  // refuses it.
  const std::string json = picojson::value(header).serialize();
  const std::vector<u8> out = replay::EncodeStart(json, m->player_mask, static_cast<u32>(m->seat));
  File file(path, "wb");
  if (!file.IsOpen() || !file.WriteBytes(out.data(), out.size()))
  {
    Error("fzgx: cannot start the replay {}", path);
    return false;
  }
  return true;
}
}  // namespace fzgx::boot
