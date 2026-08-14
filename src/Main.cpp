#include "Main.h"
#include "Log.h"
#include "Speech.h"
#include "GameText.h"
#include "MessageHook.h"
#include "Announcer.h"
#include "Input.h"

#include <windows.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

__int64 Main::moduleBase = 0;
unsigned long long Main::frameCount = 0;
unsigned long long Main::tickCount = 0;
wstring Main::pluginDir;

namespace
{
    HMODULE g_self = nullptr;
    HANDLE  g_worker = nullptr;
    volatile bool g_running = false;

    void Dispatch(const string& line);

    // Console command entry points
    void StatusCommand();
    void SayCommand();
    void MsgCommand();
    void LogCommand();

    wstring DirectoryOfSelf()
    {
        wchar_t path[MAX_PATH] = { 0 };
        if (!GetModuleFileNameW(g_self, path, MAX_PATH)) return wstring();

        wstring full(path);
        size_t slash = full.find_last_of(L"\\/");
        return (slash == wstring::npos) ? wstring() : full.substr(0, slash);
    }

    string ReadConsoleToken()
    {
        string s;
        cin >> s;
        return s;
    }

    string Trim(const string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == string::npos) return string();
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    // Kagura is driven from outside by dropping a text file next to the plugin.
    // Reading the game's console buffer from another process is expensive enough
    // to stutter the game, so this is the control channel instead.
    void PollCommandFile()
    {
        wstring cmdPath = Main::pluginDir + L"\\kagura.cmd";

        HANDLE h = CreateFileW(cmdPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        char buffer[4096] = { 0 };
        DWORD read = 0;
        ReadFile(h, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(h);
        DeleteFileW(cmdPath.c_str());

        if (read == 0) return;

        istringstream stream(string(buffer, read));
        string line;
        while (getline(stream, line))
        {
            line = Trim(line);
            if (!line.empty()) Dispatch(line);
        }
    }

    DWORD WINAPI WorkerThread(LPVOID)
    {
        Log::Write("Kagura :: Worker thread started");

        bool announcedReady = false;

        while (g_running)
        {
            Main::tickCount++;

            // Must come before announcing: the interrupt decision depends on whether
            // the player has just done something.
            Input::Poll();

            // F8 mutes/unmutes, F9 repeats the last announcement. Both are read-only
            // observations, so the game still receives the keys.
            if (Input::WasPressed(Input::HotkeyMute))
            {
                bool on = !Announcer::IsEnabled();
                Announcer::SetEnabled(on);
                Log::Write(string("Kagura :: announcements ") + (on ? "ON" : "OFF"));

                // Spoken directly, bypassing the announcer, so the unmute
                // confirmation is still audible when muting.
                Speech::Say(string(on ? "Kagura on." : "Kagura muted."), true);
            }

            if (Input::WasPressed(Input::HotkeyRepeat)) Announcer::RepeatLast();

            // The screen reader may not be up yet when the game loads, so keep
            // retrying detection (about once a second) until one answers.
            if (!announcedReady && Main::tickCount % 60 == 0)
            {
                if (Speech::Redetect())
                {
                    announcedReady = true;
                    Log::Write(L"Kagura :: screen reader detected: " + Speech::ScreenReaderName());
                    Speech::Say(L"Kagura ready.");
                }
            }

            // The detour records on the game's UI thread and never speaks or writes
            // files; both happen here instead, off the hot path.
            vector<MessageHook::Entry> entries = MessageHook::Drain();
            if (!entries.empty())
            {
                if (MessageHook::IsCapturing())
                {
                    for (size_t i = 0; i < entries.size(); i++)
                    {
                        // Tagged so the menus still needing a names.txt entry can be
                        // pulled straight out of the log.
                        const char* tag = Announcer::HasName(entries[i].id) ? "  [msg]      " : "  [unmapped] ";
                        Log::Write(tag + entries[i].id + " = " + entries[i].text);
                    }
                }

                Announcer::Process(entries);
            }

            if (Main::tickCount % 15 == 0) PollCommandFile();

            Sleep(16);
        }

        Log::Write("Kagura :: Worker thread stopped");
        return 0;
    }
}

void Main::StartWorker()
{
    if (g_worker) return;
    g_running = true;
    g_worker = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
}

void Main::StopWorker()
{
    if (!g_worker) return;
    g_running = false;
    WaitForSingleObject(g_worker, 2000);
    CloseHandle(g_worker);
    g_worker = nullptr;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

void __stdcall InitializePlugin(__int64 moduleBase, std::vector<__int64> deprecated)
{
    Main::moduleBase = moduleBase;
    Main::pluginDir = DirectoryOfSelf();

    Log::Initialize(Main::pluginDir);

    ostringstream base;
    base << "Kagura :: loaded, moduleBase = 0x" << hex << moduleBase;
    Log::Write(base.str());

    GameText::Initialize(moduleBase);
    Speech::Initialize(Main::pluginDir);
    Announcer::Initialize(Main::pluginDir);
    Input::Initialize();
    MessageHook::Install(moduleBase);

    // The framework never ticks plugins in this build, so run our own loop.
    Main::StartWorker();
}

void __stdcall InitializeCommands(__int64 moduleBase, __int64 addCommandFunctionAddress)
{
    typedef void(__stdcall *AddCmd)(std::string command, __int64 function, int paramcount);
    AddCmd AddCommand = (AddCmd)addCommandFunctionAddress;

    AddCommand("kagura", (__int64)StatusCommand, 0);
    AddCommand("ksay",   (__int64)SayCommand,    1);
    AddCommand("kmsg",   (__int64)MsgCommand,    1);
    AddCommand("klog",   (__int64)LogCommand,    0);

    Log::Write("Kagura :: Commands registered: kagura, ksay, kmsg, klog");
}

void __stdcall InitializeHooks(__int64 moduleBase, __int64 hookFunctionAddress)
{
    Log::Write("Kagura :: InitializeHooks called");
}

void __stdcall GameLoop(__int64 a)
{
    Main::frameCount++;
}

void __stdcall Unload()
{
    Main::StopWorker();
    MessageHook::Remove();
    Speech::Shutdown();
    Log::Write("Kagura :: Unloaded");
    Log::Shutdown();
}

namespace
{
    void ReportStatus()
    {
        ostringstream s;
        s << "Kagura :: alive"
          << " | moduleBase=0x" << hex << Main::moduleBase << dec
          << " | workerTicks=" << Main::tickCount
          << " | frameworkFrames=" << Main::frameCount;
        Log::Write(s.str());

        Log::Write(L"Kagura :: speech=" +
                   wstring(Speech::IsReady() ? L"ready" : L"UNAVAILABLE") +
                   L" reader=" + Speech::ScreenReaderName());

        ostringstream h;
        h << "Kagura :: msgHook=" << (MessageHook::IsInstalled() ? "installed" : "NOT INSTALLED")
          << " resolved=" << MessageHook::ResolveCount()
          << " capture=" << (MessageHook::IsCapturing() ? "on" : "off");
        Log::Write(h.str());

        ostringstream a;
        a << "Kagura :: announce=" << (Announcer::IsEnabled() ? "on" : "off")
          << " verbose=" << (Announcer::IsVerbose() ? "on" : "off")
          << " names=" << Announcer::NameCount();
        Log::Write(a.str());

        string last = MessageHook::LastText();
        if (!last.empty()) Log::Write("Kagura :: last text = " + last);
    }

    void SetCapture(bool on)
    {
        MessageHook::SetCapture(on);
        Log::Write(string("Kagura :: message capture ") + (on ? "ON" : "OFF"));
    }

    void SpeakLiteral(const string& text)
    {
        if (text.empty()) return;
        Speech::Say(text);
        Log::Write("Kagura :: spoke \"" + text + "\"");
    }

    void SpeakMessageId(const string& id)
    {
        if (id.empty()) return;

        string resolved = GameText::Resolve(id);
        if (resolved.empty())
        {
            Log::Write("Kagura :: no text for \"" + id + "\"");
            return;
        }

        Log::Write("Kagura :: \"" + id + "\" -> " + resolved);
        Speech::Say(resolved);
    }

    // dump <prefix> [max] - walk a prefix's numeric ID space and log everything the
    // game can resolve. The message table itself is sealed inside the .cpk archives,
    // so the running game is used as the oracle instead. Suffix width varies by
    // screen (collect_getinfo_23, gamemodeselect_011, main_outline_00000), so each
    // candidate is tried at several widths.
    void DumpMessages(const string& args)
    {
        istringstream in(args);
        string prefix;
        int max = 0;
        in >> prefix >> max;

        if (prefix.empty())
        {
            Log::Write("Kagura :: usage: dump <prefix> [max]");
            return;
        }
        if (max <= 0) max = 300;

        // Resolving runs through our own hook, which would otherwise announce every
        // hit and flood the screen reader.
        bool wasAnnouncing = Announcer::IsEnabled();
        Announcer::SetEnabled(false);

        const int widths[] = { 3, 2, 5, 1 };
        int found = 0;

        Log::Write("Kagura :: dump \"" + prefix + "\" begin");

        for (int n = 0; n <= max; n++)
        {
            for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++)
            {
                ostringstream id;
                id << prefix << "_" << setw(widths[w]) << setfill('0') << n;

                string text = GameText::Resolve(id.str());
                string alt  = GameText::ResolveAlt(id.str());

                if (text.empty() && alt.empty()) continue;

                // The two resolvers disagree on purpose. On the command list one
                // returns the input ("<icon stick_l />") and the other the label
                // ("Move"), so both are logged when they differ.
                if (!text.empty()) Log::Write("  [dump] " + id.str() + " = " + text);
                if (!alt.empty() && alt != text) Log::Write("  [alt]  " + id.str() + " = " + alt);

                found++;
                break;   // one width matched; do not report the same entry twice
            }
        }

        ostringstream done;
        done << "Kagura :: dump \"" << prefix << "\" end, " << found << " found";
        Log::Write(done.str());

        MessageHook::Drain();               // discard what the dump pushed into the ring
        Announcer::SetEnabled(wasAnnouncing);
    }

    // Shared by the console commands and the command file.
    void Dispatch(const string& line)
    {
        istringstream parts(line);
        string verb;
        parts >> verb;

        string rest;
        getline(parts, rest);
        rest = Trim(rest);

        if (verb == "status")       ReportStatus();
        else if (verb == "log")     SetCapture(rest != "off");
        else if (verb == "say")     SpeakLiteral(rest);
        else if (verb == "msg")     SpeakMessageId(rest);
        else if (verb == "silence") Speech::Silence();
        else if (verb == "names")   Announcer::LoadNames();
        else if (verb == "announce") Announcer::SetEnabled(rest != "off");
        else if (verb == "verbose") Announcer::SetVerbose(rest != "off");
        else if (verb == "dump")    DumpMessages(rest);
        else Log::Write("Kagura :: unknown command \"" + verb + "\"");
    }

    void StatusCommand() { ReportStatus(); }

    void LogCommand() { SetCapture(!MessageHook::IsCapturing()); }

    void SayCommand()
    {
        cout << "SAY >> ";
        SpeakLiteral(ReadConsoleToken());
    }

    void MsgCommand()
    {
        cout << "MSG >> ";
        SpeakMessageId(ReadConsoleToken());
    }
}
