#ifndef KAGURA_GAMETEXT_H
#define KAGURA_GAMETEXT_H
#pragma once

#include <string>

// Access to the game's message table: message ID -> localized display string.
// Confirmed working on NS4 1.09 via the framework's ConvertMessage command.
class GameText
{
public:
    // Offsets of the message resolvers from the module base. 1.09 hardcoded values,
    // matching HookFunctions::fc_msgtostring / fc_msgtostring_3 in the Storm Framework.
    // The game uses both: different screens route their text through different ones.
    static const __int64 OffsetMessageToString = 0xAB8720;
    static const __int64 OffsetMessageToString3 = 0xAB87D0;

    static void Initialize(__int64 moduleBase);

    // Returns the resolved string, or an empty string if resolution failed.
    static std::string Resolve(const std::string& messageId);

private:
    static __int64 _moduleBase;
};

#endif
