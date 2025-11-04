#include "../precompiled.h"

#include "MainWindow.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using namespace Collections;

static auto createWindow() {
    // Create the root panel
    const StackPanel rootPanel;
    rootPanel.Orientation(Orientation::Vertical);

    // Create the MenuBar
    const MenuBar menuBar;

    // File menu
    const MenuBarItem fileMenu;
    fileMenu.Title(L"File");

    const MenuFlyoutItem openItem;
    openItem.Text(L"Open");

    const MenuFlyoutItem saveItem;
    saveItem.Text(L"Save");

    const MenuFlyoutItem exitItem;
    exitItem.Text(L"Exit");
    exitItem.Click([](IInspectable const&, RoutedEventArgs const&) {
        Application::Current().Exit();
    });

    const auto fileMenuItems = fileMenu.Items().as<IVector<MenuFlyoutItemBase>>();
    fileMenuItems.Append(openItem);
    fileMenuItems.Append(saveItem);
    fileMenuItems.Append(exitItem);

    // Edit menu
    const MenuBarItem editMenu;
    editMenu.Title(L"Edit");

    const MenuFlyoutItem cutItem;
    cutItem.Text(L"Cut");

    const MenuFlyoutItem copyItem;
    copyItem.Text(L"Copy");

    const MenuFlyoutItem pasteItem;
    pasteItem.Text(L"Paste");

    const auto editMenuItems = editMenu.Items().as<IVector<MenuFlyoutItemBase>>();
    editMenuItems.Append(cutItem);
    editMenuItems.Append(copyItem);
    editMenuItems.Append(pasteItem);

    // Add menus to MenuBar
    const auto menuBarItems = menuBar.Items().as<IVector<MenuBarItem>>();
    menuBarItems.Append(fileMenu);
    menuBarItems.Append(editMenu);

    // Add MenuBar to root panel
    rootPanel.Children().Append(menuBar);

    // Add a welcome message
    const TextBlock welcomeText;
    welcomeText.Text(L"Welcome to WinUI 3!");
    welcomeText.Margin(ThicknessHelper::FromUniformLength(20));
    rootPanel.Children().Append(welcomeText);

    // Set the content of the window
    return rootPanel;
}

MainWindow::MainWindow() {
    this->Content(createWindow());
}
