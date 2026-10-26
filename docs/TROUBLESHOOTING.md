# Troubleshooting

## "The C++ compiler is not able to compile a simple test program" (`RC Pass 1: rc /fo ... no such file or directory`)

**Symptom** (seen with CMake 4.3 + VS 2026 Community, `x64-Debug` preset):

```
CMake Error at .../CMakeTestCXXCompiler.cmake:73 (message):
  The C++ compiler "cl.exe" is not able to compile a simple test program.
  ...
  RC Pass 1: command "rc /fo CMakeFiles\cmTC_xxx.dir/manifest.res ..." failed
  no such file or directory
  ninja: build stopped: subcommand failed.
```

**Root cause:** the Ninja generator relies on the shell environment having the
Windows SDK tools on PATH. If the Windows SDK is not installed, or the shell is
not a Visual Studio developer environment, `rc.exe` (Resource Compiler) and
`mt.exe` (Manifest Tool, `--mt=CMAKE_MT-NOTFOUND` in the link line) are not
found, and every CMake compiler test fails at the manifest step.

**Fix (pick one):**

1. **Use the Visual Studio generator (recommended).** Run
   `cmake --preset win-x64` — the repo's `CMakePresets.json` selects
   `Visual Studio 18 2026`, which locates the SDK itself and does not depend on
   PATH. In Visual Studio: *Tools → Options → CMake → switch the configuration
   provider to "Visual Studio" presets*, or just pick the `win-x64` preset.

2. **Install the missing VS components.** Visual Studio Installer → Modify →
   under "Desktop development with C++", check:
   - **Windows 11 SDK (latest)**
   - **Universal Windows Platform tools** (required for UWP/Xbox builds)
   - **C++ Universal Windows Platform build tools**

3. **If you must use Ninja**, launch the shell from
   *Start → Visual Studio 2026 → Developer PowerShell* (vcvars64 + vcvarsuwp
   are run for you), not a plain `cmd.exe`/`pwsh`:

   ```powershell
   # Developer PowerShell for VS 2026
   cmake -B out/build/x64-Debug -S . -G Ninja `
     -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 `
     -DCMAKE_CXX_COMPILER=cl
   ```

## "Could not find any instance of Visual Studio"

- VS 2026 needs **CMake ≥ 4.2** for the `Visual Studio 18 2026` generator.
  Older CMake only knows `Visual Studio 17 2022` — use the `win-x64-vs2022`
  preset instead, or upgrade CMake.

## Linker errors about missing `windowsapp.lib` / WinRT headers

- UWP toolset not selected. Confirm `CMAKE_SYSTEM_NAME=WindowsStore` was set at
  configure time (the presets do this; the top-level `CMakeLists.txt` hard-guards
  it too).

## Xbox Dev Mode deployment fails / capability rejected

- `broadFileSystemAccess` and `extendedExecutionUnconstrained` are restricted
  capabilities; for sideload testing you may need to strip them from
  `NexusBox/Package.appxmanifest` (see docs/BUILD_XBOX.md).
