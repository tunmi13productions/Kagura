#ifndef KAGURA_MAIN_H
#define KAGURA_MAIN_H
#pragma once

#include <string>
#include <vector>

// Kagura - accessibility plugin for NARUTO SHIPPUDEN: Ultimate Ninja STORM 4.
// Runs as a plugin under the Storm Framework (ns4moddingapi).
//
// The framework's per-frame dispatch (GameLoop / InitializeHooks) never reaches
// plugins in this build, so Kagura drives itself from its own worker thread.
class Main
{
public:
    static __int64 moduleBase;
    static unsigned long long frameCount;   // framework GameLoop ticks (may stay 0)
    static unsigned long long tickCount;    // our own worker thread ticks
    static std::wstring pluginDir;

    static void StartWorker();
    static void StopWorker();
};

extern "C"
{
    __declspec(dllexport) void __stdcall InitializePlugin(__int64 moduleBase, std::vector<__int64> deprecated);
    __declspec(dllexport) void __stdcall InitializeCommands(__int64 moduleBase, __int64 addCommandFunctionAddress);
    __declspec(dllexport) void __stdcall InitializeHooks(__int64 moduleBase, __int64 hookFunctionAddress);
    __declspec(dllexport) void __stdcall GameLoop(__int64 a);
    __declspec(dllexport) void __stdcall Unload();
}

#endif
