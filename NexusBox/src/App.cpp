// App.cpp — Xbox Dev Mode lifecycle.
// Xbox suspends UWP apps aggressively; we hook Suspending to flush SRAM and
// persist state, and resume by rebuilding GPU resources (device may be lost).
#include "App.h"

#include "render/Renderer.h"   // complete type for unique_ptr<Renderer> use

#include <winrt/Windows.UI.Core.h>       // Visibility/Size args, Dispatcher, ProcessEvents
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Gaming.Input.h>

#include <memory>
#include <thread>

// Note: no "using namespace winrt::Windows..." here. wrl/client.h (pulled in by
// ComPtr usage) declares its own global ::Windows namespace stub, so unqualified
// "Windows::" is ambiguous (C2872). Everything is winrt::-qualified.

namespace nb
{
using winrt::Windows::ApplicationModel::Core::CoreApplication;
using winrt::Windows::ApplicationModel::Core::CoreApplicationView;
using winrt::Windows::ApplicationModel::Core::IFrameworkView;
using winrt::Windows::ApplicationModel::SuspendingEventArgs;
using winrt::Windows::ApplicationModel::SuspendingDeferral;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::UI::Core::CoreWindow;
using winrt::Windows::UI::Core::CoreProcessEventsOption;
using winrt::Windows::UI::Core::VisibilityChangedEventArgs;
using winrt::Windows::UI::Core::WindowSizeChangedEventArgs;

// ----------------------------------------------------------------------------
// Entry point. UWP uses IFrameworkViewSource instead of main().
// The view source is created via winrt::make — winrt::implements objects must
// be factory-created (stack construction is abstract-class, C2259).
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(winrt::make<AppViewSource>());
    return 0;
}

IFrameworkView AppViewSource::CreateView() { return winrt::make<App>(); }

// ----------------------------------------------------------------------------
void App::Initialize(CoreApplicationView const&)
{
    // InputManager registers itself with Gamepad.GamepadAdded/Removed.
}

void App::SetWindow(CoreWindow const& window)
{
    m_window = window;
    m_suspendingToken = CoreApplication::Suspending({ this, &App::OnSuspending });
    m_resumingToken   = CoreApplication::Resuming({ this, &App::OnResuming });
    m_visibilityToken = window.VisibilityChanged({ this, &App::OnVisibilityChanged });
    m_dpiToken        = window.SizeChanged({ this, &App::OnDpiChanged });
    // Back button (B) is delivered through Windows.Gaming.Input gamepads, not
    // a CoreWindow event; InputManager routes it via CoreEngine::HandleSystemBack.
}

void App::Load(winrt::hstring const&) {}

void App::CreateSwapchain()
{
    m_renderer = std::make_unique<Renderer>();
    m_renderer->Initialize(m_window);            // device, cmd queue, swapchain (4K/120, flip-discard, VRR)
    m_engine  = std::make_unique<CoreEngine>();
    m_engine->Initialize(m_renderer.get());
    m_engine->Start();
}

void App::Run()
{
    CreateSwapchain();
    while (m_running)
    {
        // Drain CoreWindow events (non-blocking; gamepad handled on InputManager thread)
        m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

        if (m_visible)
        {
            m_engine->Tick();                     // hotkeys / overlay pump
            m_renderer->Present();                // VRR / low-latency / frame pacing
        }
        else
        {
            ::Sleep(16);                          // background: stay resident
        }
    }
    m_engine->Shutdown();
    m_renderer->Shutdown();
}

void App::Uninitialize() {}

// --- Lifecycle --------------------------------------------------------------

void App::OnSuspending(CoreApplicationView const&, SuspendingEventArgs const& args)
{
    SuspendingDeferral deferral = args.SuspendingOperation().GetDeferral();
    // We must return fast; do the flush on a worker and complete the deferral.
    std::thread([this, deferral]
    {
        if (m_engine) m_engine->PrepareForSuspension();   // async SRAM + state flush
        deferral.Complete();
    }).detach();
}

void App::OnResuming(CoreApplicationView const&, IInspectable const&)
{
    if (m_renderer) m_renderer->HandleDeviceLost();       // GPU may have been reset
    if (m_engine)  m_engine->OnResume();
}

void App::OnVisibilityChanged(CoreWindow const&, VisibilityChangedEventArgs const& args)
{
    m_visible = args.Visible();
    if (!m_visible && m_engine) m_engine->Pause();
}

void App::OnDpiChanged(CoreWindow const&, WindowSizeChangedEventArgs const& args)
{
    if (m_renderer) m_renderer->OnResize(static_cast<uint32_t>(args.Size().Width),
                                         static_cast<uint32_t>(args.Size().Height));
}

} // namespace nb
