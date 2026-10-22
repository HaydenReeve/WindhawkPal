// Unit tests for the mod's input logic. The mod source compiles here against
// the fakes in shim/, and each test feeds events straight to its hooks.

#ifndef MOD_SOURCE
#define MOD_SOURCE "../mods/replace-start-with-command-palette.wh.cpp"
#endif
#include MOD_SOURCE

#include <cstdio>

int g_failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                        #condition);                                    \
            g_failures++;                                               \
        }                                                               \
    } while (0)

constexpr DWORD kPalettePid = 1234;
const HWND kPaletteWindow = (HWND)0x1;

void Reset() {
    StopBuffering();
    ZeroMemory(g_keyDown, sizeof(g_keyDown));
    g_winDown = false;
    g_chord = false;
    g_palettePid = 0;
    fake::Reset();
    fake::settings.clear();
    LoadSettings();
}

LRESULT Key(DWORD vk, bool up, DWORD scanCode = 0) {
    KBDLLHOOKSTRUCT key{};
    key.vkCode = vk;
    key.scanCode = scanCode ? scanCode : MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    key.flags = up ? LLKHF_UP : 0;
    return KeyboardProc(HC_ACTION, up ? WM_KEYUP : WM_KEYDOWN, (LPARAM)&key);
}

void Press(DWORD vk) {
    Key(vk, false);
    Key(vk, true);
}

int PostedCount(UINT message) {
    int count = 0;
    for (const auto& posted : fake::posted) {
        count += posted.message == message;
    }
    return count;
}

int SentDowns(WORD vk) {
    int count = 0;
    for (const INPUT& input : fake::sent) {
        count += input.ki.wVk == vk && !(input.ki.dwFlags & KEYEVENTF_KEYUP);
    }
    return count;
}

bool Tapped() { return PostedCount(kMsgTap) > 0; }
int MasksSent() { return SentDowns(kMaskKey); }

void PlainTapSummonsAndMasksStart() {
    Reset();
    Press(VK_LWIN);
    CHECK(Tapped());
    CHECK(MasksSent() == 1);
    CHECK(g_buffering);
}

void HeldKeyRepeatsKeepTheTap() {
    Reset();
    Key(VK_F13, false);
    Key(VK_LWIN, false);
    Key(VK_F13, false);
    Key(VK_F13, false);
    Key(VK_LWIN, true);
    CHECK(Tapped());
}

void NewKeyWithWinIsAChord() {
    Reset();
    Key(VK_LWIN, false);
    Press('E');
    Key(VK_LWIN, true);
    CHECK(!Tapped());
    CHECK(MasksSent() == 0);
}

void CtrlRolledIntoWinIsATap() {
    Reset();
    Key(VK_LCONTROL, false);
    Key(VK_LWIN, false);
    Key(VK_LCONTROL, true);
    Key(VK_LWIN, true);
    CHECK(Tapped());
    CHECK(MasksSent() == 1);
}

void CtrlHeldThroughWinIsAChordThatMasksStart() {
    Reset();
    Key(VK_LCONTROL, false);
    Key(VK_LWIN, false);
    Key(VK_LWIN, true);
    Key(VK_LCONTROL, true);
    CHECK(!Tapped());
    CHECK(MasksSent() == 1);
}

void WinClickIsAChordThatMasksStartOnce() {
    Reset();
    Key(VK_LWIN, false);
    OnMouseButton(false);
    OnMouseButton(false);
    Key(VK_LWIN, true);
    CHECK(!Tapped());
    CHECK(MasksSent() == 1);
}

void StickyKeysTurnsTheModOff() {
    Reset();
    fake::stickyKeys = true;
    Press(VK_LWIN);
    CHECK(!Tapped());
    CHECK(MasksSent() == 0);
}

void TapWithPaletteInFrontCloses() {
    Reset();
    g_palettePid = kPalettePid;
    fake::foreground = kPaletteWindow;
    fake::foregroundPid = kPalettePid;
    Press(VK_LWIN);
    CHECK(PostedCount(kMsgClose) == 1);
    CHECK(!Tapped());
    CHECK(!g_buffering);
}

void TypedKeysWaitForThePalette() {
    Reset();
    Press(VK_LWIN);
    fake::sent.clear();
    CHECK(Key('A', false) == 1);
    CHECK(Key('A', true) == 1);
    CHECK(fake::sent.empty());
    Flush();
    CHECK(SentDowns('A') == 1);
    CHECK(!g_buffering);
}

void EnterWaitsForTheResults() {
    Reset();
    Press(VK_LWIN);
    fake::sent.clear();
    Press('A');
    Press(VK_RETURN);
    Press('B');
    Flush();
    CHECK(SentDowns('A') == 1);
    CHECK(SentDowns(VK_RETURN) == 0);
    CHECK(g_enterWait);
    FlushAll();
    CHECK(SentDowns(VK_RETURN) == 1);
    CHECK(SentDowns('B') == 1);
}

void UnicodeTextReplaysItsCharacter() {
    Reset();
    Press(VK_LWIN);
    fake::sent.clear();
    Key(VK_PACKET, false, 0xE9);
    Key(VK_PACKET, true, 0xE9);
    Flush();
    CHECK(fake::sent.size() == 2);
    CHECK(fake::sent[0].ki.wVk == 0);
    CHECK(fake::sent[0].ki.wScan == 0xE9);
    CHECK(fake::sent[0].ki.dwFlags & KEYEVENTF_UNICODE);
}

void DesktopSwitchReleasesReplayedKeys() {
    Reset();
    Press(VK_LWIN);
    Key('A', false);
    Press(VK_RETURN);
    Flush();
    Key('A', true);
    fake::sent.clear();
    WinEventProc(nullptr, EVENT_SYSTEM_DESKTOPSWITCH, nullptr, 0, 0, 0, 0);
    CHECK(fake::sent.size() == 1);
    CHECK(fake::sent[0].ki.wVk == 'A');
    CHECK(fake::sent[0].ki.dwFlags & KEYEVENTF_KEYUP);
    CHECK(!g_buffering);
}

int main() {
    struct {
        const char* name;
        void (*run)();
    } tests[] = {
#define TEST(name) {#name, name}
        TEST(PlainTapSummonsAndMasksStart),
        TEST(HeldKeyRepeatsKeepTheTap),
        TEST(NewKeyWithWinIsAChord),
        TEST(CtrlRolledIntoWinIsATap),
        TEST(CtrlHeldThroughWinIsAChordThatMasksStart),
        TEST(WinClickIsAChordThatMasksStartOnce),
        TEST(StickyKeysTurnsTheModOff),
        TEST(TapWithPaletteInFrontCloses),
        TEST(TypedKeysWaitForThePalette),
        TEST(EnterWaitsForTheResults),
        TEST(UnicodeTextReplaysItsCharacter),
        TEST(DesktopSwitchReleasesReplayedKeys),
#undef TEST
    };
    for (const auto& test : tests) {
        int before = g_failures;
        test.run();
        std::printf("%s %s\n", g_failures == before ? "pass" : "FAIL",
                    test.name);
    }
    return g_failures ? 1 : 0;
}
