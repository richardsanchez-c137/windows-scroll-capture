#include <windows.h>
#include <string>

#include "scroller/version.hpp"

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int
) {
    const std::wstring message =
        L"Project initialized successfully.\n\nVersion: "
        + std::wstring(scroller::kVersion);

    MessageBoxW(
        nullptr,
        message.c_str(),
        L"Windows Scroll Capture",
        MB_OK | MB_ICONINFORMATION
    );

    return 0;
}