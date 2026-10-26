#pragma once
// Scraper.h — gamelist.xml / gamelist.json metadata reader + box art cache.
// Reads EmulationStation-compatible gamelist.xml next to each ROM folder, and a
// simple gamelist.json alternative. Media is cached at <saves>\media\<name>.png.
#include <string>
#include <vector>
#include <unordered_map>
#include <future>

#include "storage/StorageManager.h"
#include "ui/Dashboard.h"

namespace nb {

class Scraper
{
public:
    // System detection by extension + heuristics from the folder name.
    std::wstring DetectSystem(const std::wstring& path) const;
    // libretro core id for a detected system.
    std::wstring PickCore(const std::wstring& system) const;

    // Parse <root>/gamelist.xml or gamelist.json into name -> {title, image, desc}.
    struct Meta { std::wstring title, image, desc; };
    std::unordered_map<std::wstring, Meta> LoadGamelist(const std::wstring& root) const;

    // Kick off async scraping: fills displayName / boxartUv on the entries.
    void ScrapeLibraryAsync(std::vector<LibraryEntry>& library);
    void Shutdown();

private:
    struct LibraryEntry;
    std::future<void> m_job;
    std::atomic<bool> m_stop{ false };
};

} // namespace nb
