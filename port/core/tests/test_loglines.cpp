// The game's log as a file a player sends (fzgx/loglines.h): a line without the player's folders, which of the
// emulator's lines go into the log and how many, and the file a start writes when the one before cannot be moved.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

#include "fzgx/loglines.h"

// A path of the player's folders in a line: a known folder becomes its word, however it is written; any other
// absolute Windows path is cut down to its file's name. A folder's path holds the account's name.
void TestALineWithoutThePlayersFolders()
{
  using fzgx::loglines::WithoutFolders;
  const std::vector<fzgx::loglines::Folder> known = {{"C:\\Users\\Made Up\\AppData\\Local\\app\\recomp-user", "<user folder>"},
                                                     {"C:\\Users\\Made Up", "<home>"}};
  CHECK(WithoutFolders("could not open C:\\Users\\Made Up\\AppData\\Local\\app\\recomp-user\\Config\\GFX.ini", known) ==
        "could not open <user folder>\\Config\\GFX.ini");
  // other separators, other letter case
  CHECK(WithoutFolders("could not open c:/users/made up/appdata/local/APP/recomp-user/Config/GFX.ini", known) ==
        "could not open <user folder>/Config/GFX.ini");
  CHECK(WithoutFolders("reading C:\\USERS\\MADE UP\\Documents\\a.bin failed", known) == "reading <home>\\Documents\\a.bin failed");
  // a path under no known folder: the file's name alone, with what follows it
  CHECK(WithoutFolders("could not open D:\\Games\\Some One\\saves\\card a.raw: access denied", {}) ==
        "could not open card a.raw: access denied");
  CHECK(WithoutFolders("file \"E:/x y/z/shader.bin\" is damaged, and F:\\q\\r.txt too", {}) ==
        "file \"shader.bin\" is damaged, and r.txt too");
  CHECK(WithoutFolders("from \\\\server\\share\\some one\\f.dat (network)", {}) == "from f.dat (network)");
  // what is no path stays: an address, a time, a ratio, a web address, the emulator's own source file
  const std::string plain = "FIFO: Unknown Opcode 0x62 @ 0x209d08997c0 at 12:30:01, 16:9, http://x.y/z, Core\\HLE\\HLE_OS.cpp:96";
  CHECK(WithoutFolders(plain, known) == plain);
  CHECK(WithoutFolders("", known).empty());
}

// The emulator's lines that go into the game's log: the game's own report lines and the emulator's errors have a
// budget each, so a burst of errors cannot keep the game's panic out; a line like the one before of its kind is
// counted, not written (an error that differs in its numbers only; a report of the same text).
void TestWhichOfTheEmulatorsLinesAreWritten()
{
  using fzgx::loglines::Filter;
  using Kind = Filter::Kind;
  Filter f;
  const auto error = [](int n) {
    return "E[Video]: FIFO: Unknown Opcode 0x" + std::to_string(n) + " @ 0x209d0899" + std::to_string(n) + ", CPBase: 0x0155b000";
  };
  // a burst of 24 errors that differ in their numbers: one line, and their count when something else comes
  CHECK(f.Take(Kind::Error, error(10)) == std::vector<std::string>({error(10)}));
  for (int i = 11; i < 34; ++i)
    CHECK(f.Take(Kind::Error, error(i)).empty());
  // the game's report lines between them are written at once, and do not end the errors' run
  CHECK(f.Take(Kind::Report, "N[OSREPORT]: [warning] GP WAIT Timeout... (sending object 0/3)").size() == 1);
  const std::vector<std::string> next = f.Take(Kind::Error, "E[Core]: another error");
  CHECK(next.size() == 2 && next[0].find("(23 more like it: \"E[Video]: FIFO: Unknown Opcode 0x10 @ ") == 0 && next[1] == "E[Core]: another error");
  // report lines that differ are all written (a panic's stack lines differ in their numbers only): only the same
  // text again is counted
  CHECK(f.Take(Kind::Report, "N[OSREPORT]: 0x801b75b8:   0x801b7648    0x8000c558").size() == 1);
  CHECK(f.Take(Kind::Report, "N[OSREPORT]: 0x801b7648:   0x801b7668    0x801beeb0").size() == 1);
  CHECK(f.Take(Kind::Report, "N[OSREPORT]: 0x801b7648:   0x801b7668    0x801beeb0").empty());
  const std::vector<std::string> flushed = f.Flush();
  CHECK(flushed.size() == 1 && flushed[0].find("(1 more like it: \"N[OSREPORT]: 0x801b7648:") == 0 && f.Flush().empty());
  // the errors' budget runs out: said once; the game's report lines still come, up to a budget of their own
  Filter full;
  int errors = 0, reports = 0, notes = 0;
  for (int i = 0; i < 1000; ++i)
    for (const std::string& line : full.Take(Kind::Error, i % 2 ? "E[Video]: one kind " + std::to_string(i) : "E[Core]: another kind"))
      (line[0] == '(' ? notes : errors) += 1;
  CHECK(errors == Filter::MOST_ERRORS && notes == 1);
  for (int i = 0; i < 1000; ++i)
    for (const std::string& line : full.Take(Kind::Report, "N[OSREPORT]: line " + std::to_string(i)))
      (line[0] == '(' ? notes : reports) += 1;
  CHECK(reports == Filter::MOST_REPORTS && notes == 2);
}

// The file a start writes its log to. The logs before are kept beside it, the oldest goes. A log that cannot be
// moved aside (a game that never finished its shutdown holds it: the log with its freeze report) is left as it
// is, and the start writes a file beside it; written all the same, it would have been emptied.
void TestTheLogFileAStartWrites()
{
  namespace fs = std::filesystem;
  using fzgx::loglines::LogFileToWrite;
  const fs::path folder = fs::path(Temp()) / "logs";
  fs::remove_all(folder);
  const fs::path file = folder / "runner.log";
  const auto text = [&folder](const char* name) {
    std::ifstream in(folder / name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  // no file yet: the folder is made, the file is the one to write
  CHECK(LogFileToWrite(file, 3) == file && fs::is_directory(folder));
  // five starts, three kept: .1 the latest, the oldest gone
  for (int i = 1; i <= 5; ++i)
  {
    WriteText(file.string(), "game " + std::to_string(i));
    CHECK(LogFileToWrite(file, 3) == file && !fs::exists(file));
  }
  CHECK(text("runner.1.log") == "game 5" && text("runner.2.log") == "game 4" && text("runner.3.log") == "game 3" &&
        !fs::exists(folder / "runner.4.log"));
  // a log that cannot be moved aside (here: every older name is a folder that holds something, so no rename of
  // the chain can be made): it stays as it is, and the answer is a file beside it
  fs::remove_all(folder);
  for (const char* name : {"runner.1.log", "runner.2.log"})
  {
    fs::create_directories(folder / name);
    WriteText((folder / name / "inside").string(), "x");
  }
  WriteText(file.string(), "the log of a game that still runs");
  CHECK(LogFileToWrite(file, 2) == folder / "runner.held1.log");
  CHECK(text("runner.log") == "the log of a game that still runs");
  // the fallback's own name is free again at the next start (its old text goes); one that is held too is passed over
  WriteText((folder / "runner.held1.log").string(), "old");
  CHECK(LogFileToWrite(file, 2) == folder / "runner.held1.log" && !fs::exists(folder / "runner.held1.log"));
  fs::create_directories(folder / "runner.held1.log");
  WriteText((folder / "runner.held1.log" / "inside").string(), "x");
  CHECK(LogFileToWrite(file, 2) == folder / "runner.held2.log");
  CHECK(text("runner.log") == "the log of a game that still runs");
  fs::remove_all(folder);
}
