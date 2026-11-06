// The draw marks (the host call HOST_MARK, port/image/sites_marks.py): at the game's own draw sites the image tells which
// machine's body or burner the next draws are; the emulator's Aurora feed puts the mark at that place of the command stream
// it hands Aurora (VideoCommon/AuroraPresent: NoteMark), so every draw Aurora keeps carries the object the game drew it for
// (GXRuntime's aurora_present.h AURORA_TAG_*). The drawing only: guest memory is read, never written, and no register
// changes.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <cstring>

#include "Common/Swap.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/AuroraPresent.h"

namespace fzgx_port
{
namespace
{
// sites_marks.py's operations
constexpr u32 BODY = 1, BURNER = 2, END = 3;
// aurora_present.h: the kind in bits 8-15, 1 + the machine in bits 0-7
constexpr u32 TAG_BODY = 1, TAG_BURNER = 2;
constexpr u32 SEATS = 30;
// the car object's machine block (fn_1_84860: obj +0x32C); a burner draw node's record (+8) and its machine block (+0x48)
constexpr u32 CAR_MACHINE = 0x32C, NODE_RECORD = 8, RECORD_MACHINE = 0x48;

u32 Read32(u32 address)
{
  auto& memory = Core::System::GetInstance().GetMemory();
  const u32 at = address & 0x1FFFFFFF;
  if ((address >> 28) != 0x8 || at + 4 > memory.GetRamSizeReal())
    return 0;
  u32 value;
  std::memcpy(&value, memory.GetRAM() + at, 4);
  return Common::swap32(value);
}

// 1 + the machine whose block this is (the machines' blocks, racer order: facts MACHINES_AT, MACHINE_SIZE), 0 for any other
u32 Machine(u32 block)
{
  const u32 first = Read32(game::MACHINES_AT);
  if (first == 0 || block < first || block >= first + SEATS * game::MACHINE_SIZE || (block - first) % game::MACHINE_SIZE != 0)
    return 0;
  return (block - first) / game::MACHINE_SIZE + 1;
}
}  // namespace

void MarkCall(u32 op, u32 arg)
{
  if (!VideoCommon::AuroraPresent::On())
    return;
  u32 tag = 0;
  if (op == BODY)
  {
    if (const u32 m = Machine(Read32(arg + CAR_MACHINE)))
      tag = TAG_BODY << 8 | m;
  }
  else if (op == BURNER)
  {
    if (const u32 m = Machine(Read32(Read32(arg + NODE_RECORD) + RECORD_MACHINE)))
      tag = TAG_BURNER << 8 | m;
  }
  VideoCommon::AuroraPresent::NoteMark(tag);  // (END, or an object of no machine: none)
}
}  // namespace fzgx_port
