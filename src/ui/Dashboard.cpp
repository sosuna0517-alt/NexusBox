// Dashboard.cpp — the 10-foot shell.
#include "Dashboard.h"
#include "Scraper.h"
#include "render/Renderer.h"
#include "CoreEngine.h"
#include "InputManager.h"
#include "imgui.h"
// Platform + DX12 glue comes from src/ui/ImguiBackend.* (UWP-safe; the
// desktop imgui_impl_win32/dx12 backends do not compile under WindowsStore).

#include <windows.h>
#include <algorithm>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace nb
{

// Tuned for the 10-foot experience: big rows, generous padding, readable at 4K
// from a couch. All coordinates are ImGui font-size-relative so they scale.
static constexpr float kTileW    = 420.0f;   // logical px at 1080p design size
static constexpr float kTileH    = 590.0f;   // 3:4-ish box art ratio
static constexpr float kTileGap  = 36.0f;
static constexpr float kRowScrollSpeed = 14.0f;

void Dashboard::Initialize(Renderer* renderer, CoreEngine* engine)
{
    m_renderer = renderer;
    m_engine   = engine;
    m_scraper  = std::make_unique<Scraper>();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // 10-foot: gamepad-first
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.IniFilename = nullptr;                              // no config in sandbox

    // 10-foot legibility: 28pt base (≈ 2x desktop default). Freetype rasterizer
    // recommended; bitmap fonts look soft at 4K.
    io.FontGlobalScale = 1.0f;
    ImFontConfig cfg;
    cfg.SizePixels = 28.0f;
    // io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 28.0f, &cfg);

    m_renderer->InitImGuiBackend();   // Win32 handler + D3D12 device/queue binding

    // Load library from all mounted sources.
    auto games = StorageManager::ScanLibrary();
    for (auto& g : games)
    {
        LibraryEntry e;
        e.game = g;
        e.displayName = g.name;
        e.system = m_scraper->DetectSystem(g.path);
        e.coreId = m_scraper->PickCore(e.system);
        m_library.push_back(std::move(e));
    }
    // Scrape metadata asynchronously (box art, descriptions) after first frame.
    m_scraper->ScrapeLibraryAsync(m_library);

    // System tabs: All + distinct systems with entries.
    m_systemTabs.push_back(L"All");
    for (auto& e : m_library)
        if (std::find(m_systemTabs.begin(), m_systemTabs.end(), e.system) == m_systemTabs.end())
            m_systemTabs.push_back(e.system);

    m_screen = Screen::Boot;
}

void Dashboard::Shutdown()
{
    if (m_scraper) m_scraper->Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// ------------------------------------------------------------------ navigation

void Dashboard::NavigateGrid(int dx, int dy)
{
    // Grid is refiltered per tab; wrap at edges, clamp vertically.
    m_focusX = std::max(0, m_focusX + dx);
    m_focusY = std::max(0, m_focusY + dy);
    m_focusAnimX += (float)dx;   // eased toward 0 by Update
    m_focusAnimY += (float)dy;
}

void Dashboard::Update(float dt, const std::vector<PadState>& pads)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = dt;

    // Gamepad -> ImGui nav (the backend also handles core input when running).
    if (!pads.empty())
    {
        const auto& p = pads[0];
        auto b = [&](PadButton n) { return (p.buttons & (1u << (int)n)) != 0; };
        io.NavInputs[ImGuiNavInput_Activate]  = b(PadButton::A)     ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_Cancel]    = b(PadButton::B)     ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_Input]     = b(PadButton::Menu)  ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadLeft]  = b(PadButton::DPadLeft)  ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadRight] = b(PadButton::DPadRight) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadUp]    = b(PadButton::DPadUp)    ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_DpadDown]  = b(PadButton::DPadDown)  ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_FocusPrev] = b(PadButton::LB) ? 1.0f : 0.0f;
        io.NavInputs[ImGuiNavInput_FocusNext] = b(PadButton::RB) ? 1.0f : 0.0f;
    }

    // Ease focus (spring-ish) for the cover flow feel.
    const float ease = 1.0f - std::pow(0.001f, dt);
    m_focusAnimX *= (1.0f - ease);
    m_focusAnimY *= (1.0f - ease);

    ImGui::NewFrame();

    switch (m_screen)
    {
    case Screen::Boot:        DrawBootSplash(dt); break;
    case Screen::Library:     DrawLibrary(dt);    break;
    case Screen::GameDetail:  DrawGameDetail();   break;
    case Screen::Settings:    DrawSettings();     break;
    case Screen::Overlay:     DrawOverlay();      break;
    default: break;
    }

    ImGui::Render();
    m_renderer->RenderImGui(ImGui::GetDrawData());   // into the same command queue
}

// ------------------------------------------------------------------ screens

void Dashboard::DrawBootSplash(float dt)
{
    m_bootTimer += dt;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.09f, 1.0f));
    ImGui::Begin("##boot", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav);
    const float a = std::clamp(m_bootTimer / 1.2f, 0.0f, 1.0f);
    ImGui::SetCursorPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 220, 400));
    ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, a), "NEXUSBOX");
    ImGui::SetCursorPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 220, 470));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, a), "Press  A  to  continue");
    if (m_bootTimer > 1.2f && ImGui::IsNavInputTest(ImGuiNavInput_Activate, ImGuiNavReadMode_Pressed))
    {
        m_screen = Screen::Library;
        m_bootTimer = 0;
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Dashboard::DrawLibrary(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(64, 40));
    ImGui::Begin("##library", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar);

    // ---- System tabs
    for (int i = 0; i < (int)m_systemTabs.size(); ++i)
    {
        ImGui::SameLine(i == 0 ? 64 : 0);
        ImGui::PushID(i);
        if (ImGui::Selectable(WideToUtf8(m_systemTabs[i]).c_str(), m_activeTab == i,
                              ImGuiSelectableFlags_SelectOnNav))
            m_activeTab = i;
        ImGui::PopID();
    }

    // ---- Cover grid
    ImGui::NewLine();
    const float tileStep = kTileW + kTileGap;
    int col = 0, row = 0;
    for (int gi = 0; gi < (int)m_library.size(); ++gi)
    {
        const auto& e = m_library[gi];
        if (m_activeTab > 0 && e.system != m_systemTabs[m_activeTab]) continue;

        const bool focused = (gi == m_focusX);
        ImGui::SameLine(64.0f + col * tileStep, 0);
        ImGui::PushID(gi);
        ImVec2 size(kTileW, kTileH);
        if (focused)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 8);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
            size.x += 16; size.y += 16;
        }
        // Box art (Scraper caches to <saves>\media); placeholder tint until scraped.
        if (e.boxartUv.empty())
            ImGui::Dummy(size);
        else
            ImGui::Image(m_renderer->TextureId(e.boxartUv), size);
        if (ImGui::IsItemHovered() || focused)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 34);
            ImGui::TextUnformatted(WideToUtf8(e.displayName).c_str());
        }
        if ((focused && ImGui::IsNavInputTest(ImGuiNavInput_Activate, ImGuiNavReadMode_Pressed))
            || ImGui::IsItemClicked())
            Launch(e);
        ImGui::PopID();
        ++col;
        if (col >= (int)(io.DisplaySize.x - 128) / (int)tileStep) { col = 0; ++row; }
    }

    // ---- Bottom hints (10-foot convention)
    ImGui::SetCursorPos(ImVec2(64, io.DisplaySize.y - 80));
    ImGui::TextDisabled("A: Launch    B: Back    Menu: Settings    RB/LB: Systems");

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Dashboard::DrawGameDetail()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##detail", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::TextUnformatted(WideToUtf8(m_library[m_focusX].displayName).c_str());
    ImGui::TextDisabled("%S", m_library[m_focusX].system.c_str());
    ImGui::Separator();
    if (ImGui::Button("Play"))
    {
        Launch(m_library[m_focusX]);
        m_screen = Screen::Library;
    }
    ImGui::SameLine();
    if (ImGui::Button("Back"))
        m_screen = Screen::Library;
    ImGui::End();
}

void Dashboard::DrawSettings()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##settings", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::TextUnformatted("Settings");
    ImGui::Separator();

    static int scaleMode = 2;   // Integer default
    ImGui::RadioButton("Nearest",   &scaleMode, 0); ImGui::SameLine();
    ImGui::RadioButton("Bilinear",  &scaleMode, 1); ImGui::SameLine();
    ImGui::RadioButton("Integer",   &scaleMode, 2);
    m_renderer->SetScaleMode((ScaleMode)scaleMode);

    static bool vsync = true, lowLatency = true;
    ImGui::Checkbox("VSync (off = VRR/tearing allowed)", &vsync);
    m_renderer->SetVsync(vsync);
    ImGui::Checkbox("Low Latency Mode", &lowLatency);
    m_renderer->SetLowLatencyMode(lowLatency);

    static int shaderIdx = 0;
    const char* shaders[] = { "None", "Scanlines", "CRT-Royale" };
    if (ImGui::Combo("Post-processing shader", &shaderIdx, shaders, 3))
        m_renderer->SetPostChain(shaderIdx == 1 ? "scanlines" :
                                 shaderIdx == 2 ? "crt-royale" : nullptr);

    static bool rewind = false;
    ImGui::Checkbox("Rewind (64MB ring)", &rewind);
    m_engine->SetRewindEnabled(rewind);

    static float autoSaveSec = 30.0f;
    if (ImGui::SliderFloat("Auto-save interval (s)", &autoSaveSec, 5.0f, 120.0f, "%.0f"))
        m_engine->States().SetAutoSaveIntervalSec(autoSaveSec);

    ImGui::Separator();
    if (ImGui::Button("Back"))
        m_screen = Screen::Library;
    ImGui::End();
}

void Dashboard::DrawOverlay()
{
    // Quick-access HUD: floats over the running game, drawn into the same
    // frame's command list before present.
    const float w = 560.0f, h = 640.0f;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 48, 48), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.92f));
    ImGui::Begin("NexusBox Overlay", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    ImGui::TextUnformatted("Quick Access");
    ImGui::Separator();

    if (ImGui::Button("Save State (Slot 1)")) m_engine->SaveStateNow(1);
    ImGui::SameLine();
    if (ImGui::Button("Load State (Slot 1)")) m_engine->LoadStateNow(1);

    if (ImGui::Button("Rewind")) m_engine->SetRewindEnabled(true);
    ImGui::SameLine();
    if (ImGui::Button("Stop Rewind")) m_engine->SetRewindEnabled(false);

    static float ff = 1.0f;
    if (ImGui::SliderFloat("Fast forward", &ff, 1.0f, 8.0f, "x%.1f"))
        m_engine->SetFastForward(ff);

    // Real-time profiler readout (from CoreEngine).
    auto& prof = m_engine->Profile();
    ImGui::Text("Frametime: %.2f ms  (%.0f fps)", prof.LastFrameMs(), prof.AvgFps());
    ImGui::Text("Memory:    %.0f MB", Profiler::MemMB());
    ImGui::Text("Latency:   input->present budget %.2f ms", 1000.0f / prof.AvgFps());

    ImGui::Separator();
    if (ImGui::Button("Resume"))
    {
        m_engine->Resume();
        m_overlayOpen = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Dashboard::ShowLibrary()  { m_screen = Screen::Library; m_overlayOpen = false; }
void Dashboard::ShowOverlay()  { m_screen = Screen::Overlay; m_overlayOpen = true;  }
void Dashboard::HideOverlay()  { if (m_overlayOpen) { m_screen = Screen::Library; m_overlayOpen = false; } }

void Dashboard::Launch(const LibraryEntry& e)
{
    // Resolve the core DLL and launch. Failure surfaces a toast in the overlay.
    std::wstring corePath = StorageManager::CoresDir() + L"\\" + e.coreId + L".dll";
    if (!m_engine->LoadCore(corePath.c_str(), e.game.path.c_str()))
    {
        // TODO: toast notification ("core not found").
        return;
    }
    m_screen = Screen::Overlay;   // HUD active over the running game
    m_overlayOpen = true;
}

} // namespace nb
