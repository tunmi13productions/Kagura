#include "MessageHook.h"
#include "GameText.h"
#include "Log.h"

#include <windows.h>
#include <sstream>
#include <deque>

bool MessageHook::_installed = false;
// Capture defaults on: the log is the raw material for mapping menus, and there is
// no way to know in advance which screen the user is about to walk through.
volatile bool MessageHook::_capturing = true;

namespace
{
    typedef char* (__fastcall *MessageToStringFn)(const char*);

    const size_t kPatchLen = 12;

    // NS4 1.09 prologue shared by both resolvers:
    //   48 83 EC 28    sub  rsp, 28h
    //   48 85 C9       test rcx, rcx
    //   74 ??          je   <short>      <- displacement differs per function
    //   48 8B D1       mov  rdx, rcx
    // Instruction boundaries land at 4, 7, 9 and 12, so a 12-byte absolute-jump
    // patch never splits an instruction.
    bool MatchesPrologue(const unsigned char* p)
    {
        return p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC && p[3] == 0x28
            && p[4] == 0x48 && p[5] == 0x85 && p[6] == 0xC9
            && p[7] == 0x74
            && p[9] == 0x48 && p[10] == 0x8B && p[11] == 0xD1;
    }

    struct HookSlot
    {
        unsigned char*    target;
        unsigned char*    trampoline;
        unsigned char     original[kPatchLen];
        MessageToStringFn callOriginal;
        bool              installed;
    };

    HookSlot g_slots[2] = { 0 };

    volatile unsigned long long g_resolveCount = 0;

    CRITICAL_SECTION               g_lock;
    bool                           g_lockReady = false;
    std::deque<MessageHook::Entry> g_ring;
    std::string                    g_lastText;
    const size_t                   kRingMax = 512;

    void WriteAbsoluteJump(unsigned char* at, const void* destination)
    {
        // mov rax, imm64 ; jmp rax   (12 bytes)
        // rax is volatile and holds no argument at a function entry, so clobbering it is safe.
        at[0] = 0x48;
        at[1] = 0xB8;
        *(unsigned __int64*)(at + 2) = (unsigned __int64)destination;
        at[10] = 0xFF;
        at[11] = 0xE0;
    }

    // Rebuilds the 12 displaced bytes at a new address. The short `je` among them is
    // PC-relative, so it is inverted into a jump over an absolute jump to the original
    // branch destination rather than copied verbatim.
    unsigned char* BuildTrampoline(unsigned char* target)
    {
        unsigned char* t = (unsigned char*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!t) return nullptr;

        // Taken branch of the original `je`: (target + 9) + displacement
        unsigned char* branchTarget = target + 9 + (signed char)target[8];

        memcpy(t + 0, target + 0, 7);      // sub rsp,28h ; test rcx,rcx

        t[7] = 0x75;                       // jne +12 -> skips the absolute jump below
        t[8] = 0x0C;

        WriteAbsoluteJump(t + 9, branchTarget);

        memcpy(t + 21, target + 9, 3);     // mov rdx,rcx

        WriteAbsoluteJump(t + 24, target + kPatchLen);

        return t;
    }

    void Record(const char* id, const char* text)
    {
        if (!g_lockReady) return;

        EnterCriticalSection(&g_lock);

        g_lastText = text ? text : "";

        // Always recorded: the announcer consumes this stream continuously, and the
        // capture flag only controls whether the worker also writes it to the log.
        MessageHook::Entry e;
        e.id = id ? id : "";
        e.text = g_lastText;
        g_ring.push_back(e);
        while (g_ring.size() > kRingMax) g_ring.pop_front();

        LeaveCriticalSection(&g_lock);
    }

    // Runs on the game's UI thread, so it stays cheap and never speaks or writes files.
    char* Handle(int slot, const char* messageId)
    {
        char* result = g_slots[slot].callOriginal(messageId);

        g_resolveCount++;
        if (messageId && result) Record(messageId, result);

        return result;
    }

    // A jmp carries no context, so each slot needs its own entry point.
    char* __fastcall Detour0(const char* messageId) { return Handle(0, messageId); }
    char* __fastcall Detour1(const char* messageId) { return Handle(1, messageId); }

    bool InstallSlot(int index, unsigned char* target, void* detour, const char* label)
    {
        HookSlot& slot = g_slots[index];
        if (slot.installed) return true;

        // Refuse to patch anything that is not the shape we expect. A game patch or a
        // competing mod must not turn into a silent crash.
        if (!MatchesPrologue(target))
        {
            std::ostringstream msg;
            msg << "Kagura :: " << label << " hook ABORTED - unexpected bytes at 0x"
                << std::hex << (unsigned __int64)target
                << " (game updated, or another mod hooked it first)";
            Log::Write(msg.str());
            return false;
        }

        slot.trampoline = BuildTrampoline(target);
        if (!slot.trampoline)
        {
            Log::Write(std::string("Kagura :: ") + label + " hook failed - could not allocate trampoline");
            return false;
        }

        memcpy(slot.original, target, kPatchLen);
        slot.target = target;
        slot.callOriginal = (MessageToStringFn)slot.trampoline;

        DWORD oldProtect = 0;
        VirtualProtect(target, kPatchLen, PAGE_EXECUTE_READWRITE, &oldProtect);
        WriteAbsoluteJump(target, detour);
        VirtualProtect(target, kPatchLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), target, kPatchLen);

        slot.installed = true;

        std::ostringstream msg;
        msg << "Kagura :: " << label << " hook installed at 0x" << std::hex << (unsigned __int64)target;
        Log::Write(msg.str());
        return true;
    }

    void RemoveSlot(int index)
    {
        HookSlot& slot = g_slots[index];
        if (!slot.installed || !slot.target) return;

        DWORD oldProtect = 0;
        VirtualProtect(slot.target, kPatchLen, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(slot.target, slot.original, kPatchLen);
        VirtualProtect(slot.target, kPatchLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), slot.target, kPatchLen);

        slot.installed = false;
        slot.target = nullptr;

        // The trampoline is deliberately leaked: a game thread may still be inside it.
    }
}

unsigned long long MessageHook::ResolveCount() { return g_resolveCount; }

bool MessageHook::Install(__int64 moduleBase)
{
    if (!g_lockReady)
    {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }

    bool a = InstallSlot(0, (unsigned char*)(moduleBase + GameText::OffsetMessageToString),  (void*)Detour0, "msgtostring");
    bool b = InstallSlot(1, (unsigned char*)(moduleBase + GameText::OffsetMessageToString3), (void*)Detour1, "msgtostring3");

    _installed = a || b;
    return _installed;
}

void MessageHook::Remove()
{
    RemoveSlot(0);
    RemoveSlot(1);
    _installed = false;
}

void MessageHook::SetCapture(bool on)
{
    if (!g_lockReady) { _capturing = on; return; }

    EnterCriticalSection(&g_lock);
    if (on) g_ring.clear();
    _capturing = on;
    LeaveCriticalSection(&g_lock);
}

std::vector<MessageHook::Entry> MessageHook::Drain()
{
    std::vector<Entry> out;
    if (!g_lockReady) return out;

    EnterCriticalSection(&g_lock);
    out.assign(g_ring.begin(), g_ring.end());
    g_ring.clear();
    LeaveCriticalSection(&g_lock);

    return out;
}

std::string MessageHook::LastText()
{
    if (!g_lockReady) return std::string();

    EnterCriticalSection(&g_lock);
    std::string s = g_lastText;
    LeaveCriticalSection(&g_lock);

    return s;
}
