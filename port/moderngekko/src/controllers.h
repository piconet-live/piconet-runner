// The controller helper: the runner started with `--controllers USER_FOLDER` and no game. It starts the emulator's
// input layer on that user folder, exactly as a game start does, and talks with the launcher's Controller page in
// lines of JSON on its standard input and output (protocol/formats.md, "The controller helper"): which devices
// are plugged in, what the native GameCube adapter says, what one device's inputs do while the player presses the
// control they want, what the game would read from a port with the settings on disk, and a short rumble. So the
// names and the behaviour the page shows are the ones a race has: both come from this one program.
// It reads the settings and never writes them, starts no game and takes no launch file: nothing here can start or
// change a race. It ends when its input closes.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace fzgx_port
{
// Never returns: the program ends here, with status 0 once the input has closed.
[[noreturn]] void RunControllerHelper(const std::filesystem::path& user_folder);

namespace controllers
{
// One value of a command: a text, a number, true or false, or null.
struct Value
{
  enum class Kind { Null, Text, Number, Flag };
  Kind kind = Kind::Null;
  std::string text;
  double number = 0;
  bool flag = false;
};
using Command = std::map<std::string, Value>;

// controllers_words.cpp: a line of the launcher's read as one flat JSON object (nothing nested: no command has
// more), or nothing when it is none; a text as a JSON string.
std::optional<Command> ReadCommand(const std::string& line);
std::string Quoted(const std::string& text);

// controllers_words.cpp: what the helper says, each a whole line without its end.
// The devices the input layer holds now, and the one the emulator takes when a port names none.
std::string DevicesLine();
// Each port as the settings on disk have it: its kind (the settings' own number: 0 nothing, 6 a controller, 12 the
// adapter), the device its mapping names and whether that device is there now.
std::string PortsLine(const std::array<int, 4>& kinds);
// The native adapter: "notAsked" until the launcher asks, "unused" while no port is set to it (the emulator does not
// look for it then), "found", "none", "noDriver", "inUse" or "error", with the emulator's own words for the last three.
std::string AdapterLine(bool asked, bool wanted);
// The GameCube controller of a port as the game would read it now.
std::string PadLine(int port, int kind, bool adapter_asked);
}  // namespace controllers
}  // namespace fzgx_port
