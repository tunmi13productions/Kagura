#include "GameText.h"

#include <windows.h>

__int64 GameText::_moduleBase = 0;

namespace
{
    // SEH must live in a function with no objects requiring unwinding (C2712 under /EHsc),
    // so the guarded call is kept free of C++ types.
    char* SafeResolve(__int64 address, const char* messageId)
    {
        typedef char* (__fastcall *MessageToString)(const char*);
        MessageToString resolve = (MessageToString)(address);

        __try
        {
            return resolve(messageId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
}

void GameText::Initialize(__int64 moduleBase)
{
    _moduleBase = moduleBase;
}

std::string GameText::ResolveAt(__int64 offset, const std::string& messageId)
{
    if (_moduleBase == 0 || messageId.empty()) return std::string();

    // These are detoured, so a bad ID can surface as a fault rather than a clean
    // failure.
    char* result = SafeResolve(_moduleBase + offset, messageId.c_str());

    if (!result) return std::string();
    return std::string(result);
}

std::string GameText::Resolve(const std::string& messageId)
{
    return ResolveAt(OffsetMessageToString, messageId);
}

std::string GameText::ResolveAlt(const std::string& messageId)
{
    return ResolveAt(OffsetMessageToString3, messageId);
}
