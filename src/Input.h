#ifndef KAGURA_INPUT_H
#define KAGURA_INPUT_H
#pragma once

#include <windows.h>

// Tracks whether the player has actually done something (keyboard or controller).
//
// This is what tells an announcement apart from a continuation: a message that
// follows player input is a NEW selection and should cut off whatever is being
// spoken, whereas a message that arrives on its own is the rest of the same burst
// and should queue behind it.
class Input
{
public:
    static void Initialize();

    // Called every worker tick.
    static void Poll();

    static DWORD LastInputTime() { return _lastInputTime; }

    // True if input arrived after the given timestamp.
    static bool InputSince(DWORD when) { return _lastInputTime > when; }

    // Kagura's own hotkeys. Read-only observation via GetAsyncKeyState - the game
    // still receives these keys untouched, so nothing is stolen from it.
    static const int HotkeyMute = 0x77;    // VK_F8
    static const int HotkeyRepeat = 0x78;  // VK_F9

    // True exactly once per physical press, then cleared.
    static bool WasPressed(int virtualKey);

private:
    static DWORD _lastInputTime;
};

#endif
