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

std::string GameText::Resolve(const std::string& messageId)
{
    if (_moduleBase == 0 || messageId.empty()) return std::string();

    // The framework detours this function, so a bad ID can surface as a fault
    // rather than a clean failure.
    char* result = SafeResolve(_moduleBase + OffsetMessageToString, messageId.c_str());

    if (!result) return std::string();
    return std::string(result);
}
