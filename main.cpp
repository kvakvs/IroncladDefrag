#include "precompiled.h"
#include "ui/App.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::make<App>();
    winrt::Windows::UI::Xaml::Application::Start([](auto &&) {});
    return 0;
}
