// The step run ahead on the game's thread (fzgx/runahead.h): at the frame tick the registers are kept and each thing
// the core asks for is a guest call that returns to a host call of its own, so the run loop goes on between them as
// ever (interrupts, the task's host call). When the core has done, the registers come back and the tick returns.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"
#include "lanes.h"

#include <algorithm>
#include <cstring>

#include "Core/PowerPC/StaticRecomp/StaticRecompCore.h"
#include "VideoCommon/AuroraPresent.h"
#include "VideoCommon/FrameRedraw.h"

#include "fzgx/runahead.h"

namespace fzgx_port
{
namespace
{
// Below the tick's stack: room no call of the game's comes near reaching back into.
constexpr u32 STACK_GAP = 0x400;

struct Kept
{
  u32 gpr[32];
  double fpr[32];
  double ps1[32];
  u32 pc, lr, ctr, cr, xer, fpscr;
} s_kept;

void Keep(const CPUState& c)
{
  std::memcpy(s_kept.gpr, c.gpr, sizeof(c.gpr));
  std::memcpy(s_kept.fpr, c.fpr, sizeof(c.fpr));
  std::memcpy(s_kept.ps1, c.ps1, sizeof(c.ps1));
  s_kept.pc = c.pc;
  s_kept.lr = c.lr;
  s_kept.ctr = c.ctr;
  s_kept.cr = c.cr;
  s_kept.xer = c.xer;
  s_kept.fpscr = c.fpscr;
}

void Back(CPUState* c)
{
  std::memcpy(c->gpr, s_kept.gpr, sizeof(c->gpr));
  std::memcpy(c->fpr, s_kept.fpr, sizeof(c->fpr));
  std::memcpy(c->ps1, s_kept.ps1, sizeof(c->ps1));
  c->ctr = s_kept.ctr;
  c->cr = s_kept.cr;
  c->xer = s_kept.xer;
  c->fpscr = s_kept.fpscr;
  c->lr = s_kept.lr;
  c->pc = s_kept.lr;  // the tick's caller
}

void Go(CPUState* c, u32 returned)
{
  fzgx::runahead::Call call;
  if (!fzgx::runahead::Next(returned, &call))
  {
    Back(c);
    return;
  }
  const u32 stack = s_kept.gpr[1] - STACK_GAP;
  c->gpr[1] = stack;
  c->gpr[3] = call.r3;
  c->gpr[30] = game::REL_BSS_AT;
  c->gpr[31] = table::CONTROL;
  c->lr = HOST_CALLS_END;
  c->pc = call.pc;
}
}  // namespace

void StartRunAhead()
{
  fzgx::runahead::Start([](float fraction) {
    VideoCommon::AuroraPresent::NoteBetween(VideoCommon::FrameRedraw::WrittenPosition(), fraction);
  }, [](fzgx::u32 step) { lanes::PartialForget(step); },
  [](const fzgx::runahead::RedrawRequest& r) {
    static VideoCommon::AuroraPresent::RedrawRequest out;  // (the game's thread alone)
    out.position = r.position;  // (the tick's: a request made at the drawing, AURORA_RUNAHEAD_AFTER, still belongs to the copy before it)
    out.machines = r.machines;
    std::memcpy(out.at, r.at.data(), sizeof out.at);
    out.pictures = std::min<fzgx::u32>(r.pictures, VideoCommon::AuroraPresent::REDRAW_MOST);
    for (fzgx::u32 k = 0; k < out.pictures; ++k)
    {
      out.picture[k].fraction = r.picture[k].fraction;
      std::memcpy(out.picture[k].camera, r.picture[k].camera.data(), sizeof out.picture[k].camera);
      std::memcpy(out.picture[k].machine, r.picture[k].machine.data(), sizeof out.picture[k].machine);
    }
    VideoCommon::AuroraPresent::Redraw(&out, r.view.data());
  }, [] { return VideoCommon::FrameRedraw::WrittenPosition(); });
}

void StopRunAhead()
{
  const std::string line = fzgx::runahead::Report();
  if (!line.empty())
    s_core_host.Log(fzgx::LogLevel::Notice, line);
  fzgx::runahead::Stop();
}

bool RunAheadAtTick(CPUState* cpu)
{
  if (!fzgx::runahead::Begin())
    return false;
  Keep(*cpu);
  Go(cpu, 0);
  return true;
}

void RunAheadReturned(CPUState* cpu)
{
  Go(cpu, cpu->gpr[3]);
}
}  // namespace fzgx_port
