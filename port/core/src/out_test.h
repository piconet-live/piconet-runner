// Whether a machine is out of the race: the game's own verdict (fn_1_32600 as fn_1_32A40 calls it), the same test as
// the image's port/image/routines/outtest.py (its docstring reads the game's routine). Every place of the core that shows a racer as out asks this, so the scrubber, the replay's camera and the
// launcher's live feed say what the image's HUD and spectator say. Read only; it changes nothing.
// The cases both are held to: port/image/tests/out-cases.tsv (TestTheOutTestsCases; the image's test_out_test.py).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/guest.h"
#include "rollback.h"

namespace fzgx::net::out_test
{
// a machine's flags (+0x0) and second flags (+0x58C), from the facts table (game.tsv, the machine group)
constexpr u32 FINISHED = game::MACHINE_FLAG_FINISHED, TIME_OVER = game::MACHINE_FLAG_TIME_OVER;
constexpr u32 RETIRED = game::MACHINE_FLAG_RETIRED, DESTROYED = game::MACHINE_FLAG_DESTROYED;
constexpr u32 BROKEN_DOWN = game::MACHINE_FLAG_BROKEN_DOWN, CPU = game::MACHINE_FLAG_CPU;
constexpr u32 FALLEN = game::MACHINE_FLAGS2_FALLEN;                    // out of contact, a fall
constexpr u32 RESTORE = 0x8000;                                        // the rule word's bit (.bss 0x3C30)
constexpr u32 ONE = 0x3F800000;                                        // 1.0, .rodata 0xBE4
constexpr u32 FLAGS = game::MACHINE_FLAGS, CONTACT = game::MACHINE_FLAGS2, SPEED = game::MACHINE_SPEED;  // in a machine's block
constexpr u32 RULES = BSS + 0x3C30, RACE_TYPE = BSS + 0x3C35;          // from the module pointer's value

// The verdict. `speed`: the bits of the float at +0x17C, below 1.0 as the game's fcmpo and bge say it: a NaN (of
// either sign) is not below, minus infinity and -0 are.
constexpr bool Out(u32 flags, u32 contact, u32 speed, u32 rules, u8 race_type)
{
  if (flags & FINISHED)
    return false;   // never asked: it races on
  if (flags & TIME_OVER)
    return true;    // a timed mode's or the time limit's end, in either regime
  if (rules & RESTORE)
    return false;   // every wreck, fall and breakdown is put back
  if ((flags & (RETIRED | DESTROYED)) != 0 || (contact & FALLEN) != 0)
    return true;
  if ((flags & BROKEN_DOWN) == 0)
    return false;
  if ((flags & CPU) != 0 || race_type == 5)
    return true;
  // a breakdown alone is out only at rest: the speed's bits compared as integers, as outtest.py's text does
  if (static_cast<s32>(speed) >= static_cast<s32>(ONE))
    return false;   // 1.0 or more, or a positive NaN
  if (static_cast<s32>(speed) >= 0)
    return true;
  return (speed & 0x7FFFFFFF) <= 0x7F800000;   // a negative number below; a negative NaN not
}

// The verdict on the machine whose block is at `machine`; `module`: the module pointer's value (main.rel's place).
inline bool Out(const Guest& g, u32 module, u32 machine)
{
  return Out(g.Read32(machine + FLAGS), g.Read32(machine + CONTACT), g.Read32(machine + SPEED),
             g.Read32(module + RULES), g.Read8(module + RACE_TYPE));
}
}  // namespace fzgx::net::out_test
