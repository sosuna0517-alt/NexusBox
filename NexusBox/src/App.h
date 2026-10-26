#pragma once
// App.h — WinRT lifecycle entry for Xbox Dev Mode (UWP).
// Owns: IFrameworkView, suspend/resume, visibility, D3D12 swapchain bootstrap,
//        and the CoreEngine lifetime.
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Storage.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <atomic>

#include "CoreEngine.h"

namespace nb
{
class Renderer;

class App : public winrt::implements<App, winrt::Windows::ApplicationModel::Core::IFrameworkView>
{
public:
    App() = default;

    // IFrameworkView
    void Initialize(winrt::Windows::ApplicationModel::Core::CoreApplicationView const&);
    void SetWindow(winrt::Windows::UI::Core::CoreWindow const&);
    void Load(winrt::hstring const&);
    void Run();
    void Uninitialize();

private:
    void CreateSwapchain();
    void OnSuspending(winrt::Windows::ApplicationModel::Core::CoreApplicationView const&,
                      winrt::Windows::ApplicationModel::SuspendingEventArgs const&);
    void OnResuming(winrt::Windows::ApplicationModel::Core::CoreApplicationView const&,
                    winrt::Windows::Foundation::IInspectable const&);
    void OnVisibilityChanged(winrt::Windows::UI::Core::CoreWindow const&,
                             winrt::Windows::UI::Core::VisibilityChangedEventArgs const&);
    void OnDpiChanged(winrt::Windows::UI::Core::CoreWindow const&,
                      winrt::Windows::UI::Core::WindowSizeChangedEventArgs const&);

    winrt::Windows::UI::Core::CoreWindow m_window{ nullptr };
    winrt::event_token m_suspendingToken{}, m_resumingToken{}, m_visibilityToken{},
                       m_dpiToken{};

    std::unique_ptr<Renderer>   m_renderer;
    std::unique_ptr<CoreEngine> m_engine;
    std::atomic<bool>           m_visible{ true };
    std::atomic<bool>           m_running{ true };
};

class AppViewSource : public winrt::implements<AppViewSource,
    winrt::Windows::ApplicationModel::Core::IFrameworkViewSource>
{
public:
    winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView();
};
} // namespace nb
