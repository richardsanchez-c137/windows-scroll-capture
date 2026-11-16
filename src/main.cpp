#include <windows.h>
#include <winrt/base.h>
#include <string>

#include "graphics/graphics_runtime.hpp"
#include "graphics/window_renderer.hpp"
#include "core/logger.hpp"
#include "capture/capture_runtime.hpp"


namespace {

constexpr wchar_t WINDOW_CLASS_NAME[] =
    L"WindowsSnipTool.MainWindow";

constexpr wchar_t WINDOW_TITLE[] =
    L"Windows Snip Tool";

constexpr COLORREF BACKGROUND_COLOR =
    RGB(24, 27, 33);

LRESULT CALLBACK MainWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {

    case WM_ERASEBKGND:
        return 1;

    case WM_NCCREATE: {
        const auto* createInfo =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                createInfo->lpCreateParams
            )
        );

        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);

        auto* renderer =
            reinterpret_cast<windows_snip::WindowRenderer*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );

        if (renderer) {
            renderer->Render();
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        auto* renderer =
            reinterpret_cast<windows_snip::WindowRenderer*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );

        if (renderer) {
            renderer->Resize(
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam))
            );
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        auto* renderer =
            reinterpret_cast<windows_snip::WindowRenderer*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );

        if (renderer) {
            renderer->HandleMouseButtonUp(
                static_cast<float>(LOWORD(lParam)),
                static_cast<float>(HIWORD(lParam))
            );
        }

        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

bool InitializeDpiAwareness() {

    if (SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    )) {
        return true;
    }

    // ERROR_ACCESS_DENIED can indicate that DPI awareness
    // was already configured by the application manifest.
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        return true;
    }

    return false;
}

bool RegisterMainWindowClass(HINSTANCE instance) {

    WNDCLASSEXW windowClass{};

    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(
        nullptr,
        IDC_ARROW
    );
    windowClass.hIcon = LoadIconW(
        nullptr,
        IDI_APPLICATION
    );
    windowClass.lpszClassName = WINDOW_CLASS_NAME;

    return RegisterClassExW(&windowClass) != 0;
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand
) {
    // 1. Configure per-monitor DPI awareness.
    if (!InitializeDpiAwareness()) {
        MessageBoxW(
            nullptr,
            L"Could not initialize DPI awareness.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    // Initialize application logging.
    windows_snip::Logger logger;

    std::wstring loggerError;

    if (logger.Initialize(loggerError)) {
        logger.Write(
            windows_snip::LogLevel::Info,
            L"Windows Snip Tool application starting."
        );
    }
    else {
        OutputDebugStringW(
            L"Warning: Application logging could not be initialized.\n"
        );
        OutputDebugStringW(loggerError.c_str());
        OutputDebugStringW(L"\n");
    }

   // Initialize COM and the Windows Runtime.
    try {
        winrt::init_apartment(
            winrt::apartment_type::single_threaded
        );
    }
    catch (const winrt::hresult_error& runtimeError) {

        const auto runtimeMessage = runtimeError.message();

        logger.Write(
            windows_snip::LogLevel::Error,
            L"Windows Runtime initialization failed."
        );

        MessageBoxW(
            nullptr,
            runtimeMessage.c_str(),
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        logger.Shutdown();
        return 1;
    }


    // Initialize the shared graphics runtime.
    windows_snip::WindowRenderer renderer;
    windows_snip::GraphicsRuntime graphics;

    std::wstring graphicsError;

    if (!graphics.Initialize(graphicsError)) {
        MessageBoxW(
            nullptr,
            graphicsError.c_str(),
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        winrt::uninit_apartment();
        return 1;
    }

    // Initialize the Direct3D 11 capture runtime.
    windows_snip::CaptureRuntime captureRuntime;

    std::wstring captureError;

    if (!captureRuntime.Initialize(captureError)) {

        logger.Write(
            windows_snip::LogLevel::Critical,
            L"Direct3D 11 capture runtime initialization failed."
        );

        MessageBoxW(
            nullptr,
            captureError.c_str(),
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        graphics.Shutdown();
        logger.Shutdown();
        winrt::uninit_apartment();

        return 1;
    }

    // Record which Direct3D initialization path succeeded.
    if (captureRuntime.IsHardwareAccelerated()) {

        logger.Write(
            windows_snip::LogLevel::Info,
            L"Direct3D 11 initialized using hardware acceleration."
        );

    }
    else {

        logger.Write(
            windows_snip::LogLevel::Warning,
            L"Direct3D 11 initialized using the WARP software fallback."
        );
    }

    // Identify the GPU backing the Direct3D device.
    const std::wstring adapterDescription =
        captureRuntime.GetAdapterDescription();

    if (!adapterDescription.empty()) {

        logger.Write(
            windows_snip::LogLevel::Info,
            L"Direct3D adapter: " + adapterDescription
        );

    }
    else {

        logger.Write(
            windows_snip::LogLevel::Warning,
            L"Could not retrieve the Direct3D adapter description."
        );
    }
    // Check Windows Graphics Capture availability.
    if (captureRuntime.IsCaptureSupported()) {

        logger.Write(
            windows_snip::LogLevel::Info,
            L"Windows Graphics Capture is supported."
        );

    }
    else {

        logger.Write(
            windows_snip::LogLevel::Warning,
            L"Windows Graphics Capture is not supported."
        );
    }

    // 3. Register the main window class.
    if (!RegisterMainWindowClass(instance)) {
        MessageBoxW(
            nullptr,
            L"Could not register the application window class.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );
        captureRuntime.Shutdown();
        graphics.Shutdown();
        logger.Shutdown();
        winrt::uninit_apartment();

        return 1;
    }
    logger.Write(
        windows_snip::LogLevel::Info,
        L"Direct2D, DirectWrite and WIC initialized successfully."
    );

    // 4. Define the desired client area.
    constexpr int CLIENT_WIDTH = 1100;
    constexpr int CLIENT_HEIGHT = 720;

    RECT windowRect{
        0, 0,
        CLIENT_WIDTH,
        CLIENT_HEIGHT
    };

    const DWORD windowStyle = WS_OVERLAPPEDWINDOW;

    AdjustWindowRectEx(
        &windowRect,
        windowStyle,
        FALSE,
        0
    );

    const int windowWidth =
        windowRect.right - windowRect.left;

    const int windowHeight =
        windowRect.bottom - windowRect.top;

    // 5. Center the application in the work area.
    RECT workArea{};

    SystemParametersInfoW(
        SPI_GETWORKAREA,
        0,
        &workArea,
        0
    );

    const int x =
        workArea.left +
        ((workArea.right - workArea.left) - windowWidth) / 2;

    const int y =
        workArea.top +
        ((workArea.bottom - workArea.top) - windowHeight) / 2;

    // 6. Create the application window.
    HWND hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        windowStyle,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        &renderer
    );

    if (!hwnd) {
        MessageBoxW(
            nullptr,
            L"Could not create the main application window.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        captureRuntime.Shutdown();
        graphics.Shutdown();
        logger.Shutdown();
        winrt::uninit_apartment();

        return 1;
    }

    std::wstring rendererError;

    if (!renderer.Initialize(
        hwnd,
        graphics,
        rendererError
    )) {
        MessageBoxW(
            nullptr,
            rendererError.c_str(),
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR
        );

        DestroyWindow(hwnd);
        UnregisterClassW(WINDOW_CLASS_NAME, instance);

        renderer.Shutdown();
        captureRuntime.Shutdown();
        graphics.Shutdown();
        logger.Shutdown();
        winrt::uninit_apartment();

        return 1;
    }
    logger.Write(
        windows_snip::LogLevel::Info,
        L"Direct2D window renderer initialized successfully."
    );

    // 7. Display the application.
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    // 8. Run the Windows message loop.
    MSG message{};

    int messageResult = 0;

    while ((messageResult = GetMessageW(
        &message,
        nullptr,
        0,
        0
    )) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    // 9. Release application resources.
    UnregisterClassW(WINDOW_CLASS_NAME, instance);
    logger.Write(
        windows_snip::LogLevel::Info,
        L"Windows Snip Tool application shutting down."
    );

    renderer.Shutdown();
    captureRuntime.Shutdown();
    graphics.Shutdown();
    logger.Shutdown();
    winrt::uninit_apartment();

    return messageResult == -1 ? 1 : 0;
}
