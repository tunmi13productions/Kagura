#ifndef KAGURA_ANNOUNCER_H
#define KAGURA_ANNOUNCER_H
#pragma once

#include "MessageHook.h"

#include <string>
#include <vector>

// Turns the stream of resolved game messages into screen reader announcements.
//
// The game resolves exactly one message per menu selection change, and that
// message is the highlighted item's *description* - never its name. Names are
// supplied by a data file (names.txt) mapping message ID -> spoken label, so new
// menus can be covered by editing text rather than rebuilding the plugin.
class Announcer
{
public:
    static void Initialize(const std::wstring& pluginDir);

    // (Re)reads names.txt. Safe to call while the game is running.
    static size_t LoadNames();
    static size_t NameCount();

    // True when names.txt supplies a spoken label for this message ID.
    static bool HasName(const std::string& messageId);

    static void SetEnabled(bool on);
    static bool IsEnabled();

    // Re-speaks the last announcement, interrupting whatever is being read.
    // Works even while muted, so it can be used to check what was missed.
    static void RepeatLast();

    // Verbose appends the description after the mapped name.
    static void SetVerbose(bool on);
    static bool IsVerbose();

    // Speaks newly resolved messages. One selection change resolves several messages
    // in a rapid burst; rather than delaying them behind a timer, each is spoken
    // immediately and only the ones that follow player input interrupt what is
    // already being read. See the interrupt decision in Process().
    static void Process(const std::vector<MessageHook::Entry>& entries);

    // Removes the game's inline markup: <color red>..</color>, <icon .. />, [<string />].
    static std::string StripMarkup(const std::string& text);
};

#endif
