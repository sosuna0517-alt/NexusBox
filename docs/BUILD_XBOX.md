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

## Quick start (dashboard build)

1. Put UWP x64 libretro cores in `cores/` (see `cores/README.md`), e.g.
   `snes9x_libretro.dll`, `fceumm_libretro.dll`, `mgba_libretro.dll`,
   `gambatte_libretro.dll`, `genesis_plus_gx_libretro.dll`.
2. `cmake --preset win-x64` then open `build/win-x64/NexusBox.sln`, choose
   **x64 / Remote Machine**, enter the console IP, and deploy.
   (Re-run CMake whenever you add or remove a core so it is packaged.)
3. Copy ROMs to the app's `LocalState\roms` folder with Xbox Device Portal
   (File explorer > LocalAppData > NexusBox_... > LocalState > roms), or to
   `E:\roms` on a USB drive. BIOS files go in `LocalState\system`.
4. Launch NexusBox: pick a game with the D-pad and press A.
   In game, press **View + Menu** (or L3 + R3) for the in-game menu
   (save/load state, fast forward, reset, quit). Menu on the library opens Settings.

Controller mapping is positional (SNES layout): Xbox A = libretro B,
B = A, X = Y, Y = X, View = Select, Menu = Start, LB/RB = L/R, LT/RT = L2/R2.
