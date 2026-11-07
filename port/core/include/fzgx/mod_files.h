// A mod pack's files over the disc's (the launch file's "overlay"; protocol/formats.md section 2): which of the files
// the launcher names the runner takes. The launcher's importer checks a pack before it names a file; this is the
// runner's own guard behind it, whatever the launcher sends:
// - never a file the game's code or its native form comes from: sys/ (and GCRebuilder's name for it,
//   &&systemdata/), main.dol, any .rel, enemy_line/ (main.rel), which hold the 15 files the native module was made
//   from;
// - in any start but the offline game (a race, a practice, a playback: a launch file with "match" or "replay"), a
//   race file (game/cardata.lz, the machines' stats; stage/COLI_COURSE*.lz, the courses) only from an entry with
//   "raceSame": the launcher compared its race fields with the disc's and found them equal, so only look fields
//   (a course's fog) differ. The offline game takes them either way.
// Each refused entry is a sentence for the log, and the start goes on with the rest. An entry whose path is not on
// the disc is passed over (packs carry previews, read-mes and saves), and so is one whose replacement is not a file.
// The adapter puts the chosen files over the extracted disc (mod_files.cpp in port/moderngekko).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fzgx/core.h"

#include <functional>
#include <string>
#include <vector>

namespace fzgx::mod_files
{
struct Plan
{
  std::vector<OverlayFile> files;  // the files taken: one per disc path (the last entry for it), in the order of each
                                   // path's first entry
  std::vector<std::string> refused;  // a sentence for each entry refused, in the file's order
  u32 not_on_disc = 0;   // entries whose path the disc does not have, passed over
  u32 no_source = 0;     // entries whose replacement is not a file, passed over
  u32 replaced = 0;      // entries a later one for the same path took the place of
  u32 race_files = 0;    // files taken that are race files (car data, a course)
};

// The files of `asked` the runner takes. `offline_game`: the start is the offline game (no "match", no "replay").
// `on_disc`: whether the disc has a file at a path under files/; `is_file`: whether a replacement is a file there.
Plan Choose(const std::vector<OverlayFile>& asked, bool offline_game,
            const std::function<bool(const std::string& disc)>& on_disc,
            const std::function<bool(const std::string& from)>& is_file);

// Why the runner does not take `disc` in such a start: a sentence, or empty when it may (the path's form, then the
// files of the code, then a race file outside the offline game whose entry has no `race_same`).
std::string Refusal(const std::string& disc, bool offline_game, bool race_same = false);

// The line the log says at boot: how many files are overlaid, and how many entries were refused or passed over.
std::string Summary(const Plan& plan, bool offline_game);
}  // namespace fzgx::mod_files
