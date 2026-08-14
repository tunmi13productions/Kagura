#ifndef KAGURA_LOG_H
#define KAGURA_LOG_H
#pragma once

#include <string>

// Kagura writes everything to a plain text file next to the plugin as well as to
// the framework console. Reading that file costs nothing, whereas attaching to
// the game's console buffer from outside is expensive enough to stutter the game.
class Log
{
public:
    static void Initialize(const std::wstring& pluginDir);
    static void Shutdown();

    static void Write(const std::string& line);
    static void Write(const std::wstring& line);

    static const std::wstring& Path() { return _path; }

private:
    static std::wstring _path;
};

#endif
