// The words an adoption keeps as the game has them (rollback_worker.cpp's KeptWords), from one table: the rule and the
// rows of research/study-rollback-second-core.md section 18. A word of the log's region is kept when code the worker
// never runs writes it and no race step does: an interrupt, a callback, the time base, or the frame's own code before
// the first step's hook. Each row says how its address is found and which writer justifies it.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "fzgx/guest.h"

namespace fzgx::rollback_worker
{
// How a row's words are found at an adoption (section 18.4, item 1).
enum class Where
{
  FIXED,    // (i) a main.rel word or range: `at` to `at + size`
  POINTER,  // (ii) a word relative to a pointer read in both memories: [at] + offset
  LIST,     // (iii) a list walked in both memories (the two task managers' nodes, + offset)
};

// What writes the row's words where the worker cannot (W takes no interrupt, runs no callback, its time base stands
// still, and it never reaches the frame's last step).
enum class Writer
{
  INTERRUPT,  // the VI retrace callbacks (pre and post)
  CALLBACK,   // an ARAM (ARQ) request's callback, the sound engine's, or the decoder thread it resumes
  TIME_BASE,  // the task manager's timing of its tasks (OSGetTick)
  FRAME,      // the frame routine's own code before the first step's hook (`filters`, the input layer)
};

struct KeptRow
{
  const char* name;
  Where where;
  u32 at;      // FIXED: the first byte; POINTER: the pointer's address; LIST: 0 (TimeWords walks both managers)
  u32 size;    // FIXED: bytes (a multiple of 4); POINTER: the offset from the pointer; LIST: the offset in a node
  Writer writer;
  char sweep;  // section 18.2's class: 'a' read by a step (barred, not fixed by keeping), 'b' once a frame, 'c' only
               // by its writer; 'f' the frame routine's words, 't' the time words (section 18.3)
  const char* by;  // the writer, as section 18 names it
};

// The table (section 18.2 and 18.3, with the frame routine's words of S3).
std::span<const KeptRow> KeptTable();

// The task manager's time words (the LIST row and the ten timing starts of .bss 0xD7C), of both memories.
std::vector<u32> TimeWords(const Guest& a, const Guest& b);

// Every word of the table, resolved in the game's memory g and W's gw, sorted and unique.
std::vector<u32> KeptTableWords(const Guest& g, const Guest& gw);

// The step from which a job may start (section 18.2: the class (a) row's bar; the countdown's 0 is step 421).
constexpr u32 KEPT_BAR = 422;
}  // namespace fzgx::rollback_worker
