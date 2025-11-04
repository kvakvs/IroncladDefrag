#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

#include "MainWindow.h"

struct App : winrt::implements<App, winrt::Microsoft::UI::Xaml::ApplicationT<App> >
{
    App();
    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    winrt::Windows::UI::Xaml::Window m_window{ nullptr };
};
