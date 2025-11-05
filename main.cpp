#include "precompiled.h"
#include "ui/IroncladDefragApp.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetDllDirectory(
        L"F:\\Projects\\IroncladDefrag\\external\\Microsoft.WindowsAppSDK.InteractiveExperiences.2.0.250912002-experimental\\runtimes-framework\\win-x64\\native"
        );

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::make<IroncladDefragApp>();
    winrt::Microsoft::UI::Xaml::Application::Start([](auto &&) {});
    return 0;
}
