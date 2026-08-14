#ifndef KAGURA_MESSAGEHOOK_H
#define KAGURA_MESSAGEHOOK_H
#pragma once

#include <string>
#include <vector>

// Intercepts the game's message-ID -> display-string resolver so Kagura can see
// every piece of text the game renders, as it is rendered.
//
// The Storm Framework leaves this function unpatched in the installed build, so
// Kagura owns it outright and installs a proper trampoline rather than the
// framework's unhook/call/rehook approach (which races across threads).
class MessageHook
{
public:
    struct Entry
    {
        std::string id;
        std::string text;
    };

    static bool Install(__int64 moduleBase);
    static void Remove();

    static bool IsInstalled() { return _installed; }
    static unsigned long long ResolveCount();

    // Capture is off by default: the game resolves text constantly and we only
    // want the stream when actively investigating.
    static void SetCapture(bool on);
    static bool IsCapturing() { return _capturing; }

    // Moves everything captured since the last call out of the ring buffer.
    static std::vector<Entry> Drain();

    // Most recently resolved string, regardless of capture state.
    static std::string LastText();

private:
    static bool _installed;
    static volatile bool _capturing;
};

#endif
