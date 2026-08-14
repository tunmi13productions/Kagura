#ifndef KAGURA_SPEECH_H
#define KAGURA_SPEECH_H
#pragma once

#include <string>

// Screen reader output via Prism (https://github.com/ethindp/prism).
//
// Prism unifies NVDA, JAWS, Orca, SAPI, OneCore, UIA, ZDSR and others behind one
// API, and picks the best available backend itself. It is loaded dynamically at
// runtime, so Kagura links against no import lib and still builds and runs on a
// machine with no screen reader installed.
class Speech
{
public:
    // Loads Prism from <plugin dir>\lib\prism.dll. Returns false if unavailable.
    static bool Initialize(const std::wstring& pluginDir);
    static void Shutdown();

    static bool IsReady() { return _ready; }
    static const std::wstring& ScreenReaderName() { return _readerName; }

    // Re-runs screen reader detection; the reader may start after the game does.
    // Returns true once a reader is present.
    static bool Redetect();

    // interrupt: cut off whatever the reader is currently saying.
    static void Say(const std::wstring& text, bool interrupt = true);
    static void Say(const std::string& utf8Text, bool interrupt = true);
    static void Silence();

private:
    static bool _ready;
    static std::wstring _readerName;
};

// Game text is 8-bit; convert it for the screen reader.
std::wstring Utf8ToWide(const std::string& s);

#endif
