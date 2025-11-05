#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>

#include "MainWindow.h"

struct IroncladDefragApp : winrt::Microsoft::UI::Xaml::ApplicationT<IroncladDefragApp>
{
    // IroncladDefragApp();
    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    // void OnSuspending(IInspectable const&, winrt::Windows::ApplicationModel::SuspendingEventArgs const&);
    // void OnResuming(IInspectable const&, IInspectable const&);

private:
    winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
};
