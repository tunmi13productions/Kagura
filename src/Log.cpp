#include "Log.h"

#include <windows.h>
#include <iostream>

std::wstring Log::_path;

namespace
{
    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    // Appends UTF-8 bytes to the log. Opened and closed per write so the file is
    // always complete on disk and can be read while the game is running.
    void AppendUtf8(const std::string& utf8)
    {
        HANDLE h = CreateFileW(Log::Path().c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        std::string line = utf8 + "\r\n";
        DWORD written = 0;
        WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr);
        CloseHandle(h);
    }

    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return std::string();

        std::string out((size_t)need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], need, nullptr, nullptr);
        return out;
    }
}

void Log::Initialize(const std::wstring& pluginDir)
{
    _path = pluginDir + L"\\kagura.log";

    if (!g_ready)
    {
        InitializeCriticalSection(&g_lock);
        g_ready = true;
    }

    // Start each run with a clean file so stale lines are never mistaken for new ones.
    HANDLE h = CreateFileW(_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    Write("=== Kagura log start ===");
}

void Log::Shutdown()
{
    if (!g_ready) return;
    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

void Log::Write(const std::string& line)
{
    std::cout << line << std::endl;

    if (!g_ready || _path.empty()) return;

    EnterCriticalSection(&g_lock);
    AppendUtf8(line);
    LeaveCriticalSection(&g_lock);
}

void Log::Write(const std::wstring& line)
{
    Write(WideToUtf8(line));
}
