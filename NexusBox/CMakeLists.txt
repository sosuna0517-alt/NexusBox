cmake_minimum_required(VERSION 3.20)
project(NexusBox LANGUAGES CXX VERSION 0.1.0)

# Windows.h min/max macros collide with <algorithm> in the audio ring; strip them.
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- UWP / Xbox specifics -------------------------------------------------
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "NexusBox targets UWP (WindowsStore). Use -DCMAKE_SYSTEM_NAME=WindowsStore")
endif()

set(PLATFORM "x64" CACHE STRING "x64 (Series X/S) or ARM64")
if(PLATFORM STREQUAL "ARM64")
    add_compile_definitions(_M_ARM64)
endif()

add_compile_definitions(
    _WIN32_WINNT=0x0A00
    UNICODE _UNICODE
    NEXUSBOX_VERSION_MAJOR=0 NEXUSBOX_VERSION_MINOR=1
    XB1 ON   # Xbox family target for the Gaming SDK
    _USE_MATH_DEFINES
)
# Max optimization for retail Xbox Dev Mode, LTCG, no SEH overhead on hot paths
if(MSVC)
    add_compile_options(/O2 /GL /GR- /permissive- /Zc:__cplusplus /EHsc /fp:fast)
    # /arch:AVX2 is x64-only; ARM64 builds must not pass it (D9002 noise).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
        add_compile_options(/arch:AVX2)
    endif()
    add_link_options(/LTCG /OPT:REF /OPT:ICF)
endif()

# Pure C++: no external packages. JSON parsing and LZ77 compression are
# in-repo (src/ui/Scraper.cpp uses a hand-rolled reader; src/util/LzCpp.cpp
# provides state compression). The dashboard UI is built in C++ directly.

add_executable(NexusBox WIN32
    src/App.cpp
    src/CoreEngine.cpp
    src/InputManager.cpp
    src/render/Renderer.cpp
    src/audio/AudioEngine.cpp
    src/core/RetroCoreLoader.cpp
    src/save/SaveStateManager.cpp
    src/storage/StorageManager.cpp
    src/net/Netplay.cpp
    # NexusBox/Package.appxmanifest is added by the VS project / VS generator
)

target_include_directories(NexusBox PRIVATE src)

target_link_libraries(NexusBox PRIVATE
    d3d12.lib dxgi.lib dxguid.lib
    xaudio2.lib
    windowsapp.lib  # C++/WinRT
    ws2_32.lib      # netplay sockets
    kernel32.lib user32.lib
)

# ---- Dashboard UI ----------------------------------------------------------
# ImGui is pure C++ source; fetched as source (no package manager) and compiled
# into the app. NEXUSBOX_IMGUI=OFF builds without the dashboard overlay.
option(NEXUSBOX_IMGUI "Build the ImGui dashboard UI" ON)
if(NEXUSBOX_IMGUI)
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9b
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)
    # Core ImGui only. The desktop backends (imgui_impl_win32 / imgui_impl_dx12)
    # use APIs missing from the UWP surface (GetKeyboardLayout, TrackMouseEvent,
    # DwmEnableBlurBehindWindow, ...) and are wrong for Xbox. Platform glue and
    # DX12 rendering live in our own src/ui/ImguiBackend.*.
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(NexusBox PRIVATE imgui)
    target_compile_definitions(NexusBox PRIVATE NEXUSBOX_HAS_IMGUI)
endif()
