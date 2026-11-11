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
    NEXUSBOX_XBOX=1
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

file(GLOB NEXUSBOX_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/NexusBox/Assets/*.png)
# libretro cores (UWP x64 builds) placed in ./cores are packaged into the app.
file(GLOB NEXUSBOX_CORES ${CMAKE_CURRENT_SOURCE_DIR}/cores/*.dll)
set(NEXUSBOX_PACKAGE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/NexusBox/Package.appxmanifest
    ${NEXUSBOX_ASSETS} ${NEXUSBOX_CORES})

# HLSL compiled at build time by FXC into headers (no runtime shader compiler).
set(NEXUSBOX_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(NEXUSBOX_SHADERS
    src/render/Shaders/BlitVS.hlsl  src/render/Shaders/BlitPS.hlsl
    src/render/Shaders/ImGuiVS.hlsl src/render/Shaders/ImGuiPS.hlsl)
foreach(sh ${NEXUSBOX_SHADERS})
    get_filename_component(name ${sh} NAME_WE)
    if(name MATCHES "VS$")
        set(type Vertex)
    else()
        set(type Pixel)
    endif()
    set_source_files_properties(${sh} PROPERTIES
        VS_SHADER_TYPE ${type}
        VS_SHADER_MODEL 5.0
        VS_SHADER_ENTRYPOINT main
        VS_SHADER_OUTPUT_HEADER_FILE "${NEXUSBOX_SHADER_DIR}/${name}.h"
        VS_SHADER_VARIABLE_NAME "g_${name}"
        VS_SHADER_OBJECT_FILE_NAME "$(OutDir)%(Filename).cso")
endforeach()

add_executable(NexusBox WIN32
    src/App.cpp
    src/CoreEngine.cpp
    src/InputManager.cpp
    src/render/Renderer.cpp
    src/audio/AudioEngine.cpp
    src/core/RetroCoreLoader.cpp
    src/ui/Dashboard.cpp
    src/util/LzCpp.cpp
    src/util/Profiler.cpp
    ${NEXUSBOX_SHADERS}
    ${NEXUSBOX_PACKAGE_FILES}
)
# Not built yet (unfinished): src/net, src/netplay, src/save, src/storage, src/ui/Scraper.
# Mark assets/cores as package content so the VS generator places them in the .appx
set_property(SOURCE ${NEXUSBOX_ASSETS} PROPERTY VS_DEPLOYMENT_CONTENT 1)
set_property(SOURCE ${NEXUSBOX_ASSETS} PROPERTY VS_DEPLOYMENT_LOCATION "Assets")
if(NEXUSBOX_CORES)
    set_property(SOURCE ${NEXUSBOX_CORES} PROPERTY VS_DEPLOYMENT_CONTENT 1)
    set_property(SOURCE ${NEXUSBOX_CORES} PROPERTY VS_DEPLOYMENT_LOCATION "cores")
endif()
set_target_properties(NexusBox PROPERTIES VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0")

target_include_directories(NexusBox PRIVATE src src/render ${NEXUSBOX_SHADER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro)

target_link_libraries(NexusBox PRIVATE
    d3d12.lib dxgi.lib dxguid.lib
    xaudio2.lib
    windowsapp.lib  # C++/WinRT
    ws2_32.lib      # netplay sockets
    kernel32.lib user32.lib
)

# ---- Dashboard UI (Dear ImGui, required) ------------------------------------
# Only the core ImGui sources are compiled. Platform input and D3D12 rendering
# are implemented by NexusBox itself (src/ui/Dashboard.cpp, src/render/Renderer.cpp)
# because the desktop imgui_impl_win32 backend does not work on Xbox UWP.
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(NexusBox PRIVATE imgui)
target_compile_definitions(NexusBox PRIVATE NEXUSBOX_HAS_IMGUI)
