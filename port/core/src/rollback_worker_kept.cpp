// The kept words' table (rollback_worker_kept.h; research/study-rollback-second-core.md section 18).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback_worker_kept.h"

#include <algorithm>
#include <array>

#include "net_internal.h"
#include "rollback.h"

namespace fzgx::rollback_worker
{
namespace
{
constexpr u32 BSS = game::REL_BSS_AT, DATA = game::REL_DATA_AT;
constexpr u32 RAM = layout::RAM;
constexpr u32 TIMING_STARTS = BSS + 0xD7C;  // fn_1_3BDC: the tick at each of ten timings' start
constexpr u32 FRAME_MANAGER = BSS + 0xDAC;  // the simulation manager, whose list mask and task filter `filters` restores

constexpr std::array ROWS = {
    // the VI retrace callbacks (registered by fn_1_A8D84)
    KeptRow{"VI count", Where::FIXED, BSS + 0x6F640, 4, Writer::INTERRUPT, 'b', "fn_1_A8D4C (VI pre-retrace)"},
    KeptRow{"pads ready", Where::FIXED, DATA + 0x2B64, 4, Writer::INTERRUPT, 'c',
            "fn_1_3920 (VI post-retrace) = 0; the input layer fn_1_1304 = 1"},
    KeptRow{"VI callback's retrace count", Where::FIXED, BSS + 0xD6C, 4, Writer::INTERRUPT, 'c',
            "fn_1_3920 (VI post-retrace), lbl_1_bss_D58 + 0x14"},
    KeptRow{"pads probed and merged in a stall", Where::FIXED, BSS + 0x980, 0x18, Writer::INTERRUPT, 'b',
            "fn_1_3920's stall branch (SIProbe, PADRead); the input layer fn_1_3BC0 clears"},
    KeptRow{"reset check", Where::FIXED, BSS + 0x7AD48, 0xC, Writer::INTERRUPT, 'c',
            "fn_1_D332C (VI post-retrace's stall branch and the input layer); fn_1_D3214 (a disc error)"},
    // the ARAM requests' callbacks (__ARQInterruptServiceRoutine)
    KeptRow{"file loads done", Where::FIXED, BSS + 0x384C8, 8, Writer::CALLBACK, 'c', "fn_1_451D4 (ARQ)"},
    KeptRow{"the load ring's busy words", Where::FIXED, BSS + 0x3DCE4, 0x40, Writer::CALLBACK, 'c', "fn_1_45A58 (ARQ)"},
    KeptRow{"loads done (fn_1_4B8DC)", Where::FIXED, BSS + 0x4BA58, 8, Writer::CALLBACK, 'c', "fn_1_4B8BC, fn_1_4B8CC (ARQ)"},
    KeptRow{"race set-up's load done", Where::FIXED, BSS + 0x7ECFC, 4, Writer::CALLBACK, 'c', "fn_1_F1950 (ARQ)"},
    KeptRow{"the countdown movie's stream state", Where::FIXED, BSS + 0x854C8, 4, Writer::CALLBACK, 'a',
            "fn_1_106D30 (ARQ), the decoder thread fn_1_106CAC; read by the Starter to step 421 (KEPT_BAR)"},
    // the time base (section 18.3)
    KeptRow{"timings' starts", Where::FIXED, TIMING_STARTS, 40, Writer::TIME_BASE, 't', "fn_1_3BDC"},
    KeptRow{"tasks' time sums", Where::LIST, 0, 0x1C, Writer::TIME_BASE, 't', "fn_1_3C18 (fn_1_407C +0xAC)"},
    // the frame routine's own words (S3, async-check: `filters`, frame_replay.py)
    KeptRow{"simulation manager's list mask", Where::POINTER, FRAME_MANAGER, 0x10, Writer::FRAME, 'f', "filters"},
    KeptRow{"simulation manager's task filter", Where::POINTER, FRAME_MANAGER, 0x14, Writer::FRAME, 'f', "filters"},
    KeptRow{"presentation flag", Where::FIXED, DATA + 0x3599C, 4, Writer::FRAME, 'f', "filters (replay_filter)"},
};

bool InRegion(u32 address)
{
  return address >= RAM && address < net::STATE_END && (address & 3) == 0;
}

// The LIST row: every node of both task managers' lists (the simulation's and the drawing's), + offset
void Walk(const Guest& g, u32 offset, std::vector<u32>* words)
{
  constexpr u32 MANAGERS[] = {BSS + 0x3C00 + 0x21F88, BSS + 0x3C00 + 0x220A4};
  for (const u32 at : MANAGERS)
  {
    const u32 manager = g.Read32(at);
    for (u32 list = 0; InRegion(manager) && list < 32; ++list)
    {
      const u32 head = g.Read32(manager + 24 + list * 4);
      u32 node = InRegion(head) ? g.Read32(head + 8) : 0;
      for (u32 n = 0; InRegion(node) && n < 4096; ++n, node = g.Read32(node + 8))
        words->push_back(node + offset);
    }
  }
}

void Resolve(const KeptRow& row, const Guest& a, const Guest& b, std::vector<u32>* words)
{
  switch (row.where)
  {
    case Where::FIXED:
      for (u32 k = 0; k < row.size; k += 4)
        words->push_back(row.at + k);
      break;
    case Where::POINTER:
      for (const Guest* m : {&a, &b})
        if (const u32 p = m->Read32(row.at); InRegion(p))
          words->push_back(p + row.size);
      break;
    case Where::LIST:
      for (const Guest* m : {&a, &b})
        Walk(*m, row.size, words);
      break;
  }
}

void SortUnique(std::vector<u32>* words)
{
  std::sort(words->begin(), words->end());
  words->erase(std::unique(words->begin(), words->end()), words->end());
}
}  // namespace

std::span<const KeptRow> KeptTable()
{
  return ROWS;
}

std::vector<u32> TimeWords(const Guest& a, const Guest& b)
{
  std::vector<u32> words;
  for (const KeptRow& row : ROWS)
    if (row.writer == Writer::TIME_BASE)
      Resolve(row, a, b, &words);
  SortUnique(&words);
  return words;
}

std::vector<u32> KeptTableWords(const Guest& g, const Guest& gw)
{
  std::vector<u32> words;
  for (const KeptRow& row : ROWS)
    Resolve(row, g, gw, &words);
  SortUnique(&words);
  return words;
}
}  // namespace fzgx::rollback_worker
