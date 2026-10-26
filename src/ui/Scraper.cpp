// Scraper.cpp — gamelist readers and system mapping.
#include "Scraper.h"

#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

namespace nb
{

// ---------------------------------------------------------------- systems

struct SystemDef { const wchar_t* ext; const wchar_t* name; const char* coreId; };
static const SystemDef kSystems[] = {
    { L".nes", L"NES",      "nestopia"  },
    { L".fds", L"NES",      "nestopia"  },
    { L".sfc", L"SNES",     "snes9x"    },
    { L".smc", L"SNES",     "snes9x"    },
    { L".gb",  L"Game Boy", "gambatte"  },
    { L".gba", L"GBA",      "mgba"      },
    { L".md",  L"Mega Drive","genesis_plus_gx" },
    { L".gen", L"Mega Drive","genesis_plus_gx" },
    { L".z64", L"N64",      "mupen64plus_next" },
    { L".n64", L"N64",      "mupen64plus_next" },
    { L".v64", L"N64",      "mupen64plus_next" },
    { L".iso", L"PSX",      "beetle_psx_hw" },
    { L".cue", L"PSX",      "beetle_psx_hw" },
    { L".chd", L"PSX",      "beetle_psx_hw" },
    { L".pce", L"PC Engine","beetle_pce" },
    { L".7z",  L"",         nullptr     },   // archives: sniff by content
    { L".zip", L"",         nullptr     },
};

std::wstring Scraper::DetectSystem(const std::wstring& path) const
{
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (auto& s : kSystems)
        if (_wcsicmp(ext, s.ext) == 0)
        {
            if (s.name[0]) return s.name;
            // Archive: use the containing folder as a system hint ("roms/SNES/...").
            wchar_t dir[MAX_PATH]; wcsncpy_s(dir, path.c_str(), MAX_PATH - 1);
            PathRemoveFileSpecW(dir);
            const wchar_t* base = PathFindFileNameW(dir);
            if (base) return base;
        }
    return L"Unknown";
}

std::wstring Scraper::PickCore(const std::wstring& system) const
{
    for (auto& s : kSystems)
        if (system == s.name && s.coreId) return s.coreId;
    return L"";
}

// ------------------------------------------------------------ gamelist.xml

static std::wstring XmlTag(const std::string& xml, const std::string& tag)
{
    std::regex re("<" + tag + ">(.*?)</" + tag + ">");
    std::smatch m;
    if (std::regex_search(xml, m, re)) {
        // Strip a CDATA wrapper if present.
        std::string s = m[1].str();
        auto p = s.find("<![CDATA[");
        if (p != std::string::npos) {
            auto e = s.find("]]>", p);
            if (e != std::string::npos) s = s.substr(p + 9, e - p - 9);
        }
        return std::wstring(s.begin(), s.end());
    }
    return {};
}

std::unordered_map<std::wstring, Scraper::Meta>
Scraper::LoadGamelist(const std::wstring& root) const
{
    std::unordered_map<std::wstring, Meta> out;

    // --- XML (EmulationStation format) ---
    {
        std::ifstream f(root + L"\\gamelist.xml", std::ios::binary);
        if (f)
        {
            std::stringstream ss; ss << f.rdbuf();
            const std::string xml = ss.str();
            // Per-game <game> blocks: <path>, <name>, <image>, <desc>.
            std::regex block("<game>(.*?)</game>");
            for (std::sregex_iterator it(xml.begin(), xml.end(), block), end;
                 it != end; ++it)
            {
                std::string b = it->str();
                Meta m;
                m.title = XmlTag(b, "name");
                m.image = XmlTag(b, "image");
                m.desc  = XmlTag(b, "desc");
                std::wstring p = XmlTag(b, "path");
                if (!p.empty()) out[p] = std::move(m);
            }
            return out;
        }
    }

    // --- JSON alternative (flat map by filename) ---
    {
        std::ifstream f(root + L"\\gamelist.json", std::ios::binary);
        if (f)
        {
            // Minimal hand-rolled scan (pure C++ hand-rolled; no external dependency):
            // expects a flat object: { "game.nes": {"title":"...", "image":"..."} }
            std::wstring jw(j.begin(), j.end());
            size_t pos = 0;
            while (true)
            {
                const size_t key = jw.find(L'"', pos);
                if (key == std::wstring::npos) break;
                const size_t keyEnd = jw.find(L'"', key + 1);
                if (keyEnd == std::wstring::npos) break;
                const size_t obj = jw.find(L'{', keyEnd);
                const size_t objEnd = jw.find(L'}', obj);
                if (obj == std::wstring::npos || objEnd == std::wstring::npos) break;

                auto grab = [&](const std::wstring& field) -> std::wstring
                {
                    const std::wstring pat = L'"' + field + L'"';
                    size_t f = jw.find(pat, obj);
                    if (f == std::wstring::npos || f >= objEnd) return {};
                    size_t colon = jw.find(L':', f + pat.size());
                    if (colon == std::wstring::npos || colon >= objEnd) return {};
                    size_t q1 = jw.find(L'"', colon + 1);
                    size_t q2 = jw.find(L'"', q1 + 1);
                    return (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 < objEnd)
                           ? jw.substr(q1 + 1, q2 - q1 - 1) : std::wstring();
                };

                Meta m;
                m.title = grab(L"title");
                m.image = grab(L"image");
                out[jw.substr(key + 1, keyEnd - key - 1)] = std::move(m);
                pos = objEnd + 1;
            }
        }
    }
    return out;
}

// ------------------------------------------------------------ async scrape

void Scraper::ScrapeLibraryAsync(std::vector<LibraryEntry>& library)
{
    m_job = std::async(std::launch::async, [&library, this]
    {
        // Group by parent folder to read each gamelist once.
        std::unordered_map<std::wstring, size_t> byFolder;
        for (size_t i = 0; i < library.size(); ++i)
        {
            wchar_t dir[MAX_PATH]; wcsncpy_s(dir, library[i].game.path.c_str(), MAX_PATH - 1);
            PathRemoveFileSpecW(dir);
            byFolder[dir] = i;
            // NOTE: real impl groups by dir first; scaffold touches each entry.
        }
        for (auto& [dir, idx] : byFolder)
        {
            if (m_stop) return;
            auto meta = LoadGamelist(dir);
            auto it = meta.find(PathFindFileNameW(library[idx].game.path.c_str()));
            if (it == meta.end()) continue;
            if (!it->second.title.empty()) library[idx].displayName = it->second.title;
            if (!it->second.image.empty())
                library[idx].boxartUv = "media:" + std::string(
                    it->second.image.begin(), it->second.image.end());
        }
    });
}

void Scraper::Shutdown()
{
    m_stop = true;
    if (m_job.valid()) m_job.wait();
}

} // namespace nb
