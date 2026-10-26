// Tests of the launch file's "taskbar" (files_launch.cpp): the launcher's button on the Windows taskbar, which the
// game's window joins. Its three members go to Windows as they are, so the reader holds each to what it can be.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"

namespace
{
// The sentence a text is refused with; empty when it is taken.
std::string Refused(const std::string& text)
{
  Options o;
  return ParseLaunchFile(text, &o).value_or("");
}

bool Says(const std::string& text, const std::string& part)
{
  const std::string problem = Refused(text);
  if (problem.find(part) == std::string::npos)
    std::printf("refused with: \"%s\", expected: %s\n", problem.c_str(), part.c_str());
  return problem.find(part) != std::string::npos;
}
}  // namespace

// The launcher's taskbar button ("taskbar"): its three members together, each held to what Windows is given, and
// no test setting.
void TestALaunchFilesTaskbarButton()
{
  const std::string program = "C:\\\\p\\\\l.exe";  // as a JSON text writes C:\p\l.exe
  const auto taskbar = [](const std::string& id, const std::string& program, const std::string& name) {
    return R"({"about": "x", "taskbar": {"id": ")" + id + R"(", "program": ")" + program + R"(", "name": ")" + name + "\"}}";
  };
  CHECK(Refused(taskbar(std::string(128, 'i'), program, "A name")).empty());
  CHECK(Refused(taskbar("a.b", "D:\\\\Users\\\\A \u00e9\\\\Some Launcher.EXE", "n")).empty());   // a space, a letter outside ASCII
  CHECK(Says(R"({"about": "x", "taskbar": "Piconet.Launcher"})", "\"taskbar\" is not an object"));
  CHECK(Says(R"({"about": "x", "taskbar": {"id": "a.b", "program": "C:\\l.exe"}})", "\"taskbar.name\" is missing"));
  CHECK(Says(R"({"about": "x", "taskbar": {}})", "\"taskbar.id\" is missing"));
  // the id: Windows' own form, by the letters themselves (no letter of another alphabet, whatever the system's)
  for (const char* id : {"a b", "a\u00e9b", "a/b", "a:b"})
    CHECK(Says(taskbar(id, program, "n"), "\"taskbar.id\" is not an application id"));
  CHECK(Says(taskbar(std::string(129, 'i'), program, "n"), "\"taskbar.id\" is not an application id"));
  // the program: a file of this computer by its whole path, ending in .exe
  for (const char* refused : {"l.exe", "p\\\\l.exe", "\\\\l.exe", "C:l.exe", "C:/p/l.exe", "\\\\\\\\server\\\\share\\\\l.exe",
                              "\\\\\\\\?\\\\C:\\\\p\\\\l.exe", "C:\\\\p\\\\l.cmd", "C:\\\\p\\\\l", "C:\\\\.exe", "1:\\\\p\\\\l.exe",
                              "C:\\\\p\\\\l.exe\\\" /x \\\"", "C:\\\\p\\\\l.exe /x"})
    CHECK(Says(taskbar("a.b", refused, "n"), "\"taskbar.program\" is not a program file of this computer"));
  CHECK(Says(taskbar("a.b", "C:\\\\" + std::string(1020, 'p') + ".exe", "n"), "\"taskbar.program\" is not a program file"));
  CHECK(Says(taskbar("a.b", program, "two\\nlines"), "\"taskbar.name\" is not a name"));
  CHECK(Says(taskbar("a.b", program, std::string(65, 'n')), "\"taskbar.name\" is not a name"));
  CHECK(Says(R"({"about": "x", "taskbar": {"id": "a.b", "program": "C:\\p\\l.exe", "name": "n", "icon": "i.ico"}})",
             "\"taskbar.icon\" is not a setting"));
  CHECK(Says(R"({"about": "x", "debug": {"taskbar": {"id": "a.b", "program": "C:\\p\\l.exe", "name": "n"}}})",
             "\"debug.taskbar\" is not a setting of a launch file"));
  // from a file: the program must be there (no test here has a file under a drive letter: only that one which is
  // not there is refused by its key; taskbar-run.sh, among a PC's own scripts, starts a game with one that is)
  const std::string dir = Temp();
  WriteText(dir + "/launch.json", taskbar("a.b", program, "n"));
  Options read;
  const std::string said = ReadLaunchFile(dir + "/launch.json", &read).value_or("");
  CHECK(said.find("the launch file is refused: \"taskbar.program\" is not a file that is there: C:\\p\\l.exe") == 0);
  WriteText(dir + "/launch.json", R"({"about": "x"})");
  CHECK(!ReadLaunchFile(dir + "/launch.json", &read));
  std::filesystem::remove_all(dir);
}
