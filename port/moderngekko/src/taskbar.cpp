// The game window on the Windows taskbar. Windows puts windows under one button when they carry one application
// id. A launcher with a window of its own gives its id in the launch file ("taskbar": id, program, name): the
// runner takes it for its process before the window is made, so the game window stands under the launcher's
// button as a second window of one program, and says on the window what a pinned button is (the launcher's
// program file, its name, its icon), so that a player who pins the button gets the launcher and never the bare
// runner. A start without the key, as every test's from a file of port/configs, keeps a button of its own.
// The window's icon is the app's, which the runner's program file carries (runner.rc); it is set here because the
// emulator's window class has none. Its two sizes are the launcher's window's, so that both windows show one
// picture wherever Windows shows a window's icon: 24 and 16 pixels at the screen's scaling.
// The window's part needs the frame's hook (frame.cpp): with "debug.windowControls" only the process's id is set.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef _WIN32

#include "adapter_internal.h"

#include <windows.h>

#include <objbase.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

#include "Common/StringUtil.h"

namespace fzgx_port
{
namespace
{
constexpr int ICON_RESOURCE = 1;  // runner.rc
// The window's large icon is the taskbar's size, 24, as the launcher's toolkit gives its window; the small one 16.
constexpr int LARGE_ICON = 24, SMALL_ICON = 16;
// Windows' window properties "System.AppUserModel.*": what a pinned button starts, its icon, its name, and the id
constexpr GUID APP_MODEL = {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
constexpr PROPERTYKEY RELAUNCH_COMMAND = {APP_MODEL, 2}, RELAUNCH_ICON = {APP_MODEL, 3}, RELAUNCH_NAME = {APP_MODEL, 4},
                      APP_ID = {APP_MODEL, 5};

HICON s_large = nullptr, s_small = nullptr;

// One property of the window; an empty text takes it off.
void Set(IPropertyStore* store, const PROPERTYKEY& key, const std::wstring& text)
{
  PROPVARIANT value{};
  if (!text.empty())
  {
    value.vt = VT_LPWSTR;
    value.pwszVal = const_cast<wchar_t*>(text.c_str());  // the store keeps a copy of its own
  }
  store->SetValue(key, value);
}

// The window's properties as the launch file's group says, or all of them taken off (`group` null).
void SetGroup(HWND window, const fzgx::TaskbarGroup* group)
{
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IPropertyStore* store = nullptr;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))) && store)
  {
    const std::wstring program = group ? UTF8ToWString(group->program) : std::wstring();
    Set(store, RELAUNCH_NAME, group ? UTF8ToWString(group->name) : std::wstring());
    Set(store, RELAUNCH_ICON, group ? program + L",0" : std::wstring());
    Set(store, RELAUNCH_COMMAND, group ? L"\"" + program + L"\"" : std::wstring());
    Set(store, APP_ID, group ? UTF8ToWString(group->id) : std::wstring());
    store->Release();
  }
  if (SUCCEEDED(com))
    CoUninitialize();
}

// The icon at `points` pixels of an unscaled screen: the picture of the file that is nearest that size.
HICON Icon(int points, int dpi)
{
  const int size = MulDiv(points, dpi, 96);
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ICON_RESOURCE), IMAGE_ICON, size,
                                       size, LR_DEFAULTCOLOR));
}
}  // namespace

void JoinTaskbarGroup()
{
  const fzgx::TaskbarGroup& group = s_state.options.taskbar;
  if (group.id.empty())
    return;
  const bool taken = SUCCEEDED(SetCurrentProcessExplicitAppUserModelID(UTF8ToWString(group.id).c_str()));
  s_core_host.Log(taken ? fzgx::LogLevel::Notice : fzgx::LogLevel::Warning,
                  taken ? "fzgx: the game window is under the launcher's taskbar button (" + group.id + ")" :
                          "fzgx: Windows did not take the launcher's taskbar id; the game window has its own button");
}

void DressWindow(void* handle)
{
  const HWND window = static_cast<HWND>(handle);
  const int dpi = GetDpiForWindow(window) != 0 ? static_cast<int>(GetDpiForWindow(window)) : 96;
  s_large = Icon(LARGE_ICON, dpi);
  s_small = Icon(SMALL_ICON, dpi);
  if (s_large)
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(s_large));
  if (s_small)
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(s_small));
  if (!s_state.options.taskbar.id.empty())
    SetGroup(window, &s_state.options.taskbar);
}

void UndressWindow(void* handle)
{
  // Windows asks for the properties to be taken off before a window goes
  if (!s_state.options.taskbar.id.empty())
    SetGroup(static_cast<HWND>(handle), nullptr);
  for (HICON* icon : {&s_large, &s_small})
  {
    if (*icon)
      DestroyIcon(*icon);
    *icon = nullptr;
  }
}
}  // namespace fzgx_port

#else

namespace fzgx_port
{
void JoinTaskbarGroup()
{
}
void DressWindow(void*)
{
}
void UndressWindow(void*)
{
}
}  // namespace fzgx_port

#endif
