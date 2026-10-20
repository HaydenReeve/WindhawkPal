// ==WindhawkMod==
// @id              replace-start-with-command-palette
// @name            Replace Start with Command Palette
// @description     Tap the Win key to open PowerToys Command Palette in place of the Start menu
// @version         0.2.0
// @author          HaydenReeve
// @github          https://github.com/HaydenReeve
// @include         windhawk-mod-uiaccess.exe
// @compilerOptions -lshell32 -lole32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Replace Start with Command Palette

Tap the Win key and PowerToys Command Palette opens in place of Start.

Start's search runs on Windows Search, and its results are slow to trust. Most
of the menu is space you never use. Command Palette ranks apps well, and
extensions such as Everything make file search excellent.

This mod sends a lone Win key tap to Command Palette, and a second tap closes
it. Keys you type straight after the tap land in the search box, as they do in
Start. Win+key shortcuts and the Start button work as usual, and disabling the
mod brings the stock Start menu back.

A tap still counts while you hold a key pressed before it, such as a
push-to-talk key.

In the settings, you can pick a palette page to open or a command to run. The
palette must be running.

## How it works

The mod runs in Windhawk's UI access process, so it also works while an
elevated window has focus.

A low-level keyboard hook decides when a Win press is a tap. On a tap, the hook
sends an unassigned key before Windows sees the Win release, so the Start menu
stays shut. The mod then opens the palette or runs the command.

While the mod's process is stopped, the Win key opens the Start menu.

The mod does nothing while Sticky Keys is on.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- action: palette
  $name: Action
  $description: What a Win tap does
  $options:
  - palette: Open Command Palette
  - command: Run a command
- pageHotkey: ''
  $name: Page hotkey
  $description: >-
    A Command Palette command hotkey that opens a page, such as an extension's
    search. Leave it empty to open the palette's home page
  $format: hotkey
- paletteHotkey: alt+win+32
  $name: Palette hotkey
  $description: >-
    Command Palette's activation hotkey, as set in Command Palette. A tap sends
    it to close the palette while the palette is in front
  $format: hotkey
- command: ''
  $name: Command
  $description: >-
    The program, file, folder, or URI a Win tap opens when Action is "Run a
    command"
- arguments: ''
  $name: Arguments
  $description: Arguments passed to the command
*/
// ==/WindhawkModSettings==

#include <windhawk_tool_mod.h>
#include <windhawk_utils.h>

#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

// Tags the mod's injected input so its hooks skip it.
constexpr ULONG_PTR kMarker = 0x57484B54;

// An unassigned virtual key, injected before a tap's Win key-up so Start stays
// shut.
constexpr WORD kMaskKey = 0xE8;

constexpr UINT kMsgTap = WM_APP + 1;
constexpr UINT kMsgReloadSettings = WM_APP + 2;
constexpr UINT kMsgClose = WM_APP + 3;

// How long typed keys wait for the palette before they go to whichever window
// has focus.
constexpr UINT kFlushDeadlineMs = 400;

// How long to wait for the palette's search box after the palette reaches the
// foreground, when no focus event arrives.
constexpr UINT kSettleMs = 50;

// How long Enter waits after the typed text, so the results can update.
constexpr UINT kEnterDelayMs = 150;

constexpr WCHAR kPaletteProcess[] = L"Microsoft.CmdPal.UI.exe";
constexpr WCHAR kShowEvent[] =
    L"Local\\PowerToysCmdPal-ShowEvent-62336fcd-8611-4023-9b30-091a6af4cc5a";

struct Hotkey {
    bool ctrl, alt, shift, win;
    WORD vk;
};

struct Settings {
    bool runCommand;
    Hotkey pageHotkey;
    Hotkey paletteHotkey;
    std::wstring command;
    std::wstring arguments;
} g_settings;

DWORD g_threadId;
HANDLE g_thread;
HHOOK g_keyboardHook;
HHOOK g_mouseHook;
constexpr DWORD kWinEvents[] = {EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_FOCUS,
                                 EVENT_SYSTEM_DESKTOPSWITCH};
HWINEVENTHOOK g_winEventHooks[ARRAYSIZE(kWinEvents)];

// Key state as the hook has seen it, which separates an initial key-down from
// an auto-repeat.
bool g_keyDown[256];

bool g_winDown;
bool g_chord;

// The palette process, refreshed on the hook thread's message loop.
DWORD g_palettePid;
HANDLE g_paletteProcess;

// Typed keys held from a tap until the palette has focus.
bool g_buffering;
bool g_paletteForeground;
bool g_heldKey[256];
bool g_sentDown[256];
std::vector<INPUT> g_buffer;

bool g_enterWait;

UINT_PTR g_deadlineTimer;
UINT_PTR g_settleTimer;
UINT_PTR g_enterTimer;

void StopTimer(UINT_PTR& timer) {
    if (timer) {
        KillTimer(nullptr, timer);
        timer = 0;
    }
}

void StartTimer(UINT_PTR& timer, UINT ms) {
    StopTimer(timer);
    timer = SetTimer(nullptr, 0, ms, nullptr);
}

Hotkey ParseHotkey(PCWSTR text) {
    // The stored form is [ctrl+][alt+][shift+][win+]<vk>.
    Hotkey hotkey{};
    hotkey.ctrl = wcsstr(text, L"ctrl+") != nullptr;
    hotkey.alt = wcsstr(text, L"alt+") != nullptr;
    hotkey.shift = wcsstr(text, L"shift+") != nullptr;
    hotkey.win = wcsstr(text, L"win+") != nullptr;
    PCWSTR key = wcsrchr(text, L'+');
    hotkey.vk = (WORD)_wtoi(key ? key + 1 : text);
    return hotkey;
}

std::wstring GetString(PCWSTR name) {
    return WindhawkUtils::StringSetting::make(name).get();
}

void LoadSettings() {
    g_settings.runCommand = GetString(L"action") == L"command";
    g_settings.pageHotkey = ParseHotkey(GetString(L"pageHotkey").c_str());
    g_settings.paletteHotkey = ParseHotkey(GetString(L"paletteHotkey").c_str());
    g_settings.command = GetString(L"command");
    g_settings.arguments = GetString(L"arguments");
}

// Keys that MapVirtualKey maps to their numpad scan codes unless flagged.
bool IsExtendedKey(WORD vk) {
    return (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT ||
           vk == VK_DELETE || vk == VK_DIVIDE || vk == VK_RCONTROL ||
           vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS;
}

INPUT MakeKey(WORD vk, bool up) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (IsExtendedKey(vk)) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    input.ki.dwExtraInfo = kMarker;
    return input;
}

void Send(const std::vector<INPUT>& inputs) {
    if (inputs.empty()) {
        return;
    }
    UINT sent = SendInput((UINT)inputs.size(), const_cast<INPUT*>(inputs.data()),
                          sizeof(INPUT));
    if (sent != inputs.size()) {
        DWORD error = GetLastError();
        Wh_Log(L"SendInput sent %u of %zu: %u", sent, inputs.size(), error);
    }
}

void SendHotkey(const Hotkey& hotkey) {
    if (!hotkey.vk) {
        return;
    }

    std::vector<WORD> modifiers;
    if (hotkey.ctrl) modifiers.push_back(VK_CONTROL);
    if (hotkey.alt) modifiers.push_back(VK_MENU);
    if (hotkey.shift) modifiers.push_back(VK_SHIFT);
    if (hotkey.win) modifiers.push_back(VK_LWIN);

    std::vector<INPUT> inputs;
    for (WORD vk : modifiers) {
        inputs.push_back(MakeKey(vk, false));
    }
    inputs.push_back(MakeKey(hotkey.vk, false));
    inputs.push_back(MakeKey(hotkey.vk, true));
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
        inputs.push_back(MakeKey(*it, true));
    }
    Send(inputs);
}

bool IsPaletteRunning() {
    return g_paletteProcess &&
           WaitForSingleObject(g_paletteProcess, 0) == WAIT_TIMEOUT;
}

void RefreshPaletteProcess() {
    if (IsPaletteRunning()) {
        return;
    }

    if (g_paletteProcess) {
        CloseHandle(g_paletteProcess);
        g_paletteProcess = nullptr;
    }
    g_palettePid = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok;
         ok = Process32NextW(snapshot, &entry)) {
        DWORD entrySession;
        if (_wcsicmp(entry.szExeFile, kPaletteProcess) != 0 ||
            !ProcessIdToSessionId(entry.th32ProcessID, &entrySession) ||
            entrySession != session) {
            continue;
        }
        g_paletteProcess = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (g_paletteProcess) {
            g_palettePid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snapshot);
}

bool IsPaletteWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return g_palettePid && pid == g_palettePid;
}

bool IsStickyKeysOn() {
    STICKYKEYS stickyKeys{sizeof(stickyKeys)};
    return SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(stickyKeys),
                                 &stickyKeys, 0) &&
           (stickyKeys.dwFlags & SKF_STICKYKEYSON);
}

void StopBuffering() {
    g_buffering = false;
    g_paletteForeground = false;
    g_enterWait = false;
    StopTimer(g_deadlineTimer);
    StopTimer(g_settleTimer);
    StopTimer(g_enterTimer);
    ZeroMemory(g_heldKey, sizeof(g_heldKey));
    ZeroMemory(g_sentDown, sizeof(g_sentDown));
    g_buffer.clear();
}

// Sends the key-ups still owed for replayed key-downs and drops the rest, so no
// key stays down.
void ReleaseAndStop() {
    std::vector<INPUT> ups;
    for (const INPUT& input : g_buffer) {
        if ((input.ki.dwFlags & KEYEVENTF_KEYUP) && g_sentDown[input.ki.wVk]) {
            ups.push_back(input);
        }
    }
    StopBuffering();
    Send(ups);
}

void FlushAll() {
    std::vector<INPUT> inputs = std::move(g_buffer);
    StopBuffering();
    Send(inputs);
}

// Replays the held keys up to the first Enter. The Enter and every key after it
// wait for the results to update, and buffering continues meanwhile so no
// key-up overtakes its key-down.
void Flush() {
    if (!g_buffering) {
        return;
    }
    if (g_enterWait) {
        FlushAll();
        return;
    }

    size_t i = 0;
    while (i < g_buffer.size() && !(g_buffer[i].ki.wVk == VK_RETURN &&
                                    !(g_buffer[i].ki.dwFlags & KEYEVENTF_KEYUP))) {
        i++;
    }
    if (i == g_buffer.size()) {
        FlushAll();
        return;
    }

    std::vector<INPUT> text(g_buffer.begin(), g_buffer.begin() + i);
    g_buffer.erase(g_buffer.begin(), g_buffer.begin() + i);
    for (const INPUT& input : text) {
        g_sentDown[input.ki.wVk] = !(input.ki.dwFlags & KEYEVENTF_KEYUP);
    }
    g_enterWait = true;
    StopTimer(g_deadlineTimer);
    StopTimer(g_settleTimer);
    StartTimer(g_enterTimer, kEnterDelayMs);
    Send(text);
}

void HoldKey(const KBDLLHOOKSTRUCT* key) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = (WORD)key->vkCode;
    input.ki.wScan = (WORD)key->scanCode;
    if (key->flags & LLKHF_EXTENDED) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (key->flags & LLKHF_UP) input.ki.dwFlags |= KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = kMarker;
    g_buffer.push_back(input);
}

// Runs inside the keyboard hook on a tap's Win key-up. Windows drops a
// low-level hook that overruns its timeout, so slow work goes to the message
// loop.
void OnTap() {
    Send({MakeKey(kMaskKey, false), MakeKey(kMaskKey, true)});

    // The close hotkey waits for the message loop, after the Win key-up, so a
    // hotkey without Win is not sent as Win plus that hotkey.
    if (!g_settings.runCommand && IsPaletteWindow(GetForegroundWindow())) {
        PostThreadMessageW(g_threadId, kMsgClose, 0, 0);
        return;
    }

    if (!g_settings.runCommand) {
        g_buffering = true;
        g_paletteForeground = false;
        StartTimer(g_deadlineTimer, kFlushDeadlineMs);
    }

    PostThreadMessageW(g_threadId, kMsgTap, 0, 0);
}

// ShellExecute can block, so it runs on its own thread.
struct Command {
    std::wstring file;
    std::wstring arguments;
};

DWORD WINAPI RunCommandThread(LPVOID param) {
    auto* command = static_cast<Command*>(param);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto result = (INT_PTR)ShellExecuteW(
        nullptr, nullptr, command->file.c_str(),
        command->arguments.empty() ? nullptr : command->arguments.c_str(), nullptr,
        SW_SHOWNORMAL);
    if (result <= 32) {
        DWORD error = GetLastError();
        Wh_Log(L"ShellExecute failed: %d, %u", (int)result, error);
    }
    CoUninitialize();
    delete command;
    return 0;
}

void Summon() {
    if (g_settings.runCommand) {
        if (!g_settings.command.empty()) {
            AllowSetForegroundWindow(ASFW_ANY);
            auto* command = new Command{g_settings.command, g_settings.arguments};
            HANDLE thread = CreateThread(nullptr, 0, RunCommandThread, command, 0,
                                         nullptr);
            if (thread) {
                CloseHandle(thread);
            } else {
                delete command;
            }
        }
        return;
    }

    RefreshPaletteProcess();
    if (!g_palettePid) {
        Wh_Log(L"Command Palette is not running");
        Flush();
        return;
    }

    AllowSetForegroundWindow(g_palettePid);
    if (g_settings.pageHotkey.vk) {
        SendHotkey(g_settings.pageHotkey);
        return;
    }

    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShowEvent);
    if (!event) {
        DWORD error = GetLastError();
        Wh_Log(L"OpenEvent failed: %u", error);
        Flush();
        return;
    }
    SetEvent(event);
    CloseHandle(event);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    auto* key = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vk = key->vkCode & 0xFF;
    if (key->dwExtraInfo == kMarker || vk == kMaskKey || vk == 0xFF) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    bool up = (key->flags & LLKHF_UP) != 0;
    bool initialDown = !up && !g_keyDown[vk];
    g_keyDown[vk] = !up;
    bool isWin = vk == VK_LWIN || vk == VK_RWIN;

    if (g_buffering && isWin && initialDown) {
        FlushAll();
    }

    if (g_buffering) {
        // Keys held since before the tap pass through, so a push-to-talk
        // release reaches its app straight away.
        if (initialDown) {
            g_heldKey[vk] = true;
        }
        if (g_heldKey[vk]) {
            if (up) {
                g_heldKey[vk] = false;
            }
            HoldKey(key);
            return 1;
        }
    }

    if (isWin && initialDown && !g_winDown) {
        g_winDown = true;
        g_chord = false;
    } else if (!isWin && initialDown && g_winDown) {
        g_chord = true;
    } else if (isWin && up && g_winDown && !g_keyDown[VK_LWIN] &&
               !g_keyDown[VK_RWIN]) {
        g_winDown = false;
        if (!g_chord && !IsStickyKeysOn()) {
            OnTap();
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam != WM_MOUSEMOVE) {
        if (g_winDown) {
            g_chord = true;
        }
        if (g_buffering && wParam != WM_MOUSEWHEEL && wParam != WM_MOUSEHWHEEL) {
            Flush();
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG, LONG,
                           DWORD, DWORD) {
    if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
        ReleaseAndStop();
        ZeroMemory(g_keyDown, sizeof(g_keyDown));
        g_winDown = false;
        return;
    }

    if (!g_buffering || g_enterWait) {
        return;
    }

    if (event == EVENT_SYSTEM_FOREGROUND) {
        g_paletteForeground = IsPaletteWindow(hwnd);
        if (g_paletteForeground) {
            StartTimer(g_settleTimer, kSettleMs);
        } else {
            StopTimer(g_settleTimer);
        }
    } else if (event == EVENT_OBJECT_FOCUS && g_paletteForeground &&
               IsPaletteWindow(hwnd)) {
        Flush();
    }
}

DWORD WINAPI HookThread(LPVOID readyEvent) {
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent((HANDLE)readyEvent);

    LoadSettings();
    RefreshPaletteProcess();

    HMODULE module = GetModuleHandleW(nullptr);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, module, 0);
    if (!g_keyboardHook) {
        DWORD error = GetLastError();
        Wh_Log(L"Keyboard hook failed: %u", error);
    }
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, module, 0);
    if (!g_mouseHook) {
        DWORD error = GetLastError();
        Wh_Log(L"Mouse hook failed: %u", error);
    }

    for (size_t i = 0; i < ARRAYSIZE(kWinEvents); i++) {
        g_winEventHooks[i] =
            SetWinEventHook(kWinEvents[i], kWinEvents[i], nullptr, WinEventProc,
                            0, 0, WINEVENT_OUTOFCONTEXT);
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        switch (msg.message) {
            case kMsgTap:
                Summon();
                break;
            case kMsgReloadSettings:
                LoadSettings();
                break;
            case kMsgClose:
                SendHotkey(g_settings.paletteHotkey);
                break;
            case WM_TIMER:
                // Past the deadline, the text and its Enter go to the same
                // window together.
                if (msg.wParam == g_deadlineTimer ||
                    msg.wParam == g_enterTimer) {
                    FlushAll();
                } else if (msg.wParam == g_settleTimer) {
                    Flush();
                }
                break;
            default:
                DispatchMessageW(&msg);
        }
    }

    FlushAll();
    for (HWINEVENTHOOK hook : g_winEventHooks) {
        UnhookWinEvent(hook);
    }
    UnhookWindowsHookEx(g_mouseHook);
    UnhookWindowsHookEx(g_keyboardHook);
    if (g_paletteProcess) {
        CloseHandle(g_paletteProcess);
    }
    return 0;
}

BOOL WhTool_ModInit() {
    // PostThreadMessage fails until the thread has a message queue, so wait for
    // the thread to create one.
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, HookThread, ready, 0, &g_threadId);
    if (g_thread) {
        WaitForSingleObject(ready, INFINITE);
    }
    CloseHandle(ready);
    return g_thread != nullptr;
}

void WhTool_ModUninit() {
    PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
}

void WhTool_ModSettingsChanged() {
    PostThreadMessageW(g_threadId, kMsgReloadSettings, 0, 0);
}
