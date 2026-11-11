// Dashboard.cpp — library / settings / in-game menu.
#include <windows.h>
#include "Dashboard.h"
#include "render/Renderer.h"
#include "CoreEngine.h"
#include "imgui.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace nb
{

static std::string U8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Extension -> (system, candidate cores). Only software-rendered cores work:
// NexusBox does not provide OpenGL/Vulkan hardware rendering to cores.
struct SystemInfo { const wchar_t* system; std::vector<std::wstring> cores; };
static const std::map<std::wstring, SystemInfo>& SystemTable()
{
    static const std::map<std::wstring, SystemInfo> t = {
        { L".nes", { L"NES",            { L"fceumm_libretro", L"nestopia_libretro", L"quicknes_libretro" } } },
        { L".fds", { L"NES",            { L"fceumm_libretro", L"nestopia_libretro" } } },
        { L".sfc", { L"SNES",           { L"snes9x_libretro", L"snes9x2010_libretro" } } },
        { L".smc", { L"SNES",           { L"snes9x_libretro", L"snes9x2010_libretro" } } },
        { L".gb",  { L"Game Boy",       { L"gambatte_libretro", L"mgba_libretro" } } },
        { L".gbc", { L"Game Boy Color", { L"gambatte_libretro", L"mgba_libretro" } } },
        { L".gba", { L"Game Boy Advance", { L"mgba_libretro", L"vba_next_libretro" } } },
        { L".md",  { L"Genesis",        { L"genesis_plus_gx_libretro", L"picodrive_libretro" } } },
        { L".gen", { L"Genesis",        { L"genesis_plus_gx_libretro", L"picodrive_libretro" } } },
        { L".smd", { L"Genesis",        { L"genesis_plus_gx_libretro", L"picodrive_libretro" } } },
        { L".sms", { L"Master System",  { L"genesis_plus_gx_libretro", L"picodrive_libretro" } } },
        { L".gg",  { L"Game Gear",      { L"genesis_plus_gx_libretro" } } },
        { L".pce", { L"PC Engine",      { L"mednafen_pce_fast_libretro" } } },
        { L".ngp", { L"Neo Geo Pocket", { L"mednafen_ngp_libretro" } } },
        { L".ngc", { L"Neo Geo Pocket Color", { L"mednafen_ngp_libretro" } } },
        { L".ws",  { L"WonderSwan",     { L"mednafen_wswan_libretro" } } },
        { L".wsc", { L"WonderSwan Color", { L"mednafen_wswan_libretro" } } },
        { L".a26", { L"Atari 2600",     { L"stella2014_libretro", L"stella_libretro" } } },
        { L".cue", { L"PlayStation",    { L"pcsx_rearmed_libretro", L"swanstation_libretro" } } },
        { L".chd", { L"PlayStation",    { L"pcsx_rearmed_libretro", L"swanstation_libretro" } } },
        { L".pbp", { L"PlayStation",    { L"pcsx_rearmed_libretro" } } },
        { L".zip", { L"Arcade",         { L"fbneo_libretro", L"mame2003_plus_libretro" } } },
    };
    return t;
}

bool Dashboard::Initialize(Renderer* renderer, CoreEngine* engine)
{
    m_renderer = renderer;
    m_engine   = engine;
    m_coresDir = std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()) + L"\\cores";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "nexusbox_xbox";

    // 10-foot UI: large font and widgets (the TV is several feet away).
    ImFontConfig cfg;
    cfg.SizePixels = 30.0f;
    io.Fonts->AddFontDefault(&cfg);

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.ScaleAllSizes(2.0f);
    st.WindowRounding = 12.0f;
    st.FrameRounding = 8.0f;
    st.Colors[ImGuiCol_WindowBg]      = ImVec4(0.04f, 0.05f, 0.08f, 1.00f);
    st.Colors[ImGuiCol_Header]        = ImVec4(0.10f, 0.45f, 0.20f, 0.60f);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.60f, 0.25f, 0.85f);
    st.Colors[ImGuiCol_HeaderActive]  = ImVec4(0.14f, 0.70f, 0.30f, 1.00f);
    st.Colors[ImGuiCol_Button]        = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);
    st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.60f, 0.25f, 0.85f);
    st.Colors[ImGuiCol_ButtonActive]  = ImVec4(0.14f, 0.70f, 0.30f, 1.00f);
    st.Colors[ImGuiCol_NavCursor]  = ImVec4(0.40f, 1.00f, 0.50f, 1.00f);

    if (!m_renderer->InitImGui()) return false;
    ScanLibrary();
    return true;
}

void Dashboard::Shutdown()
{
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
}

void Dashboard::FeedGamepad(const PadState& p)
{
    ImGuiIO& io = ImGui::GetIO();
    auto b = [&](PadButton x) { return (p.buttons & (1u << (int)x)) != 0; };
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  b(PadButton::A));
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, b(PadButton::B));
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  b(PadButton::X));
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    b(PadButton::Y));
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp,    b(PadButton::DPadUp));
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown,  b(PadButton::DPadDown));
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,  b(PadButton::DPadLeft));
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, b(PadButton::DPadRight));
    io.AddKeyEvent(ImGuiKey_GamepadL1,        b(PadButton::LB));
    io.AddKeyEvent(ImGuiKey_GamepadR1,        b(PadButton::RB));
    io.AddKeyEvent(ImGuiKey_GamepadStart,     b(PadButton::Menu));
    io.AddKeyEvent(ImGuiKey_GamepadBack,      b(PadButton::View));
    auto stick = [&](ImGuiKey key, float v) { io.AddKeyAnalogEvent(key, v > 0.3f, std::clamp(v, 0.0f, 1.0f)); };
    stick(ImGuiKey_GamepadLStickUp,    p.ly);
    stick(ImGuiKey_GamepadLStickDown, -p.ly);
    stick(ImGuiKey_GamepadLStickLeft, -p.lx);
    stick(ImGuiKey_GamepadLStickRight, p.lx);
}

ImDrawData* Dashboard::Update(float dt, const PadState& pad)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = std::max(dt, 1.0f / 1000.0f);
    io.DisplaySize = ImVec2((float)m_renderer->Width(), (float)m_renderer->Height());

    // While a game is running the UI is hidden and must not eat the inputs.
    if (m_screen != Screen::Hidden) FeedGamepad(pad);
    else                            FeedGamepad(PadState{});

    // Menu button on the Library opens Settings; B backs out of sub-screens.
    const uint16_t pressed = pad.buttons & ~m_prevButtons;
    m_prevButtons = pad.buttons;
    auto hit = [&](PadButton x) { return (pressed & (1u << (int)x)) != 0; };
    if (m_screen == Screen::Library && hit(PadButton::Menu)) { m_screen = Screen::Settings; m_focusFirst = true; }
    else if (m_screen == Screen::Settings && (hit(PadButton::B) || hit(PadButton::Menu))) { m_screen = Screen::Library; m_focusFirst = true; }
    else if (m_screen == Screen::InGameMenu && hit(PadButton::B)) { m_engine->Resume(); m_screen = Screen::Hidden; }

    ImGui::NewFrame();
    switch (m_screen)
    {
    case Screen::Library:    DrawLibrary();    break;
    case Screen::Settings:   DrawSettings();   break;
    case Screen::InGameMenu: DrawInGameMenu(); break;
    case Screen::Hidden:     break;
    }
    DrawToast(dt);
    ImGui::Render();
    return ImGui::GetDrawData();
}

void Dashboard::OpenInGameMenu()
{
    m_engine->Pause();
    m_screen = Screen::InGameMenu;
    m_focusFirst = true;
}

void Dashboard::Toast(const std::wstring& msg, float seconds)
{
    m_toast = U8(msg);
    m_toastTime = seconds;
    OutputDebugStringW((L"[NexusBox] " + msg + L"\n").c_str());
}

void Dashboard::DrawToast(float dt)
{
    if (m_toastTime <= 0.0f) return;
    m_toastTime -= dt;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 60.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);
    ImGui::PushTextWrapPos(io.DisplaySize.x * 0.7f);
    ImGui::TextUnformatted(m_toast.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// ------------------------------------------------------------------ library

bool Dashboard::CoreInstalled(const std::wstring& dll) const
{
    std::error_code ec;
    return fs::exists(m_coresDir + L"\\" + dll + L".dll", ec);
}

void Dashboard::ScanLibrary()
{
    m_games.clear();
    m_scannedDirs.clear();
    std::vector<std::wstring> roots = { m_engine->RomsDir() };
    for (wchar_t d : { L'D', L'E', L'F', L'G' })
        roots.push_back(std::wstring(1, d) + L":\\roms");

    const auto& table = SystemTable();
    for (const auto& root : roots)
    {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        m_scannedDirs.push_back(root);
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = it->path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            auto t = table.find(ext);
            if (t == table.end()) continue;
            m_games.push_back({ it->path().wstring(), it->path().stem().wstring(), t->second.system, t->second.cores });
        }
    }
    std::sort(m_games.begin(), m_games.end(), [](const GameEntry& a, const GameEntry& b)
    { return a.system != b.system ? a.system < b.system : _wcsicmp(a.title.c_str(), b.title.c_str()) < 0; });
}

static void BeginFullscreen(const char* id)
{
    ImGuiIO& io = ImGui::GetIO();
    // Keep 5% inside the TV-safe area.
    const float mx = io.DisplaySize.x * 0.05f, my = io.DisplaySize.y * 0.05f;
    ImGui::SetNextWindowPos(ImVec2(mx, my));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 2 * mx, io.DisplaySize.y - 2 * my));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void Dashboard::DrawLibrary()
{
    BeginFullscreen("##library");
    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f), "NEXUSBOX");
    ImGui::SameLine();
    ImGui::TextDisabled("  %d games", (int)m_games.size());
    ImGui::Separator();

    const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
    if (m_games.empty())
    {
        ImGui::TextWrapped("No games found. Copy ROMs into one of these folders, then choose Rescan:");
        ImGui::BulletText("%s", U8(m_engine->RomsDir()).c_str());
        ImGui::BulletText("E:\\roms (USB drive)");
        ImGui::Spacing();
        ImGui::TextWrapped("Tip: in Xbox Device Portal, open File explorer > LocalAppData > NexusBox > LocalState > roms.");
        ImGui::Spacing();
        if (m_focusFirst) { ImGui::SetKeyboardFocusHere(); m_focusFirst = false; }
        if (ImGui::Button("Rescan")) ScanLibrary();
    }
    else
    {
        std::wstring lastSystem;
        for (size_t i = 0; i < m_games.size(); ++i)
        {
            const auto& g = m_games[i];
            if (g.system != lastSystem)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", U8(g.system).c_str());
                lastSystem = g.system;
            }
            ImGui::PushID((int)i);
            if (m_focusFirst && i == 0) { ImGui::SetKeyboardFocusHere(); m_focusFirst = false; }
            if (ImGui::Selectable(U8(g.title).c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 48)))
                Launch(g);
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (ImGui::Button("Rescan")) ScanLibrary();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled("A: Play     Menu: Settings     In game: View + Menu opens the menu");
    ImGui::End();
}

void Dashboard::Launch(const GameEntry& g)
{
    std::wstring chosen;
    for (const auto& c : g.cores) if (CoreInstalled(c)) { chosen = c; break; }
    if (chosen.empty())
    {
        std::wstring names;
        for (const auto& c : g.cores) names += (names.empty() ? L"" : L" or ") + c + L".dll";
        Toast(L"No core installed for " + g.system + L". Add " + names + L" to the cores folder and redeploy.", 8.0f);
        return;
    }
    std::wstring error;
    if (!m_engine->LoadGame(L"cores\\" + chosen + L".dll", g.path, error))
    {
        Toast(error, 8.0f);
        return;
    }
    m_screen = Screen::Hidden;
    Toast(L"Playing " + g.title + L"  -  press View + Menu for the menu", 4.0f);
}

// ------------------------------------------------------------------ settings

void Dashboard::DrawSettings()
{
    BeginFullscreen("##settings");
    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f), "SETTINGS");
    ImGui::Separator();

    int scale = (int)m_renderer->GetScaleMode();
    if (m_focusFirst) { ImGui::SetKeyboardFocusHere(); m_focusFirst = false; }
    if (ImGui::RadioButton("Integer scaling (sharpest)", scale == 2)) m_renderer->SetScaleMode(ScaleMode::Integer);
    if (ImGui::RadioButton("Fit screen, sharp pixels", scale == 0))   m_renderer->SetScaleMode(ScaleMode::Nearest);
    if (ImGui::RadioButton("Fit screen, smooth", scale == 1))         m_renderer->SetScaleMode(ScaleMode::Bilinear);
    ImGui::Spacing();
    bool vsync = m_renderer->GetVsync();
    if (ImGui::Checkbox("VSync", &vsync)) m_renderer->SetVsync(vsync);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Cores folder: %s", U8(m_coresDir).c_str());
    ImGui::TextDisabled("BIOS folder:  %s", U8(m_engine->SystemDir()).c_str());
    ImGui::TextDisabled("Saves folder: %s", U8(m_engine->SavesDir()).c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back")) { m_screen = Screen::Library; m_focusFirst = true; }
    ImGui::End();
}

// ------------------------------------------------------------------ in-game

void Dashboard::DrawInGameMenu()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size(760, 0);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("##ingame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f), "%s", U8(m_engine->GameTitle()).c_str());
    ImGui::TextDisabled("%s   %.0f fps", m_engine->CoreName().c_str(), m_engine->Profile().AvgFps());
    ImGui::Separator();

    const ImVec2 bw(-FLT_MIN, 0);
    if (m_focusFirst) { ImGui::SetKeyboardFocusHere(); m_focusFirst = false; }
    if (ImGui::Button("Resume", bw)) { m_engine->Resume(); m_screen = Screen::Hidden; }

    ImGui::Text("State slot: %d", m_stateSlot);
    ImGui::SameLine();
    if (ImGui::SmallButton(" - ")) m_stateSlot = std::max(1, m_stateSlot - 1);
    ImGui::SameLine();
    if (ImGui::SmallButton(" + ")) m_stateSlot = std::min(9, m_stateSlot + 1);

    std::wstring msg;
    if (ImGui::Button("Save state", bw)) { m_engine->SaveState(m_stateSlot, msg); Toast(msg, 3.0f); }
    if (ImGui::Button("Load state", bw))
    {
        if (m_engine->LoadState(m_stateSlot, msg)) { m_engine->Resume(); m_screen = Screen::Hidden; }
        Toast(msg, 3.0f);
    }
    bool ff = m_engine->FastForward();
    if (ImGui::Checkbox("Fast forward (4x)", &ff)) m_engine->SetFastForward(ff);
    if (ImGui::Button("Reset game", bw)) { m_engine->ResetGame(); m_engine->Resume(); m_screen = Screen::Hidden; }
    if (ImGui::Button("Quit to library", bw))
    {
        m_engine->UnloadGame();
        m_screen = Screen::Library;
        m_focusFirst = true;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("B: Resume");
    ImGui::End();
}

} // namespace nb
