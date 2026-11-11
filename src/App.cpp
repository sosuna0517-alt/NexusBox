// App.cpp — Xbox Dev Mode lifecycle and the UI/render loop.
#include "App.h"
#include "render/Renderer.h"
#include "CoreEngine.h"
#include "InputManager.h"
#include "ui/Dashboard.h"

#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Gaming.Input.h>

#include <chrono>
#include <thread>

// Everything is winrt::-qualified: wrl/client.h declares a global ::Windows
// namespace, so an unqualified "Windows::" would be ambiguous (C2872).

namespace nb
{
using winrt::Windows::ApplicationModel::Core::CoreApplication;
using winrt::Windows::ApplicationModel::Core::CoreApplicationView;
using winrt::Windows::ApplicationModel::Core::IFrameworkView;
using winrt::Windows::ApplicationModel::SuspendingEventArgs;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::UI::Core::CoreWindow;
using winrt::Windows::UI::Core::CoreProcessEventsOption;

IFrameworkView AppViewSource::CreateView() { return winrt::make<App>(); }

App::App()  = default;
App::~App() = default;

void App::Initialize(CoreApplicationView const& view)
{
    view.Activated([](CoreApplicationView const&, auto const&)
    {
        // Required: until the window is activated Xbox keeps showing the
        // splash screen (the black screen with a crossed-out square).
        CoreWindow::GetForCurrentThread().Activate();
    });
    CoreApplication::Suspending({ this, &App::OnSuspending });
    CoreApplication::Resuming({ this, &App::OnResuming });
}

void App::SetWindow(CoreWindow const& window)
{
    m_window = window;
    window.VisibilityChanged({ this, &App::OnVisibilityChanged });
    window.Closed({ this, &App::OnClosed });

    // Use the whole screen (not just the TV-safe area); the UI pads itself.
    winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()
        .SetDesiredBoundsMode(winrt::Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

    // The B button raises BackRequested on Xbox; mark it handled so the shell
    // does not navigate away from the app. NexusBox reads B through the gamepad.
    winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView().BackRequested(
        [](auto const&, winrt::Windows::UI::Core::BackRequestedEventArgs const& args) { args.Handled(true); });
}

void App::Load(winrt::hstring const&) {}

void App::Run()
{
    m_window.Activate();

    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->Initialize(m_window))
    {
        OutputDebugStringA("[NexusBox] Fatal: renderer failed to initialize\n");
        return;
    }
    m_input = std::make_unique<InputManager>();
    m_input->Initialize();
    m_engine = std::make_unique<CoreEngine>();
    m_engine->Initialize(m_renderer.get(), m_input.get());
    m_engine->Start();
    m_dashboard = std::make_unique<Dashboard>();
    if (!m_dashboard->Initialize(m_renderer.get(), m_engine.get()))
        OutputDebugStringA("[NexusBox] Dashboard failed to initialize\n");

    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    while (m_running)
    {
        m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        if (!m_visible) { ::Sleep(50); last = clock::now(); continue; }

        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        const PadState pad = m_input->Snapshot(0)[0];

        // In game: View + Menu (or L3 + R3) opens the in-game menu.
        const uint16_t comboA = (1u << (int)PadButton::View) | (1u << (int)PadButton::Menu);
        const uint16_t comboB = (1u << (int)PadButton::LS)   | (1u << (int)PadButton::RS);
        const bool comboNow  = (pad.buttons & comboA) == comboA || (pad.buttons & comboB) == comboB;
        const bool comboPrev = (m_prevButtons & comboA) == comboA || (m_prevButtons & comboB) == comboB;
        m_prevButtons = pad.buttons;
        if (m_dashboard->GetScreen() == Dashboard::Screen::Hidden && comboNow && !comboPrev)
            m_dashboard->OpenInGameMenu();

        ImDrawData* ui = m_dashboard->Update(dt, pad);
        m_renderer->Render(ui);   // presents with vsync: paces this loop to the TV
        if (!m_renderer->Ok()) break;
    }

    m_engine->Shutdown();
    m_input->Shutdown();
    m_dashboard->Shutdown();
    m_renderer->Shutdown();
}

void App::Uninitialize() {}

// --- Lifecycle --------------------------------------------------------------

void App::OnSuspending(IInspectable const&, SuspendingEventArgs const& args)
{
    auto deferral = args.SuspendingOperation().GetDeferral();
    if (m_engine) m_engine->PrepareForSuspension();   // pause + write SRAM
    if (m_dashboard && m_engine && m_engine->HasGame() &&
        m_dashboard->GetScreen() == Dashboard::Screen::Hidden)
        m_dashboard->OpenInGameMenu();
    deferral.Complete();
}

void App::OnResuming(IInspectable const&, IInspectable const&)
{
    if (m_engine) m_engine->OnResume();
}

void App::OnVisibilityChanged(CoreWindow const&, winrt::Windows::UI::Core::VisibilityChangedEventArgs const& args)
{
    m_visible = args.Visible();
}

void App::OnClosed(CoreWindow const&, winrt::Windows::UI::Core::CoreWindowEventArgs const&)
{
    m_running = false;
}

} // namespace nb

// UWP entry point.
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::Windows::ApplicationModel::Core::CoreApplication::Run(winrt::make<nb::AppViewSource>());
    return 0;
}
