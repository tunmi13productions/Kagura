#include "Input.h"
#include "Log.h"

// GetAsyncKeyState lives in user32; nothing else in Kagura needs it.
#pragma comment(lib, "user32.lib")

DWORD Input::_lastInputTime = 0;

namespace
{
    // Navigation keys only. Kagura must never consume or alter game input - this is
    // a read-only observation of what the player is doing.
    const int kWatchedKeys[] = {
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        VK_RETURN, VK_SPACE, VK_ESCAPE, VK_BACK, VK_TAB,
        'W', 'A', 'S', 'D',
        'J', 'K', 'L', 'U', 'I', 'O'
    };
    const int kWatchedKeyCount = sizeof(kWatchedKeys) / sizeof(kWatchedKeys[0]);

    bool g_keyDown[kWatchedKeyCount] = { false };

    // Kagura's hotkeys, tracked separately from the navigation keys above: these
    // must NOT count as "the player did something" for the interrupt decision.
    const int kHotkeys[] = { Input::HotkeyMute, Input::HotkeyRepeat };
    const int kHotkeyCount = sizeof(kHotkeys) / sizeof(kHotkeys[0]);

    bool g_hotkeyDown[kHotkeyCount] = { false };
    bool g_hotkeyFired[kHotkeyCount] = { false };

    // XInput is resolved dynamically: the exact DLL varies by Windows version, and
    // the Storm Framework proxies xinput9_1_0 for its own injection.
    typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, void*);
    XInputGetStateFn g_getState = nullptr;

    DWORD g_lastPacket[4] = { 0 };

    // XINPUT_STATE begins with dwPacketNumber, which changes whenever the pad state
    // changes. That is all we need, so the rest of the struct is just padding.
    struct XInputStateLite
    {
        DWORD dwPacketNumber;
        BYTE  gamepad[16];
    };
}

void Input::Initialize()
{
    const wchar_t* candidates[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        HMODULE h = LoadLibraryW(candidates[i]);
        if (!h) continue;

        g_getState = (XInputGetStateFn)GetProcAddress(h, "XInputGetState");
        if (g_getState)
        {
            Log::Write(std::wstring(L"Kagura :: controller input via ") + candidates[i]);
            break;
        }
    }

    if (!g_getState) Log::Write("Kagura :: no XInput available (keyboard input only)");

    _lastInputTime = GetTickCount();
}

void Input::Poll()
{
    bool sawInput = false;

    for (int i = 0; i < kWatchedKeyCount; i++)
    {
        bool down = (GetAsyncKeyState(kWatchedKeys[i]) & 0x8000) != 0;

        // Only a fresh press counts; a held key is not a new selection.
        if (down && !g_keyDown[i]) sawInput = true;
        g_keyDown[i] = down;
    }

    if (g_getState)
    {
        for (DWORD pad = 0; pad < 4; pad++)
        {
            XInputStateLite state = { 0 };
            if (g_getState(pad, &state) != ERROR_SUCCESS) continue;

            if (state.dwPacketNumber != g_lastPacket[pad])
            {
                if (g_lastPacket[pad] != 0) sawInput = true;
                g_lastPacket[pad] = state.dwPacketNumber;
            }
        }
    }

    for (int i = 0; i < kHotkeyCount; i++)
    {
        bool down = (GetAsyncKeyState(kHotkeys[i]) & 0x8000) != 0;
        if (down && !g_hotkeyDown[i]) g_hotkeyFired[i] = true;
        g_hotkeyDown[i] = down;
    }

    if (sawInput) _lastInputTime = GetTickCount();
}

bool Input::WasPressed(int virtualKey)
{
    for (int i = 0; i < kHotkeyCount; i++)
    {
        if (kHotkeys[i] != virtualKey) continue;

        bool fired = g_hotkeyFired[i];
        g_hotkeyFired[i] = false;
        return fired;
    }

    return false;
}
