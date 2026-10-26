// The controller helper's words: the launcher's commands read, and the helper's lines written (controllers.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controllers.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI_Device.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/GCAdapter.h"
#include "InputCommon/GCPadStatus.h"
#include "InputCommon/InputConfig.h"

namespace fzgx_port::controllers
{
namespace
{
// A command is short: a line longer than this is none.
constexpr std::size_t LONGEST_COMMAND = 4096;

struct Reader
{
  const std::string& s;
  std::size_t at = 0;

  void Blank()
  {
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
      ++at;
  }
  bool Take(char c)
  {
    Blank();
    if (at >= s.size() || s[at] != c)
      return false;
    ++at;
    return true;
  }
  bool Word(const char* word)
  {
    const std::size_t n = std::strlen(word);
    if (s.compare(at, n, word) != 0)
      return false;
    at += n;
    return true;
  }
  // A JSON string, into UTF-8.
  bool Text(std::string* out)
  {
    if (!Take('"'))
      return false;
    out->clear();
    while (at < s.size() && s[at] != '"')
    {
      char c = s[at++];
      if (c == '\\')
      {
        if (at >= s.size())
          return false;
        const char e = s[at++];
        if (e == 'n')
          c = '\n';
        else if (e == 't')
          c = '\t';
        else if (e == 'r')
          c = '\r';
        else if (e == 'b')
          c = '\b';
        else if (e == 'f')
          c = '\f';
        else if (e == 'u')
        {
          // one character of the basic plane, written as UTF-8 (a name's letters; half of a pair is none)
          if (at + 4 > s.size())
            return false;
          const unsigned long code = std::strtoul(s.substr(at, 4).c_str(), nullptr, 16);
          at += 4;
          if (code >= 0xD800 && code <= 0xDFFF)
            return false;
          if (code >= 0x800)
          {
            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            c = static_cast<char>(0x80 | (code & 0x3F));
          }
          else if (code >= 0x80)
          {
            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
            c = static_cast<char>(0x80 | (code & 0x3F));
          }
          else
            c = static_cast<char>(code);
        }
        else
          c = e;  // \" \\ \/
      }
      out->push_back(c);
    }
    return Take('"');
  }
  bool One(Value* v)
  {
    Blank();
    if (at >= s.size())
      return false;
    if (s[at] == '"')
    {
      v->kind = Value::Kind::Text;
      return Text(&v->text);
    }
    if (Word("null"))
      return true;
    if (Word("true") || Word("false"))
    {
      v->kind = Value::Kind::Flag;
      v->flag = s[at - 2] == 'u';  // "true" ends in "ue", "false" in "se"
      return true;
    }
    char* end = nullptr;
    v->number = std::strtod(s.c_str() + at, &end);
    if (end == s.c_str() + at)
      return false;
    v->kind = Value::Kind::Number;
    at = static_cast<std::size_t>(end - s.c_str());
    return true;
  }
};

std::string Flag(bool on)
{
  return on ? "true" : "false";
}

// The adapter's state from what the emulator says of it: found, or its own (libusb's) words for why it could not be
// opened. Those words are the only thing the emulator hands out, so they are read here: no driver Windows could use
// ("not supported", "not found"), or another program holds it ("access denied", "busy").
std::string AdapterState(bool wanted, std::string* detail)
{
  if (!wanted)
    return "unused";
  const char* error = nullptr;
  if (GCAdapter::IsDetected(&error))
    return "found";
  if (!error)
    return "none";
  *detail = error;
  std::string low = *detail;
  for (char& c : low)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (low.find("not supported") != std::string::npos || low.find("not found") != std::string::npos)
    return "noDriver";
  if (low.find("access") != std::string::npos || low.find("busy") != std::string::npos)
    return "inUse";
  return "error";
}
}  // namespace

std::optional<Command> ReadCommand(const std::string& line)
{
  if (line.size() > LONGEST_COMMAND)
    return std::nullopt;
  Reader r{line};
  Command command;
  if (!r.Take('{'))
    return std::nullopt;
  if (!r.Take('}'))
  {
    do
    {
      std::string key;
      Value value;
      if (!r.Text(&key) || !r.Take(':') || !r.One(&value))
        return std::nullopt;
      command[key] = value;
    } while (r.Take(','));
    if (!r.Take('}'))
      return std::nullopt;
  }
  r.Blank();
  return r.at == line.size() ? std::optional<Command>(command) : std::nullopt;
}

std::string Quoted(const std::string& text)
{
  std::string out = "\"";
  for (const char c : text)
  {
    if (c == '"' || c == '\\')
    {
      out.push_back('\\');
      out.push_back(c);
    }
    else if (static_cast<unsigned char>(c) < 0x20)
    {
      char code[8];
      std::snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
      out += code;
    }
    else
      out.push_back(c);
  }
  return out + "\"";
}

std::string DevicesLine()
{
  std::string out = "{\"is\":\"devices\",\"default\":" + Quoted(g_controller_interface.GetDefaultDeviceString()) +
                    ",\"devices\":[";
  bool first = true;
  for (const auto& device : g_controller_interface.GetAllDevices())
  {
    const std::string source = device->GetSource();
    // SDL names a gamepad's buttons by where they are ("Button S", the lower one); a stick, a wheel or a pad SDL has
    // no layout for has numbered buttons and axes instead, and is mapped by hand
    const bool gamepad = source == "SDL" && device->FindInput("Button S") != nullptr;
    out += std::string(first ? "" : ",") + "{\"id\":" + Quoted(device->GetQualifiedName()) + ",\"backend\":" +
           Quoted(source) + ",\"index\":" + std::to_string(device->GetId()) + ",\"name\":" + Quoted(device->GetName()) +
           ",\"gamepad\":" + Flag(gamepad) + ",\"rumble\":" + Flag(!device->Outputs().empty()) + "}";
    first = false;
  }
  return out + "]}";
}

std::string PortsLine(const std::array<int, 4>& kinds)
{
  std::string out = "{\"is\":\"ports\",\"ports\":[";
  for (int port = 0; port < 4; ++port)
  {
    const ControllerEmu::EmulatedController* pad = Pad::GetConfig()->GetController(port);
    out += std::string(port ? "," : "") + "{\"kind\":" + std::to_string(kinds[port]) + ",\"device\":" +
           Quoted(pad->GetDefaultDevice().ToString()) + ",\"connected\":" + Flag(pad->IsDefaultDeviceConnected()) + "}";
  }
  return out + "]}";
}

std::string AdapterLine(bool asked, bool wanted)
{
  std::string detail;
  const std::string state = asked ? AdapterState(wanted, &detail) : "notAsked";
  std::string out = "{\"is\":\"adapter\",\"state\":" + Quoted(state) + ",\"detail\":" + Quoted(detail) + ",\"ports\":[";
  for (int port = 0; port < 4; ++port)
    out += std::string(port ? "," : "") + Flag(state == "found" && GCAdapter::DeviceConnected(port));
  return out + "]}";
}

std::string PadLine(int port, int kind, bool adapter_asked)
{
  static constexpr struct
  {
    u16 bit;
    const char* name;
  } BUTTONS[] = {
      {PAD_BUTTON_A, "A"},       {PAD_BUTTON_B, "B"},       {PAD_BUTTON_X, "X"},        {PAD_BUTTON_Y, "Y"},
      {PAD_TRIGGER_Z, "Z"},      {PAD_BUTTON_START, "Start"}, {PAD_TRIGGER_L, "L"},     {PAD_TRIGGER_R, "R"},
      {PAD_BUTTON_UP, "Up"},     {PAD_BUTTON_DOWN, "Down"}, {PAD_BUTTON_LEFT, "Left"},  {PAD_BUTTON_RIGHT, "Right"},
  };
  GCPadStatus pad{};
  pad.isConnected = false;
  if (kind == SerialInterface::SIDEVICE_WIIU_ADAPTER)
  {
    if (adapter_asked)
      pad = GCAdapter::Input(port);
  }
  else if (kind != SerialInterface::SIDEVICE_NONE)
    pad = Pad::GetStatus(port);  // every other kind of device reads the port's mapping
  std::string out = "{\"is\":\"pad\",\"port\":" + std::to_string(port) + ",\"kind\":" + std::to_string(kind) +
                    ",\"connected\":" + Flag(pad.isConnected) + ",\"buttons\":[";
  bool first = true;
  for (const auto& button : BUTTONS)
  {
    if (pad.isConnected && (pad.button & button.bit))
    {
      out += std::string(first ? "" : ",") + Quoted(button.name);
      first = false;
    }
  }
  return out + "],\"stick\":[" + std::to_string(pad.stickX) + "," + std::to_string(pad.stickY) + "],\"cStick\":[" +
         std::to_string(pad.substickX) + "," + std::to_string(pad.substickY) + "],\"l\":" +
         std::to_string(pad.triggerLeft) + ",\"r\":" + std::to_string(pad.triggerRight) + "}";
}
}  // namespace fzgx_port::controllers
