#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

struct MainWindow : winrt::implements<MainWindow, winrt::Microsoft::UI::Xaml::WindowT<MainWindow>>
{
    MainWindow();
};