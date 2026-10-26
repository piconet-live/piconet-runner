// A start that opens the game's own controller options (a launch file's "controllerSetup"): the screen where a
// player calibrates the stick, sets the buttons and the rumble (Options, Controller). The start is a normal boot,
// so the title's memory card sequence loads the player's save as in any offline start; this file then asks the
// game for its Options where the player would press Start, asks Options for its controller menu, and stops the
// game when the player leaves that menu. Nothing is pressed for the player and nothing of the save is written
// here: each screen under the menu saves through the game's own save screen when the player changed something
// and confirmed, and a visit that changes nothing writes nothing.
//
// The game's mode machine (main.rel fn_1_ECC) keeps four 16-bit words: the mode and the sub-mode it is in, and
// the ones asked for, which it enters at its next frame (the old one's exit, the new one's start). The game's own
// screens ask by storing into the same two words. What is relied on, and where it was read:
//   - the title. After its memory card sequence (the sub-mode MEMCARD, 0x9A, which ends by asking for the
//     demonstration) a boot stands in SUBMODE_TITLE_DEMO, where the title takes Start and asks for the next mode with
//     a plain store (fze.title.rel fn_8_954). SAVE_DEFAULTS_SET says the save's defaults are in memory: MEMCARD's
//     init puts them there (fn_8_25F8; the title's table, .data 0x328), and while it is 0 the title's exit would,
//     over what the card gave (its _epilog, 0xAF0: the same test). A few frames on, the title loads its demonstration
//     race for some four seconds: a request stored at once is taken before that (ctlb3, ctlb4: the title was left two
//     fields after it stood), one stored during the load waits in its word until it is over (ctlb1, ctlb2).
//   - Options. Its module's start asks for its first menu itself (fze.option.rel _prolog stores
//     SUBMODE_OPTION_MENU), so a sub-mode asked for together with the mode is lost: the controller menu is asked for
//     once the first menu stands, which is the store that menu makes when the player chooses Controller
//     (fn_4_D7C). The first menu has then run one frame of its fade from black.
//   - the controller menu (fn_4_7CA4). B asks for the first menu again, after its fade to black. Its three screens
//     (calibration, button settings, rumble) come back to it, through SUBMODE_OPTION_SAVE when the player changed
//     something and confirmed: the game's save, whole, written to the memory card before the menu is back.
// The watch runs once a picture, at any point of the game's frame, so it acts only on what stands for a whole
// frame or longer (the mode and sub-mode the game is in), and writes one word at a time: a request is a single
// 16-bit store, and the mode machine reads each request word afresh wherever it tests it.
// The game is stopped as closing its window stops it: the emulator then writes the memory card's files.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

namespace fzgx::boot
{
namespace
{
enum class Visit
{
  Title,    // waiting for the title to stand after its memory card sequence
  Asked,    // Options asked for; waiting for its first menu
  Opening,  // the controller menu asked for; waiting for it
  Open,     // the player is in the controller options
  Left,     // the stop is asked for: nothing more to do
};
Visit s_visit = Visit::Title;
u32 s_saves = 0;       // times the game went to its save screen during the visit
bool s_saving = false;

void Leave(const char* how)
{
  s_visit = Visit::Left;
  Notice("fzgx: controller setup: {}; the game saved {} time{} in the visit; stopping", how, s_saves, s_saves == 1 ? "" : "s");
  TheHost().RequestStop();
}
}  // namespace

void ResetOptionsScreen()
{
  s_visit = Visit::Title;
  s_saves = 0;
  s_saving = false;
}

void OptionsScreenTick(Guest& g)
{
  if (!TheOptions().controller_setup || s_visit == Visit::Left)
    return;
  // main.rel at its pinned place: the words below are named by their addresses there
  if (MainRel(g) != layout::MAIN_REL)
    return;
  const u16 none = game::MODE_NONE;
  const u16 mode = g.Read16(game::GAME_MODE_AT), sub = g.Read16(game::GAME_SUBMODE_AT);
  const u16 asked_mode = g.Read16(game::MODE_REQUEST_AT), asked_sub = g.Read16(game::SUBMODE_REQUEST_AT);
  const bool asked = asked_mode != none || asked_sub != none;
  // Options is being left for another mode: the player backed out of the first menu before the controller menu
  // came (B in its first frame), or the game left by a way this file does not know. Either is "the player left".
  const bool leaving = s_visit != Visit::Title && ((mode == game::MODE_OPTION && asked_mode != none) ||
                                                   (s_visit != Visit::Asked && mode != game::MODE_OPTION && mode != none));
  if (leaving)
  {
    Leave("the game left its Options");
    return;
  }
  switch (s_visit)
  {
  case Visit::Title:
    if (mode == game::MODE_TITLE && sub == game::SUBMODE_TITLE_DEMO && g.Read8(game::SAVE_DEFAULTS_SET) != 0 && !asked)
    {
      g.Write16(game::MODE_REQUEST_AT, game::MODE_OPTION);
      s_visit = Visit::Asked;
      Notice("fzgx: controller setup: the title stands; asked the game for its Options");
    }
    break;
  case Visit::Asked:
    if (mode == game::MODE_OPTION && sub == game::SUBMODE_OPTION_MENU && !asked)
    {
      g.Write16(game::SUBMODE_REQUEST_AT, game::SUBMODE_OPTION_CONTROLLER);
      s_visit = Visit::Opening;
    }
    break;
  case Visit::Opening:
    if (mode == game::MODE_OPTION && sub == game::SUBMODE_OPTION_CONTROLLER)
    {
      s_visit = Visit::Open;
      Notice("fzgx: controller setup: the game's controller options are open");
    }
    break;
  case Visit::Open:
    if (sub == game::SUBMODE_OPTION_SAVE && !s_saving)
      ++s_saves;
    s_saving = sub == game::SUBMODE_OPTION_SAVE;
    // B in the controller menu: the first menu is asked for (seen while the request stands), or already entered
    if (asked_sub == game::SUBMODE_OPTION_MENU || sub == game::SUBMODE_OPTION_MENU)
      Leave("the player left the controller options");
    break;
  case Visit::Left:
    break;
  }
}
}  // namespace fzgx::boot
