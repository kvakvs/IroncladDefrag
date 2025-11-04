#include "../precompiled.h"
#include "App.h"

App::App() { InitializeComponent(); }

void App::OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    m_window = winrt::make<MainWindow>();
    m_window.Activate();
}
