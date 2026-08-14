#include "Announcer.h"
#include "Speech.h"
#include "Input.h"
#include "Log.h"

#include <windows.h>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace
{
    std::wstring          g_namesPath;
    std::map<std::string, std::string> g_names;

    // Button/key icon name -> spoken word, from "@icon:<name> = <word>" in names.txt.
    std::map<std::string, std::string> g_icons;

    // Rules whose ID ends in '*' match by prefix. Kept in file order so the first
    // matching rule wins, which lets a specific entry override a broad wildcard.
    std::vector<std::pair<std::string, std::string> > g_wildcards;

    // Rule values with special meaning. Anything else is a spoken label.
    const char* kSuppress = "-";   // recognised, deliberately never spoken
    const char* kPriority = "^";   // speak its own text, but first in the burst
    const char* kPlain    = "+";   // speak its own text normally (carves an
                                   // exception out of a broader wildcard)

    std::string           g_lastSpoken;
    DWORD                 g_lastSpokenTime = 0;
    bool                  g_enabled = true;

    // On by default: a sighted player sees the item label AND its description, so we
    // speak both. "verbose off" drops back to the label alone.
    bool                  g_verbose = true;

    // Suppresses the repeat that arrives when the game re-resolves the same content
    // without the selection having moved.
    const DWORD           kRepeatWindowMs = 400;

    std::string Trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    // Exact match first, then wildcards in file order.
    const std::string* FindRule(const std::string& id)
    {
        std::map<std::string, std::string>::const_iterator it = g_names.find(id);
        if (it != g_names.end()) return &it->second;

        for (size_t i = 0; i < g_wildcards.size(); i++)
        {
            const std::string& prefix = g_wildcards[i].first;
            if (id.size() >= prefix.size() && id.compare(0, prefix.size(), prefix) == 0)
                return &g_wildcards[i].second;
        }

        return nullptr;
    }

    // Strings that reduce to punctuation - "<string />%" becomes "%" - carry nothing
    // a listener can use, so they are dropped rather than spoken.
    bool HasSpeakableContent(const std::string& s)
    {
        for (size_t i = 0; i < s.size(); i++)
        {
            unsigned char c = (unsigned char)s[i];
            if (isalnum(c) || c >= 0x80) return true;
        }
        return false;
    }

    std::string CollapseSpaces(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());

        bool pendingSpace = false;
        for (size_t i = 0; i < s.size(); i++)
        {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                pendingSpace = !out.empty();
                continue;
            }
            if (pendingSpace) { out += ' '; pendingSpace = false; }
            out += c;
        }
        return out;
    }
}

void Announcer::Initialize(const std::wstring& pluginDir)
{
    g_namesPath = pluginDir + L"\\names.txt";
    LoadNames();
}

size_t Announcer::LoadNames()
{
    g_names.clear();
    g_wildcards.clear();
    g_icons.clear();

    std::ifstream file(g_namesPath.c_str());
    if (!file.is_open())
    {
        Log::Write("Kagura :: names.txt not found (announcing raw descriptions)");
        return 0;
    }

    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string id = Trim(line.substr(0, eq));
        std::string name = Trim(line.substr(eq + 1));
        if (id.empty() || name.empty()) continue;

        // "@icon:btn_attack = Attack" defines an icon translation, not a message name.
        if (id.compare(0, 6, "@icon:") == 0)
        {
            g_icons[id.substr(6)] = name;
            continue;
        }

        if (id[id.size() - 1] == '*')
            g_wildcards.push_back(std::make_pair(id.substr(0, id.size() - 1), name));
        else
            g_names[id] = name;
    }

    std::ostringstream msg;
    msg << "Kagura :: loaded " << g_names.size() << " name mappings, "
        << g_wildcards.size() << " wildcard rules";
    Log::Write(msg.str());

    return g_names.size() + g_wildcards.size();
}

size_t Announcer::NameCount() { return g_names.size() + g_wildcards.size(); }

// Any rule counts as handled - including a suppression rule - so that deliberately
// silenced IDs stop showing up in the log as gaps still needing a name.
bool Announcer::HasName(const std::string& messageId)
{
    return FindRule(messageId) != nullptr;
}

void Announcer::SetEnabled(bool on)
{
    g_enabled = on;

    // Muting should take effect now, not after the current sentence finishes.
    if (!on) Speech::Silence();
}

bool Announcer::IsEnabled() { return g_enabled; }

void Announcer::RepeatLast()
{
    if (g_lastSpoken.empty())
    {
        Speech::Say(std::string("Nothing to repeat."), true);
        return;
    }

    Speech::Say(g_lastSpoken, true);
}

void Announcer::SetVerbose(bool on) { g_verbose = on; }
bool Announcer::IsVerbose() { return g_verbose; }

std::string Announcer::StripMarkup(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];

        if (c == '<')
        {
            size_t close = text.find('>', i);
            if (close == std::string::npos) break;   // malformed; drop the remainder

            std::string tag = Trim(text.substr(i + 1, close - i - 1));
            i = close;

            // <icon btn_attack /> names a button. Deleting it the way <color red>
            // is deleted would leave "Strike Attack:" with no button, which is
            // exactly the information the line exists to convey, so icons are
            // translated to words instead.
            if (tag.compare(0, 5, "icon ") == 0)
            {
                std::istringstream parts(tag.substr(5));
                std::string iconName;
                parts >> iconName;

                std::map<std::string, std::string>::const_iterator it = g_icons.find(iconName);
                if (it != g_icons.end())
                {
                    if (!out.empty() && out[out.size() - 1] != ' ') out += ' ';
                    out += it->second;
                }
                else if (!iconName.empty())
                {
                    // Unknown icon: say the raw name rather than silently dropping
                    // it, so gaps in the icon table are audible and fixable.
                    if (!out.empty() && out[out.size() - 1] != ' ') out += ' ';
                    out += iconName;
                }
            }

            continue;
        }

        // Placeholder brackets like [<string />] leave stray square brackets behind.
        if (c == '[' || c == ']') continue;

        out += c;
    }

    return Trim(CollapseSpaces(out));
}

void Announcer::Process(const std::vector<MessageHook::Entry>& entries)
{
    if (!g_enabled) return;

    // Everything resolved in the same tick is one burst. Priority entries go first -
    // on character select the character's own name arrives in the middle of the
    // burst and is the part the player actually needs. Reordering within a batch
    // costs nothing, since these are all already in hand.
    std::vector<std::string> lines;

    for (int pass = 0; pass < 2; pass++)
    {
        bool wantPriority = (pass == 0);

        for (size_t i = 0; i < entries.size(); i++)
        {
            const MessageHook::Entry& e = entries[i];

            const std::string* rule = FindRule(e.id);
            if (rule && *rule == kSuppress) continue;

            bool priority = (rule && *rule == kPriority);
            if (priority != wantPriority) continue;

            std::string description = StripMarkup(e.text);
            if (!HasSpeakableContent(description)) continue;

            bool rawText = (priority || (rule && *rule == kPlain));

            std::string spoken;
            if (rule && !rawText)
            {
                spoken = *rule;
                // Skip the description when it merely repeats the label.
                if (g_verbose && description != *rule) spoken += ". " + description;
            }
            else
            {
                spoken = description;
            }

            // A burst repeats entries; say each thing only once.
            bool duplicate = false;
            for (size_t j = 0; j < lines.size(); j++)
            {
                if (lines[j] == spoken) { duplicate = true; break; }
            }
            if (!duplicate) lines.push_back(spoken);
        }
    }

    for (size_t i = 0; i < lines.size(); i++)
    {
        const std::string& spoken = lines[i];

        DWORD now = GetTickCount();
        if (spoken == g_lastSpoken && (now - g_lastSpokenTime) < kRepeatWindowMs) continue;

        // The interrupt decision. A message that follows player input is a new
        // selection and cuts off whatever is being read; a message that arrived on
        // its own is the rest of the same burst and queues behind it. That way fast
        // scrolling stays responsive instead of building a backlog, while a single
        // selection gets read out in full.
        bool interrupt = Input::InputSince(g_lastSpokenTime);

        Speech::Say(spoken, interrupt);

        g_lastSpoken = spoken;
        g_lastSpokenTime = now;
    }
}
