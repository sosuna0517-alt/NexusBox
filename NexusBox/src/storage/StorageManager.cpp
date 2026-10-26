// StorageManager.cpp — UWP isolated storage + removable USB ROM drives.
// Note: the desktop-only API surface (CreateDirectoryW, PathFileExistsW,
// FindFirstFileExW) is not present in the UWP compile partition. All file
// enumeration/creation goes through std::filesystem (CRT-backed, UWP-safe)
// and winrt::Windows::Storage for the app-local root.
#include "StorageManager.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;



namespace nb
{

static void EnsureDir(std::wstring const& path)
{
    std::error_code ec;
    fs::create_directories(fs::path(path), ec);   // no-op when it exists
}

void StorageManager::Initialize()
{
    m_localDir = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    EnsureDir(m_localDir + L"\\system");
    EnsureDir(m_localDir + L"\\saves");
    EnsureDir(m_localDir + L"\\roms");
    EnsureDir(m_localDir + L"\\cores");
    StartDeviceWatcher();
}

std::wstring StorageManager::SystemDir() const { return m_localDir + L"\\system"; }
std::wstring StorageManager::SavesDir()  const { return m_localDir + L"\\saves"; }
std::wstring StorageManager::ConfigPath()const { return m_localDir + L"\\nexusbox.json"; }
std::wstring StorageManager::CoresDir()  const
{
    // Prefer an external core dir on D: (Xbox internal storage is tiny);
    // fall back to the packaged install.
    if (fs::exists(L"D:\\nexusbox\\cores")) return L"D:\\nexusbox\\cores";
    return m_localDir + L"\\cores";
}

void StorageManager::StartDeviceWatcher()
{
    m_watcher = winrt::Windows::Devices::Enumeration::DeviceInformation::CreateWatcher(winrt::Windows::Devices::Enumeration::DeviceClass::RemovableDevices());

    namespace dev = winrt::Windows::Devices::Enumeration;
    m_watcher.Added([this](dev::DeviceWatcher const&, dev::DeviceInformation const& info)
    {
        if (m_onMounted) m_onMounted(info.Id().c_str());
    });
    m_watcher.Removed([this](dev::DeviceWatcher const&, dev::DeviceInformationUpdate const& upd)
    {
        if (m_onRemoved) m_onRemoved(upd.Id().c_str());
    });
    m_watcher.Start();
}

std::vector<StorageManager::GameEntry> StorageManager::ScanLibrary()
{
    std::vector<GameEntry> out;
    // Walk every mounted removable root + the local folder; parse gamelist.xml
    // or gamelist.json when present (EmulationStation-compatible format).
    std::vector<std::wstring> roots;
    if (fs::exists(L"D:\\roms")) roots.push_back(L"D:\\roms");
    // Isolated-storage ROMs (copied via the dashboard UI) also count.
    roots.push_back(m_localDir + L"\\roms");

    static const wchar_t* exts[] = { L".nes", L".sfc", L".smc", L".gba",
        L".md", L".z64", L".n64", L".iso", L".cue", L".chd", L".7z", L".zip" };

    std::error_code ec;
    for (auto const& root : roots)
    {
        for (auto const& it : fs::directory_iterator(fs::path(root), ec))
        {
            if (!it.is_regular_file(ec)) continue;
            std::wstring name = it.path().filename().c_str();
            for (auto* e : exts)
            {
                if (name.size() > wcslen(e) &&
                    _wcsicmp(name.c_str() + name.size() - wcslen(e), e) == 0)
                {
                    GameEntry g;
                    g.path = root + L"\\" + name;
                    g.name = name.substr(0, name.size() - wcslen(e));
                    out.push_back(std::move(g));
                    break;
                }
            }
        }
    }
    return out;
}

} // namespace nb
