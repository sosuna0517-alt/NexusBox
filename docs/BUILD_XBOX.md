# Building & deploying to Xbox Dev Mode

> **First-time configure failing with "RC Pass 1: rc ... no such file or
> directory"?** See docs/TROUBLESHOOTING.md — use `cmake --preset win-x64`
> (Visual Studio generator), which does not need the Windows SDK on PATH.

1. Activate **Dev Mode** on the Xbox (retail console, free dev account).
2. Visual Studio 2022/2026 -> UWP workload + **Gaming SDK**; add the Xbox platform.
3. Pair the console: Device Portal or `WebB` remote debugging.
4. Set the appx `Identity.Publisher` to your Partner Center ID (or use the
   dev-mode sideload flow for local testing).
5. Deploy: right-click `NexusBox (Universal Windows)` -> **Deploy**. First run
   requires granting `broadFileSystemAccess` in Settings.
6. Cores: place compiled libretro cores (`.dll`/`.lib`) under
   `D:\\nexusbox\\cores\\<core>.dll`; ROMs on a USB drive at `D:\\roms\\<system>\\`.

## Configure commands

```bat
:: VS 2026 (CMake >= 4.2)
cmake --preset win-x64
cmake --build --preset win-x64 --config Release

:: VS 2022 fallback
cmake --preset win-x64-vs2022
```

## Capabilities in `Package.appxmanifest`
- `broadFileSystemAccess` — external ROM/core drives (D:).
- `extendedExecutionUnconstrained` — keep autosave/SRAM flush running.
- `removableStorage` + `DeviceCapability` mass-storage — USB auto-mount.
