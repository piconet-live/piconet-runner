// The rollback worker's guards from the thread audit (rollback_worker_guard.h; fzgx/rollback_worker.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rollback_worker_guard.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "fzgx/rollback_worker.h"
#include "internal.h"

namespace fzgx::rollback_worker
{
namespace
{
std::atomic<u64 (*)()> s_generation{nullptr};
std::atomic<const char*> s_instrumented{nullptr};
std::atomic<bool (*)()> s_interception{nullptr};
u64 s_generation_at = 0;
}  // namespace

void SetCodeGeneration(u64 (*generation)())
{
  s_generation = generation;
}

void SetInstrumented(const char* what)
{
  s_instrumented = what;
}

void SetInterception(bool (*active)())
{
  s_interception = active;
}

void OffLimits(const char* entry)
{
  if (!OnW())
    return;
  if (!TheOptions().debug.worker_mode.empty())
  {
    // a test or a gate (the launch file names the worker's mode): loudly, at once, from W's thread
    std::fprintf(stderr, "fzgx: rollback worker: FAILED: %s was reached on the worker's thread (study section 17's "
                         "allowlist); stopping\n", entry);
    std::fflush(stderr);
    std::abort();
  }
  GiveUp(std::string("a host entry outside the worker's allowlist: ") + entry);
}

namespace guard
{
const char* StartBar(bool pipe_per_thread)
{
  if (!pipe_per_thread)
    return "the module's pipe is one for every thread";  // U2
  if (s_instrumented.load() != nullptr)
    return "an instrumented module";  // U6
  if (const auto active = s_interception.load(); active != nullptr && active())
    return "the mod loader's interception";  // U7
  return nullptr;
}

void TakeGeneration()
{
  const auto generation = s_generation.load();
  s_generation_at = generation != nullptr ? generation() : 0;
}

bool Moved()
{
  const auto generation = s_generation.load();
  return generation != nullptr && generation() != s_generation_at;
}

std::map<std::string, u64>& Barred()
{
  static std::map<std::string, u64> barred;
  return barred;
}
}  // namespace guard
}  // namespace fzgx::rollback_worker
