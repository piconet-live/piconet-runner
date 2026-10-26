// The controller helper (controllers.h): the emulator's input layer started as a game start does it, and the
// launcher's commands answered until its input closes.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controllers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Common/Config/Config.h"
#include "Common/StringUtil.h"
#include "Common/WindowSystemInfo.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/Config/ConfigManager.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI_Device.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"
#include "InputCommon/ControllerInterface/MappingCommon.h"
#include "InputCommon/GCAdapter.h"
#include "UICommon/UICommon.h"

namespace fzgx_port
{
namespace
{
using namespace controllers;
using std::chrono::milliseconds;
using Time = std::chrono::steady_clock::time_point;

constexpr milliseconds TICK{8};            // how often the inputs are read
constexpr milliseconds SLOW_TICK{250};     // how often the devices, the ports and the adapter are looked at
constexpr milliseconds SETTLE{250};        // the device list must stand still this long before "ready"...
constexpr milliseconds SETTLE_LIMIT{2500}; // ...and "ready" is said by then whatever it does
constexpr int LONGEST_CAPTURE_MS = 15000, LONGEST_RUMBLE_MS = 1000;

struct Helper
{
  std::mutex lines_lock;
  std::deque<std::string> lines;  // the launcher's, as they came
  bool input_closed = false;

  std::array<int, 4> kinds{};   // each port's kind in the settings on disk
  bool adapter_asked = false;   // the launcher asked for the native adapter: only then is it opened
  int watched = -1;             // the port whose controller is said as it moves
  std::string said_devices, said_ports, said_adapter, said_pad;

  std::unique_ptr<ciface::Core::InputDetector> detector;  // "press the control you want", on capture_device
  std::string capture_device;
  int capture_id = 0;  // the launcher's number for this listening, said back with what was heard
  milliseconds capture_wait{0};

  int rumbling = -1;  // the port whose motor is on, until rumble_end
  Time rumble_end;
};

void Say(const std::string& line)
{
  std::fwrite(line.data(), 1, line.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

// Said only when it is not what was said last.
void SayNew(std::string* said, const std::string& line)
{
  if (line == *said)
    return;
  *said = line;
  Say(line);
}

void Problem(const std::string& text)
{
  Say("{\"is\":\"problem\",\"text\":" + Quoted(text) + "}");
}

// The ports' kinds as the settings file has them (the emulator's own reading of [Core] SIDevice0..3).
void ReadKinds(Helper& h)
{
  for (int port = 0; port < 4; ++port)
    h.kinds[port] = static_cast<int>(Config::GetBase(Config::GetInfoForSIDevice(port)));
}

bool AdapterWanted(const Helper& h)
{
  return std::any_of(h.kinds.begin(), h.kinds.end(),
                     [](int kind) { return kind == SerialInterface::SIDEVICE_WIIU_ADAPTER; });
}

// What a game start does for its controllers (UICommon::Init and InitControllers), less everything that is not
// input and less every write: the settings are loaded and never saved (the program leaves by std::_Exit, so the
// settings layer's own save at its end never runs either). The native adapter is not opened here: only when the
// launcher asks (AskAdapter), so a start that only wants the list of devices takes it from nobody.
void StartInput(const std::filesystem::path& user_folder)
{
  UICommon::SetUserDirectory(PathToString(user_folder));
  Config::Init();
  Config::AddLayer(ConfigLoaders::GenerateBaseConfigLoader());
  SConfig::Init();
  WindowSystemInfo wsi;
#ifdef _WIN32
  wsi.type = WindowSystemType::Windows;  // no window: the keyboard and the sticks are read in the background
#endif
  g_controller_interface.Initialize(wsi);
  Pad::Initialize();
}

void AskAdapter(Helper& h)
{
  if (h.adapter_asked)
    return;
  h.adapter_asked = true;
  // as a game start: the emulator looks for the adapter only while a port is set to it, and follows the settings
  GCAdapter::Init();
}

void Reload(Helper& h)
{
  Config::Load();
  ReadKinds(h);
  Pad::LoadConfig();
  h.said_pad.clear();
  h.said_ports.clear();
}

void StopRumble(Helper& h)
{
  if (h.rumbling < 0)
    return;
  if (h.kinds[h.rumbling] == SerialInterface::SIDEVICE_WIIU_ADAPTER)
    GCAdapter::Output(h.rumbling, 0);
  else
    Pad::ResetRumble(h.rumbling);
  h.rumbling = -1;
}

void StartRumble(Helper& h, int port, int ms)
{
  StopRumble(h);
  if (h.kinds[port] == SerialInterface::SIDEVICE_WIIU_ADAPTER)
  {
    if (!h.adapter_asked)
      return;
    GCAdapter::Output(port, 1);
  }
  else if (h.kinds[port] != SerialInterface::SIDEVICE_NONE)
    Pad::Rumble(port, 1.0);
  else
    return;
  h.rumbling = port;
  h.rumble_end = std::chrono::steady_clock::now() + milliseconds(std::clamp(ms, 50, LONGEST_RUMBLE_MS));
}

void SayCaptured(const Helper& h, const ciface::Core::InputDetector::Detection* found)
{
  std::string line = "{\"is\":\"captured\",\"id\":" + std::to_string(h.capture_id) + ",\"device\":" + Quoted(h.capture_device);
  if (found)
  {
    ciface::Core::DeviceQualifier device;
    device.FromDevice(found->device.get());
    // the name and the expression the emulator's own mapping window would write for this press
    const std::string name = found->device->GetParentMostInput(found->input)->GetName();
    line += ",\"input\":" + Quoted(name) + ",\"expression\":" +
            Quoted(ciface::MappingCommon::GetExpressionForControl(name, device, device));
  }
  else
    line += ",\"input\":null,\"expression\":null";
  Say(line + "}");
}

void StartCapture(Helper& h, const std::string& device, int ms, int id)
{
  if (h.detector)
    SayCaptured(h, nullptr);  // the one before is over, with nothing
  h.detector.reset();
  h.capture_device = device;
  h.capture_id = id;
  if (device.empty())
    return;
  h.capture_wait = milliseconds(std::clamp(ms, 500, LONGEST_CAPTURE_MS));
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  const std::vector<std::string> devices{device};
  h.detector = std::make_unique<ciface::Core::InputDetector>();
  h.detector->Start(g_controller_interface, devices);
}

// The emulator's own detector decides what counts as a press (a button down, a stick or trigger moved far from
// where it rested): the first one is said at once, and with none in time, nothing.
void Capture(Helper& h)
{
  if (!h.detector)
    return;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  h.detector->Update(h.capture_wait, milliseconds(0), h.capture_wait);
  const auto& results = h.detector->GetResults();
  if (results.empty() && !h.detector->IsComplete())
    return;
  SayCaptured(h, results.empty() ? nullptr : &results.front());
  h.detector.reset();
}

int Number(const Command& command, const char* key, int otherwise)
{
  const auto at = command.find(key);
  return at != command.end() && at->second.kind == Value::Kind::Number ? static_cast<int>(at->second.number) : otherwise;
}

std::string Text(const Command& command, const char* key)
{
  const auto at = command.find(key);
  return at != command.end() && at->second.kind == Value::Kind::Text ? at->second.text : std::string();
}

void Obey(Helper& h, const std::string& line)
{
  const auto command = ReadCommand(line);
  const std::string what = command ? Text(*command, "do") : std::string();
  const int port = command ? Number(*command, "port", -1) : -1;
  if (what == "reload")
    Reload(h);
  else if (what == "adapter")
    AskAdapter(h);
  else if (what == "watch" && port >= -1 && port < 4)
  {
    h.watched = port;  // no port: nothing is watched
    h.said_pad.clear();
  }
  else if (what == "capture")
    StartCapture(h, Text(*command, "device"), Number(*command, "ms", 5000), Number(*command, "id", 0));
  else if (what == "rumble" && port >= 0 && port < 4)
    StartRumble(h, port, Number(*command, "ms", 300));
  else
    Problem("not a command: " + line.substr(0, 200));
}

void SayState(Helper& h)
{
  SayNew(&h.said_devices, DevicesLine());
  SayNew(&h.said_ports, PortsLine(h.kinds));
  SayNew(&h.said_adapter, AdapterLine(h.adapter_asked, AdapterWanted(h)));
}

// The launcher's lines are read on a thread of their own: reading blocks, and the inputs are read meanwhile.
void ReadLines(Helper* h)
{
  std::string line;
  while (std::getline(std::cin, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::lock_guard lock(h->lines_lock);
    h->lines.push_back(line);
  }
  const std::lock_guard lock(h->lines_lock);
  h->input_closed = true;
}

// One line of the launcher's, or the word that there will be no more.
bool NextLine(Helper& h, std::string* line, bool* closed)
{
  const std::lock_guard lock(h.lines_lock);
  if (!h.lines.empty())
  {
    *line = h.lines.front();
    h.lines.pop_front();
    return true;
  }
  *closed = h.input_closed;
  return false;
}

// The input layer finds its devices on threads of its own: "ready" is said once the list has stood still.
void WaitForDevices()
{
  const Time start = std::chrono::steady_clock::now();
  Time changed = start;
  std::string before;
  for (;;)
  {
    const Time now = std::chrono::steady_clock::now();
    const std::string devices = DevicesLine();
    if (devices != before)
    {
      before = devices;
      changed = now;
    }
    if (now - changed >= SETTLE || now - start >= SETTLE_LIMIT)
      return;
    std::this_thread::sleep_for(milliseconds(25));
  }
}
}  // namespace

void RunControllerHelper(const std::filesystem::path& user_folder)
{
  Helper h;
  StartInput(user_folder);
  ReadKinds(h);
  WaitForDevices();
  SayState(h);
  Say("{\"is\":\"ready\",\"version\":1}");
  std::thread(ReadLines, &h).detach();

  Time slow = std::chrono::steady_clock::now();
  for (bool closed = false; !closed;)
  {
    std::string line;
    while (NextLine(h, &line, &closed))
      Obey(h, line);
    const Time now = std::chrono::steady_clock::now();
    g_controller_interface.UpdateInput();
    Capture(h);
    if (h.rumbling >= 0 && now >= h.rumble_end)
      StopRumble(h);
    if (h.watched >= 0)
      SayNew(&h.said_pad, PadLine(h.watched, h.kinds[h.watched], h.adapter_asked));
    if (now - slow >= SLOW_TICK)
    {
      slow = now;
      SayState(h);
    }
    std::this_thread::sleep_for(TICK);
  }
  StopRumble(h);
  std::fflush(stdout);
  // Nothing of the emulator's is closed in order: its end would save the settings, and this program writes none.
  std::_Exit(0);
}
}  // namespace fzgx_port
