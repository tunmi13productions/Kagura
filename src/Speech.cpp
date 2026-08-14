#include "Speech.h"
#include "PrismApi.h"
#include "Log.h"

#include <windows.h>
#include <sstream>

bool Speech::_ready = false;
std::wstring Speech::_readerName = L"none";

namespace
{
    HMODULE       g_prism = nullptr;
    PrismContext* g_ctx = nullptr;
    PrismBackend* g_backend = nullptr;

    typedef PrismConfig  (__cdecl *fn_config_init)(void);
    typedef PrismContext*(__cdecl *fn_init)(PrismConfig*);
    typedef void         (__cdecl *fn_shutdown)(PrismContext*);
    typedef PrismBackend*(__cdecl *fn_acquire_best)(PrismContext*);
    typedef size_t       (__cdecl *fn_registry_count)(PrismContext*);
    typedef const char*  (__cdecl *fn_backend_name)(PrismBackend*);
    typedef PrismError   (__cdecl *fn_backend_initialize)(PrismBackend*);
    typedef PrismError   (__cdecl *fn_backend_output)(PrismBackend*, const char*, bool);
    typedef PrismError   (__cdecl *fn_backend_speak)(PrismBackend*, const char*, bool);
    typedef PrismError   (__cdecl *fn_backend_stop)(PrismBackend*);
    typedef const char*  (__cdecl *fn_error_string)(PrismError);

    fn_config_init        p_config_init = nullptr;
    fn_init               p_init = nullptr;
    fn_shutdown           p_shutdown = nullptr;
    fn_acquire_best       p_acquire_best = nullptr;
    fn_registry_count     p_registry_count = nullptr;
    fn_backend_name       p_backend_name = nullptr;
    fn_backend_initialize p_backend_initialize = nullptr;
    fn_backend_output     p_backend_output = nullptr;
    fn_backend_speak      p_backend_speak = nullptr;
    fn_backend_stop       p_backend_stop = nullptr;
    fn_error_string       p_error_string = nullptr;

    // Not every backend implements output() (speech + braille); those fall back to speak().
    bool g_useOutput = true;

    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();

        int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return std::string();

        std::string out((size_t)need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], need, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWideLocal(const std::string& s)
    {
        if (s.empty()) return std::wstring();

        int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (need <= 0) return std::wstring();

        std::wstring out((size_t)need, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
        return out;
    }

    template <typename T>
    bool Bind(T& target, const char* name)
    {
        target = (T)GetProcAddress(g_prism, name);
        if (!target) Log::Write(std::string("Kagura :: prism.dll is missing ") + name);
        return target != nullptr;
    }
}

// The game's message table is UTF-8; fall back to the ANSI code page if it isn't.
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();

    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    if (need <= 0)
    {
        cp = CP_ACP;
        need = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
    }
    if (need <= 0) return std::wstring();

    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

bool Speech::Initialize(const std::wstring& pluginDir)
{
    if (_ready) return true;

    std::wstring prismPath = pluginDir + L"\\lib\\prism.dll";

    // LOAD_WITH_ALTERED_SEARCH_PATH so Prism's own folder is searched for the
    // optional reader DLLs it delay-loads.
    g_prism = LoadLibraryExW(prismPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_prism)
    {
        std::wostringstream msg;
        msg << L"Kagura :: prism.dll not loaded from " << prismPath << L" (error " << GetLastError() << L")";
        Log::Write(msg.str());
        return false;
    }

    bool ok = true;
    ok &= Bind(p_config_init,        "prism_config_init");
    ok &= Bind(p_init,               "prism_init");
    ok &= Bind(p_shutdown,           "prism_shutdown");
    ok &= Bind(p_acquire_best,       "prism_registry_acquire_best");
    ok &= Bind(p_backend_name,       "prism_backend_name");
    ok &= Bind(p_backend_initialize, "prism_backend_initialize");
    ok &= Bind(p_backend_output,     "prism_backend_output");
    ok &= Bind(p_backend_speak,      "prism_backend_speak");
    ok &= Bind(p_backend_stop,       "prism_backend_stop");

    // Optional - used only for diagnostics.
    Bind(p_registry_count, "prism_registry_count");
    Bind(p_error_string,   "prism_error_string");

    if (!ok)
    {
        FreeLibrary(g_prism);
        g_prism = nullptr;
        return false;
    }

    PrismConfig cfg = p_config_init();
    cfg.version = PRISM_CONFIG_VERSION;

    g_ctx = p_init(&cfg);
    if (!g_ctx)
    {
        Log::Write("Kagura :: prism_init failed");
        FreeLibrary(g_prism);
        g_prism = nullptr;
        return false;
    }

    _ready = true;

    if (p_registry_count)
    {
        std::ostringstream msg;
        msg << "Kagura :: Prism ready, " << p_registry_count(g_ctx) << " backends registered";
        Log::Write(msg.str());
    }

    Redetect();

    Log::Write(L"Kagura :: screen reader = " + _readerName);
    return true;
}

void Speech::Shutdown()
{
    if (!_ready) return;

    // The backend is owned by the context; shutting the context down releases it.
    g_backend = nullptr;

    if (g_ctx && p_shutdown) p_shutdown(g_ctx);
    g_ctx = nullptr;

    if (g_prism) FreeLibrary(g_prism);
    g_prism = nullptr;

    _ready = false;
}

bool Speech::Redetect()
{
    if (!_ready || !g_ctx) return false;

    // Prism polls backend availability in the background, so re-asking for the best
    // one picks up a screen reader that started after the game did.
    PrismBackend* best = p_acquire_best(g_ctx);
    if (!best)
    {
        g_backend = nullptr;
        _readerName = L"none";
        return false;
    }

    if (best != g_backend)
    {
        // acquire_best hands back a SHARED backend the context already initialized,
        // unlike create_best which returns a fresh one. Initializing it again is
        // harmless but reports ALREADY_INITIALIZED, which is not a failure.
        PrismError err = p_backend_initialize(best);
        if (err != PRISM_OK && err != PRISM_ERROR_ALREADY_INITIALIZED)
        {
            std::ostringstream msg;
            msg << "Kagura :: backend initialize failed: "
                << (p_error_string ? p_error_string(err) : "error");
            Log::Write(msg.str());
            return false;
        }

        g_backend = best;
        g_useOutput = true;

        const char* name = p_backend_name(best);
        _readerName = name ? Utf8ToWideLocal(name) : L"unknown";
    }

    return true;
}

void Speech::Say(const std::wstring& text, bool interrupt)
{
    Say(WideToUtf8(text), interrupt);
}

void Speech::Say(const std::string& utf8Text, bool interrupt)
{
    if (!_ready || !g_backend || utf8Text.empty()) return;

    // output() drives speech and braille together; not all backends provide it.
    if (g_useOutput)
    {
        if (p_backend_output(g_backend, utf8Text.c_str(), interrupt) == PRISM_OK) return;
        g_useOutput = false;
    }

    p_backend_speak(g_backend, utf8Text.c_str(), interrupt);
}

void Speech::Silence()
{
    if (_ready && g_backend) p_backend_stop(g_backend);
}
