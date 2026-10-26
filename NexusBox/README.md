# NexusBox

A high-performance, fully native C++ emulator frontend and multi-core runtime for
**Xbox Series X|S** running in **Developer Mode (UWP / WinRT)**.

## Highlights

| Subsystem | Implementation |
|---|---|
| Language | C++20, `/await`, WinRT (`C++/WinRT`), no managed code |
| Rendering | D3D12 flip-discard swapchain, 4K/120Hz buffers, VRR + Low Latency Mode (DXGI_OPTIONS), hardware scaling (Bilinear / Integer / Nearest), shader pipeline for HLSL/Slang (CRT-Royale, scanlines) |
| Audio | XAudio2, lock-free SPSC ring buffer, low-latency stream flags |
| Input | `Windows.Gaming.Input` polling thread, per-core controller remapping, radial deadzones, impulse triggers, hotkeys (Guide+View, L3+R3 overlay) |
| Cores | Libretro-compatible loader (`LoadLibraryExW` static/dynamic, sandboxed memory arenas) |
| States | LZ77-compressed (pure C++) save states, async auto-save + SRAM backup, rewind ring buffer |
| UI | Console-grade 10-foot dashboard (grid cover flow, gamelist.xml/JSON scrapers) + in-game ImGui overlay |
| Profiler | Frame-time OSD, memory-budget guard against Dev Mode sandbox termination |
| Netplay | Local peer-to-peer rollback foundation on non-blocking sockets |

## Layout

```
NexusBox/
  CMakeLists.txt                  # UWP-aware CMake (VS2022 generator)
  NexusBox/Package.appxmanifest   # capabilities: broadFileSystemAccess, extendedExecutionUnconstrained, removableStorage
  src/
    App.h / App.cpp               # WinRT lifecycle, susp/resume, swapchain bootstrap
    CoreEngine.h / CoreEngine.cpp # deterministic 60/120Hz emulation dispatch loop
    InputManager.h / .cpp         # controller polling thread, remap, deadzones, hotkeys
    render/Renderer.*             # D3D12 pipeline, scaling, post-processing
    render/Shaders/*.hlsl         # scanline / integer-scale example shaders
    audio/AudioEngine.*           # XAudio2 ring-buffer voice
    core/RetroCoreLoader.*        # libretro symbol binding + memory arenas
    save/SaveStateManager.*       # LZ77 states (src/util/LzCpp.cpp), async autosave, rewind
    storage/StorageManager.*      # IsolatedStorage + removable USB ROM mounts
    net/Netplay.*                 # rollback netplay foundation
    util/Profiler.h               # frametime/mem/latency counters for the OSD
    util/MemoryBudget.h           # Dev Mode RAM budget guard
  ui/ (dashboard + overlay)       # see src/ui/README for the planned modules
```

## Building

1. Visual Studio 2022 (17.9+) with **Universal Windows Platform** workload and
   the Xbox One / Series X|S platform (Gaming SDK).
2. `cmake --preset win-x64` (VS 2026; VS 2022 fallback: `--preset win-x64-vs2022`)
   then `cmake --build --preset win-x64 --config Release`. ARM64: preset `win-arm64`.
   If configure fails on `rc`/`CMAKE_MT-NOTFOUND`, see docs/TROUBLESHOOTING.md.
3. Deploy in **Dev Mode**: right-click `NexusBox` package -> Deploy. Xbox Dev Mode
   Partner Center provisioning is required for `broadFileSystemAccess`.

## Legal note

NexusBox ships **no cores, no BIOS, no copyrighted content**. Users supply their
own libretro cores and legally-obtained ROMs. See `docs/LEGAL.md`.
