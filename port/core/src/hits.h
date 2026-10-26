// Hits a rollback reveals, shown. In close racing a rival's side attack, a lunge over a few
// frames from a button no prediction foresees, reaches this game a round trip late: the rollback runs the steps
// again with the attack and the race jumps to the present, the victim already hit and pushed. The race is right; the
// player saw neither the lunge nor the hit: the steps run again make no sound (the image's mute: a step simulated
// before), and the rivals' smoothing (smooth.py) glides a corrected rival to where it is, which hides the lunge.
//
// HitShow is the decision, plain numbers with no game in it so that it is tested alone (tests/test_hits.cpp): the
// host tells it what each step left (which seats are in machine contact, which are in a side attack) and which step
// a displayed frame showed, and it says, once per displayed frame, the hits and the lunges that a rollback put into
// steps the player never saw. hits.cpp reads the game for it and shows them: the game's own hit sound, pushed into
// its sound queue, and a lunging rival's smoothing offset shrunk fast, so the corrected path is drawn as a short jab.
// Both are presentation only: nothing the race reads is written (the sound queue and the smoothing's offsets lie
// outside the rolled-back memory, and no step reads them).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace fzgx::net
{
class HitShow
{
public:
  using u32 = std::uint32_t;
  static constexpr u32 RING = 64;    // steps remembered (a rollback is at most 24 deep: MAX_AHEAD)
  static constexpr u32 WINDOW = 12;  // a hit is shown when it lies at most this many steps before the frame's own
  static constexpr u32 NEAR = 4;     // a hit within this many steps of one the player saw (same seat) is that one

  // What the step `step` left: the seats in machine contact in it (bit i: seat i; the machine's flag 0x02000000,
  // set by the contact task and cleared by the machine's next step) and those in a side attack (flag 0x20000).
  // Called once for every time a step runs, the first time and every time a rollback runs it again.
  void Observe(u32 step, u32 contact, u32 attack)
  {
    Entry& e = At(step);
    if (e.step != step)
      e = Entry{step};
    e.contact = contact;
    e.attack = attack;
    e.known = true;
    if (!m_open)
    {
      m_open = true;
      m_first = step;
      m_again = false;
      m_before = m_any ? m_high : 0;
    }
    m_first = step < m_first ? step : m_first;
    if (m_any && step <= m_high)
      m_again = true;
    m_high = m_any && m_high > step ? m_high : step;
    m_any = true;
  }

  struct Shown
  {
    u32 hits = 0;     // seats whose machine contact began in a step the player did not see (bit i: seat i)
    u32 lunges = 0;   // seats whose side attack began in such a step
    u32 hit_age = 0;  // the steps from the newest such hit to the frame's own step (0: none)
    bool hard = false;  // one of those hits had a side attacker in contact in its step (the game's louder hit)
  };

  // The displayed frame that just ended showed the step `step` (its last): what it showed counts as seen, and when
  // the frame ran steps again (a rollback), the hits and lunges in the steps before it that the player never saw.
  // Each is said once: a hit said, or one the player saw near it, is not said again by a later rollback.
  Shown Present(u32 step)
  {
    Shown out;
    Entry& now = At(step);
    if (now.step == step && now.known)
    {
      now.seen_contact |= now.contact;
      now.seen_attack |= now.attack;
    }
    const bool again = m_open && m_again;
    const u32 first = m_open ? m_first : step;
    m_open = false;
    if (!again)
      return out;
    // Only steps that ran before this frame: a step new in it (catching up) played its own sound and was not hidden
    // by a correction; and the frame's own step is the one shown.
    const u32 from = std::max({first, step > WINDOW ? step - WINDOW : 1u, 1u});
    const u32 to = std::min(step, m_before + 1);
    for (u32 u = from; u < to; ++u)
    {
      Entry& e = At(u);
      const Entry& before = At(u - 1);
      if (e.step != u || !e.known || before.step != u - 1 || !before.known)
        continue;
      const u32 hits = e.contact & ~before.contact;
      const u32 lunges = e.attack & ~before.attack;
      for (u32 seat = 0; seat < 32; ++seat)
      {
        const u32 bit = 1u << seat;
        if ((hits & bit) && !SeenNear(u, step, bit, true))
        {
          e.seen_contact |= bit;
          out.hits |= bit;
          out.hit_age = step - u;
          out.hard = out.hard || (e.contact & e.attack) != 0;
        }
        if ((lunges & bit) && !SeenNear(u, step, bit, false))
        {
          e.seen_attack |= bit;
          out.lunges |= bit;
        }
      }
    }
    return out;
  }

  // Whether the frame gives the game's hit rumble: a hit shown of the local seat (the pad's own machine; the game
  // rumbles only the pads of the machines hit). Present says each hit once, so the rumble is never asked twice.
  static bool RumblesFor(const Shown& shown, u32 local) { return local < 32 && (shown.hits >> local & 1) != 0; }

private:
  struct Entry
  {
    u32 step = 0xFFFFFFFF;
    u32 contact = 0, attack = 0;            // as the step's latest run left it
    u32 seen_contact = 0, seen_attack = 0;  // shown to the player at this step, by any run of it
    bool known = false;
  };

  Entry& At(u32 step) { return m_ring[step % RING]; }

  // Whether the seat's contact (or attack) was shown at a step near `u`, up to the frame's own step.
  bool SeenNear(u32 u, u32 step, u32 bit, bool contact)
  {
    const u32 lo = u > NEAR ? u - NEAR : 0;
    const u32 hi = std::min(u + NEAR, step);
    for (u32 v = lo; v <= hi; ++v)
    {
      const Entry& e = At(v);
      if (e.step == v && ((contact ? e.seen_contact : e.seen_attack) & bit))
        return true;
    }
    return false;
  }

  std::array<Entry, RING> m_ring{};
  bool m_open = false;   // a displayed frame's steps are being observed
  bool m_again = false;  // ... and one of them ran before
  bool m_any = false;
  u32 m_first = 0;       // the frame's lowest step
  u32 m_high = 0;        // the highest step ever observed
  u32 m_before = 0;      // ... when the frame's first step was observed: the steps up to it ran before
};
}  // namespace fzgx::net
