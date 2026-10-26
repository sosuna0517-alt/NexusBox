#pragma once
// StorageManager.h — UWP isolated storage + removable USB ROM drives.
// - IsolatedStorage: ApplicationData::Current().LocalFolder() for saves/config.
// - RemovableDevices: KnownFolders::RemovableDevices() enumeration with
//   auto-mount (DeviceWatcher) for NTFS/exFAT USB drives plugged into the Xbox.
// - broadFileSystemAccess lets us read arbitrary paths on D: (external HDD).
#include <string>
#include <vector>
#include <functional>

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Devices.Enumeration.h>

namespace nb {

class StorageManager
{
public:
    void Initialize();

    // Canonical locations
    std::wstring SystemDir() const;      //  BIOS/firmware: <local>\system
    std::wstring SavesDir()  const;      //  SRAM/states:  <local>\saves
    std::wstring ConfigPath()const;      //  nexusbox.json
    std::wstring CoresDir()  const;      //  D:\nexusbox\cores (external preferred)

    // Removable device watch: fires when a USB ROM drive is inserted/ejected.
    void SetOnDriveMounted(std::function<void(std::wstring const& path)> cb);
    void SetOnDriveRemoved(std::function<void(std::wstring const& path)> cb);

    // Build the ROM library index from all mounted sources (gamelist.xml/JSON).
    struct GameEntry
    {
        std::wstring path;          // full path to the ROM
        std::wstring name;          // display title
        std::wstring coreHint;      // libretro core id
        std::wstring boxartPath;    // local box art cache, if scraped
    };
    std::vector<GameEntry> ScanLibrary();

private:
    void StartDeviceWatcher();
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::StorageFolder>
        ResolveRemovableRootAsync();

    std::wstring m_localDir;
    winrt::Windows::Devices::Enumeration::DeviceWatcher m_watcher{ nullptr };
    std::function<void(std::wstring const&)> m_onMounted, m_onRemoved;
};

} // namespace nb
