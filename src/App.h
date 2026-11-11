#pragma once
// App.h — WinRT CoreApplication view for Xbox Dev Mode (UWP).
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Core.h>
#include <memory>
#include <atomic>

namespace nb
{
class Renderer; class CoreEngine; class InputManager; class Dashboard;

class App : public winrt::implements<App, winrt::Windows::ApplicationModel::Core::IFrameworkView>
{
public:
    App();
    ~App();

    // IFrameworkView
    void Initialize(winrt::Windows::ApplicationModel::Core::CoreApplicationView const&);
    void SetWindow(winrt::Windows::UI::Core::CoreWindow const&);
    void Load(winrt::hstring const&);
    void Run();
    void Uninitialize();

private:
    void OnSuspending(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::ApplicationModel::SuspendingEventArgs const&);
    void OnResuming(winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::Foundation::IInspectable const&);
    void OnVisibilityChanged(winrt::Windows::UI::Core::CoreWindow const&,
                             winrt::Windows::UI::Core::VisibilityChangedEventArgs const&);
    void OnClosed(winrt::Windows::UI::Core::CoreWindow const&,
                  winrt::Windows::UI::Core::CoreWindowEventArgs const&);

    winrt::Windows::UI::Core::CoreWindow m_window{ nullptr };

    std::unique_ptr<Renderer>     m_renderer;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<CoreEngine>   m_engine;
    std::unique_ptr<Dashboard>    m_dashboard;
    std::atomic<bool>             m_visible{ true };
    std::atomic<bool>             m_running{ true };
    uint16_t                      m_prevButtons = 0;
};

class AppViewSource : public winrt::implements<AppViewSource,
    winrt::Windows::ApplicationModel::Core::IFrameworkViewSource>
{
public:
    winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView();
};
} // namespace nb
