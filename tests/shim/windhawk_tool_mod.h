// Stands in for Windhawk's windhawk_tool_mod.h in the unit tests. It routes the
// mod's input, message, timer, and window calls to recording fakes, so tests
// drive the hooks directly.
#pragma once

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <vector>

inline void Wh_Log(PCWSTR, ...) {}

namespace fake {

struct Posted {
    UINT message;
};

inline std::vector<INPUT> sent;
inline std::vector<Posted> posted;
inline HWND foreground;
inline DWORD foregroundPid;
inline bool stickyKeys;
inline UINT_PTR nextTimer = 1;

inline void Reset() {
    sent.clear();
    posted.clear();
    foreground = nullptr;
    foregroundPid = 0;
    stickyKeys = false;
}

inline UINT SendInput(UINT count, INPUT* inputs, int) {
    sent.insert(sent.end(), inputs, inputs + count);
    return count;
}

inline BOOL PostThreadMessageW(DWORD, UINT message, WPARAM, LPARAM) {
    posted.push_back({message});
    return TRUE;
}

inline UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC) { return nextTimer++; }
inline BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
inline HWND GetForegroundWindow() { return foreground; }

inline DWORD GetWindowThreadProcessId(HWND hwnd, LPDWORD pid) {
    *pid = hwnd == foreground ? foregroundPid : 0;
    return 1;
}
inline LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { return 0; }

inline BOOL SystemParametersInfoW(UINT, UINT, PVOID param, UINT) {
    auto* keys = static_cast<STICKYKEYS*>(param);
    keys->dwFlags = stickyKeys ? SKF_STICKYKEYSON : 0;
    return TRUE;
}

}  // namespace fake

#define SendInput fake::SendInput
#define PostThreadMessageW fake::PostThreadMessageW
#define SetTimer fake::SetTimer
#define KillTimer fake::KillTimer
#define GetForegroundWindow fake::GetForegroundWindow
#define GetWindowThreadProcessId fake::GetWindowThreadProcessId
#define CallNextHookEx fake::CallNextHookEx
#undef SystemParametersInfo
#define SystemParametersInfoW fake::SystemParametersInfoW
