// The emulator's own lines that matter, into the game's log. The emulator writes its log (dolphin.log) only when
// its settings say so, which a test's do and a player's do not; so what the game itself reports (its "GP WAIT
// Timeout... (sending object a/b)" when its wait for the graphics side runs out) and the emulator's errors (an
// unknown opcode in the command list) were lost in a player's game. This listener takes those lines, and only
// those: every error, and the notices of the game's own report channel, each kind with a budget of its own and a
// run of like lines written once (fzgx/loglines.h, tested in the core).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter_internal.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Common/Config/Config.h"
#include "Common/Logging/LogManager.h"

#include "fzgx/loglines.h"

namespace fzgx_port
{
namespace
{
using Common::Log::LogLevel;
using Common::Log::LogListener;
using Common::Log::LogManager;
using Common::Log::LogType;
using fzgx::loglines::Filter;

// What is turned on for it: the game's two report channels, and the parts whose errors say something about a game
// that stops or stands (the picture's side, the CPU, the core). The emulator's verbosity is raised to "error" when
// it is lower. Each is put back as it was when the game has stopped, so the user folder's Logger.ini is not changed.
// Only parts whose being on changes nothing but what is logged (read in the emulator's source): the
// "Host GPU" part is not among them, because the Vulkan backend makes a debug messenger and OpenGL turns its debug
// output on when that part is enabled, in every game, on the picture's thread. Of the emulator's other tests of
// "is this part enabled" (the audio library's callback, the disc's file monitor, the frame dump) none is of a
// part named here.
constexpr LogType TYPES[] = {LogType::OSREPORT,    LogType::OSREPORT_HLE, LogType::VIDEO, LogType::COMMANDPROCESSOR,
                             LogType::PIXELENGINE, LogType::POWERPC,      LogType::CORE,  LogType::MASTER_LOG};
std::vector<bool> s_were_on;
bool s_listener_was_on = false;
std::atomic<bool> s_taking{false};
// The listener is called from any thread of the emulator's: the filter and what it holds back are under this.
std::mutex s_lines_mutex;
Filter s_filter;
std::vector<fzgx::loglines::Folder> s_folders;

void Write(const std::vector<std::string>& lines, bool error)
{
  for (const std::string& line : lines)
    s_core_host.Log(error && line[0] != '(' ? fzgx::LogLevel::Error : fzgx::LogLevel::Notice, "fzgx: emulator: " + line);
}

class Lines final : public LogListener
{
public:
  // `text`: "MM:SS:mmm FILE:LINE L[TYPE]: the message\n", L being N (notice), E (error), W, I or D.
  void Log(LogLevel level, const char* text) override
  {
    const bool report = level == LogLevel::LNOTICE && std::strstr(text, "N[OSREPORT") != nullptr;
    if (level != LogLevel::LERROR && !report)
      return;
    // without the time of day and the source file: from the level's letter on
    const char* from = text;
    if (const char* space = std::strchr(text, ' '))
      if (const char* second = std::strchr(space + 1, ' '))
        from = second + 1;
    std::string line(from);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    std::lock_guard<std::mutex> guard(s_lines_mutex);
    Write(s_filter.Take(report ? Filter::Kind::Report : Filter::Kind::Error, fzgx::loglines::WithoutFolders(line, s_folders)),
          !report);
  }
};

// The player's folders a line may name: the user folder, and the account's own folder when the user folder is
// under one (Users/NAME on a drive, /home/NAME). Any other absolute path is cut to its file's name by the filter.
void KnowTheFolders()
{
  const std::u8string folder = s_state.user_folder.u8string();
  const std::string user(folder.begin(), folder.end());
  s_folders = {{user, "<user folder>"}};
  for (const char* users : {"\\Users\\", "/Users/", "/home/"})
    if (const std::size_t at = user.find(users); at != std::string::npos)
      if (const std::size_t end = user.find_first_of("\\/", at + std::strlen(users)); end != std::string::npos)
        s_folders.push_back({user.substr(0, end), "<home>"});
}
}  // namespace

void TakeEmulatorLines()
{
  LogManager* log = LogManager::GetInstance();
  if (!log || s_taking.exchange(true))
    return;
  {
    std::lock_guard<std::mutex> guard(s_lines_mutex);
    s_filter = Filter();
    KnowTheFolders();
  }
  s_were_on.clear();
  for (const LogType type : TYPES)
  {
    s_were_on.push_back(log->IsEnabled(type, LogLevel::LNOTICE));
    log->SetEnable(type, true);
  }
  if (Config::Get(Common::Log::LOGGER_VERBOSITY) < LogLevel::LERROR)
    Config::SetCurrent(Common::Log::LOGGER_VERBOSITY, LogLevel::LERROR);
  s_listener_was_on = log->IsListenerEnabled(LogListener::LOG_WINDOW_LISTENER);
  log->RegisterListener(LogListener::LOG_WINDOW_LISTENER, std::make_unique<Lines>());
  log->EnableListener(LogListener::LOG_WINDOW_LISTENER, true);
}

// What the filter holds back ("(N more like it)") is written now: before a freeze's report, and at the stop.
void FlushEmulatorLines()
{
  std::lock_guard<std::mutex> guard(s_lines_mutex);
  Write(s_filter.Flush(), false);
}

void LeaveEmulatorLines()
{
  LogManager* log = LogManager::GetInstance();
  if (!s_taking.exchange(false))
    return;
  FlushEmulatorLines();
  if (!log)
    return;
  log->EnableListener(LogListener::LOG_WINDOW_LISTENER, s_listener_was_on);
  log->RegisterListener(LogListener::LOG_WINDOW_LISTENER, nullptr);
  for (std::size_t i = 0; i < s_were_on.size(); ++i)
    log->SetEnable(TYPES[i], s_were_on[i]);
}
}  // namespace fzgx_port
