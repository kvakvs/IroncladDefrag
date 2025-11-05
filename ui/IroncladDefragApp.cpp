#include "../precompiled.h"
#include "IroncladDefragApp.h"

// IroncladDefragApp::IroncladDefragApp() {
    // Optional: set up app-level resources or theme
    // Suspending({ this, &IroncladDefragApp::OnSuspending });
    // Resuming({ this, &IroncladDefragApp::OnResuming });
// }

void IroncladDefragApp::OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    m_window = winrt::make<MainWindow>();
    m_window.Activate();
}
