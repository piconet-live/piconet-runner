// The draw marks (the host call HOST_MARK, port/image/sites_marks.py): at the game's own draw sites the image tells which
// machine's body or burner (which one) the next draws are; the emulator's Aurora feed puts the mark at that place of the command stream
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
// aurora_present.h: 1 + the machine in bits 0-7, the kind in bits 8-15, for a burner 1 + its record's slot in bits 16-23
constexpr u32 TAG_BODY = 1, TAG_BURNER = 2;
constexpr u32 SEATS = 30;
// the car object's machine block (fn_1_84860: obj +0x32C); a burner draw node's record (+8) and its machine block (+0x48)
constexpr u32 CAR_MACHINE = 0x32C, NODE_RECORD = 8, RECORD_MACHINE = 0x48;
// The burners' pool (facts BURNERS, burner.c): fn_1_986A4 hands out BURNER_SLOTS records of BURNER_SIZE bytes from +BURNER_FIRST;
// fn_1_983CC makes one for each nozzle in a machine's .fmi (car.c), so a record is one burner of one machine
constexpr u32 BURNER_FIRST = 0x80, BURNER_SIZE = 0x4AC, BURNER_SLOTS = 120;

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

// 1 + the slot of this burner record in the game's pool, 0 for any other address
u32 BurnerSlot(u32 record)
{
  const u32 first = Read32(game::BURNERS_AT) + BURNER_FIRST;
  if (first == BURNER_FIRST || record < first || record >= first + BURNER_SLOTS * BURNER_SIZE || (record - first) % BURNER_SIZE != 0)
    return 0;
  return (record - first) / BURNER_SIZE + 1;
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
    const u32 record = Read32(arg + NODE_RECORD);
    if (const u32 m = Machine(Read32(record + RECORD_MACHINE)))
      tag = BurnerSlot(record) << 16 | TAG_BURNER << 8 | m;  // which of the machine's burners: a light each (Aurora rt_rules.hpp light_picks)
  }
  VideoCommon::AuroraPresent::NoteMark(tag);  // (END, or an object of no machine: none)
}
}  // namespace fzgx_port
