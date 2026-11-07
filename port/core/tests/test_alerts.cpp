// The emulator's alerts as lines of the runner's log (fzgx/alerts.h): the answers, the line, the budget.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/alerts.h"

namespace
{
using fzgx::alerts::Kind;

void TestAlertAnswers()
{
  // an alert that asks nothing: the box's one button
  for (const Kind kind : {Kind::Information, Kind::Question, Kind::Warning, Kind::Critical})
    CHECK(fzgx::alerts::Answer(kind, false));
  // a question (AskYesNo: the DSP ROM's "stop now to fix the problem?", a dump's "overwrite?"): no
  CHECK(!fzgx::alerts::Answer(Kind::Question, true));
  // a panic that asks (an ASSERT's "Ignore and continue?", a NAND file's "make another attempt?"): yes, as before
  CHECK(fzgx::alerts::Answer(Kind::Warning, true));
  CHECK(fzgx::alerts::Answer(Kind::Critical, true));
}

void TestAlertLines()
{
  fzgx::alerts::Lines lines;
  CHECK(lines.Line(Kind::Warning, "Warning", "IOS_FS: Failed to rename temporary FST file", false) ==
        "fzgx: emulator alert (Warning): IOS_FS: Failed to rename temporary FST file");
  // one line: a run of line breaks is one " / ", none at the end
  CHECK(lines.Line(Kind::Warning, "Warning", "An error occurred.\n\n  Condition: x\r\n\nIgnore and continue?\n", true) ==
        "fzgx: emulator alert (Warning): An error occurred. /   Condition: x / Ignore and continue? [answered yes]");
  CHECK(lines.Line(Kind::Question, "Question", "Delete the existing file 'a.wav'?", true) ==
        "fzgx: emulator alert (Question): Delete the existing file 'a.wav'? [answered no]");
  CHECK(lines.Line(Kind::Information, "Information", "", false) == "fzgx: emulator alert (Information): ");

  // the budget: MOST_LINES lines, then one that says so, then none
  fzgx::alerts::Lines many;
  for (std::size_t i = 0; i < fzgx::alerts::MOST_LINES; ++i)
    CHECK(!many.Line(Kind::Warning, "Warning", "again", false).empty());
  CHECK(many.Line(Kind::Warning, "Warning", "again", false) == "fzgx: emulator alert: 200 written; no more are");
  CHECK(many.Line(Kind::Warning, "Warning", "again", false).empty());
  CHECK(many.Line(Kind::Critical, "Critical", "last", false).empty());
}
}  // namespace

void TestAlerts()
{
  TestAlertAnswers();
  TestAlertLines();
}
