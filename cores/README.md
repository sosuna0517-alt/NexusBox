# cores

Put libretro core DLLs **built for UWP x64** here (for example
`snes9x_libretro.dll`, `fceumm_libretro.dll`, `mgba_libretro.dll`), then
re-run CMake and redeploy. They are packaged into the app's `cores` folder,
which is the only place Xbox lets NexusBox load DLLs from.

Regular Windows desktop cores will not load on Xbox. UWP builds are published
on the libretro buildbot (the UWP / `windows-msvc*-uwp` x64 nightly folders),
or can be taken from the `cores` folder of a RetroArch UWP package.

Only software-rendered cores are supported (no OpenGL/Vulkan hardware render).
