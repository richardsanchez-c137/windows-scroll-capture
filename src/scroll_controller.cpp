
#include "scroll_controller.hpp"
#include "scroll_capture.hpp"

#include <windows.h>
#include <ole2.h>
#include <gdiplus.h>

#include <algorithm>
#include <memory>
#include <string>

namespace {

constexpr wchar_t WINDOW_CLASS[] =
    L"WindowsScrollCaptureController";

constexpr int ID_CAPTURE_MORE = 1001;
constexpr int ID_FINISH_SAVE = 1002;
constexpr int ID_CANCEL = 1003;

constexpr UINT_PTR CAPTURE_SETTLE_TIMER = 1;

// One button press sends three standard wheel notches.
// We can tune this after testing the incremental capture.
constexpr int SCROLL_NOTCHES_PER_TAP = 1;
constexpr UINT SCROLL_SETTLE_MS = 500;

struct ControllerState {
    RECT region{};

    HWND status = nullptr;
    HWND captureButton = nullptr;
    HWND finishButton = nullptr;
    HWND cancelButton = nullptr;

    std::unique_ptr<ScrollCaptureSession> session;
    std::unique_ptr<Gdiplus::Bitmap> previewBitmap;
    std::wstring previewPath;

    RECT previewRect{20, 65, 540, 350};

    bool capturePending = false;
};

ControllerState* getState(HWND hwnd) {
    return reinterpret_cast<ControllerState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA)
    );
}

void setStatus(ControllerState* state, const std::wstring& text) {
    if (state && state->status) {
        SetWindowTextW(state->status, text.c_str());
    }
}

void updateButtons(ControllerState* state) {
    if (!state) {
        return;
    }

    EnableWindow(
        state->captureButton,
        !state->capturePending
    );

    EnableWindow(
        state->finishButton,
        !state->capturePending &&
        state->session &&
        state->session->FrameCount() > 0
    );

    EnableWindow(
        state->cancelButton,
        TRUE
    );
}


bool refreshPreview(
    HWND hwnd,
    ControllerState* state,
    std::wstring& error
) {
    if (!state || !state->session) {
        error = L"Capture session is unavailable.";
        return false;
    }

    // Release the previous image before loading the updated one.
    state->previewBitmap.reset();

    std::wstring path;

    if (!state->session->SaveStitchedPng(path, error)) {
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> bitmap(
        Gdiplus::Bitmap::FromFile(path.c_str(), FALSE)
    );

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        error = L"Could not load the updated preview image.";
        return false;
    }

    state->previewPath = path;
    state->previewBitmap = std::move(bitmap);

    if (hwnd && IsWindow(hwnd)) {
        InvalidateRect(hwnd, &state->previewRect, TRUE);
    }

    return true;
}

// Hide the floating controller so it never becomes part
// of the screenshot.
bool captureCurrentFrame(
    HWND hwnd,
    ControllerState* state,
    std::wstring& error
) {
    if (!state || !state->session) {
        error = L"Screenshot session is unavailable.";
        return false;
    }

    const bool wasVisible =
        IsWindowVisible(hwnd) != FALSE;

    if (wasVisible) {
        ShowWindow(hwnd, SW_HIDE);
        Sleep(120);
    }

    const bool success =
        state->session->CaptureFrame(error);

    if (wasVisible) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
    }

    return success;
}

// Perform exactly one controlled scrolling action.
// The target browser remains foreground because the
// controller uses WS_EX_NOACTIVATE.
bool scrollOneStep(
    const RECT& region,
    std::wstring& error
) {
    POINT previousCursor{};

    if (!GetCursorPos(&previousCursor)) {
        error = L"Could not retrieve the mouse position.";
        return false;
    }

    const int centerX =
        (region.left + region.right) / 2;

    const int centerY =
        (region.top + region.bottom) / 2;

    if (!SetCursorPos(centerX, centerY)) {
        error = L"Could not position the mouse over the capture area.";
        return false;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;

    input.mi.mouseData = static_cast<DWORD>(
        -WHEEL_DELTA * SCROLL_NOTCHES_PER_TAP
    );

    const UINT sent = SendInput(
        1,
        &input,
        sizeof(INPUT)
    );

    SetCursorPos(
        previousCursor.x,
        previousCursor.y
    );

    if (sent != 1) {
        error = L"Windows could not send the scrolling input.";
        return false;
    }

    return true;
}

// Called after one Capture More tap.
// Scrolls once, waits for the page to settle,
// then appends exactly one screenshot frame.
void captureMore(HWND hwnd, ControllerState* state) {
    if (!state || state->capturePending || !state->session) {
        return;
    }

    state->capturePending = true;

    updateButtons(state);

    setStatus(
        state,
        L"Scrolling one step. Waiting for the page to settle..."
    );

    std::wstring error;

    if (!scrollOneStep(state->region, error)) {
        state->capturePending = false;

        setStatus(state, error);
        updateButtons(state);
        return;
    }

    // One-shot delay before capturing the new viewport.
    if (!SetTimer(
        hwnd,
        CAPTURE_SETTLE_TIMER,
        SCROLL_SETTLE_MS,
        nullptr
    )) {
        state->capturePending = false;

        setStatus(state, L"Could not start the capture timer.");
        updateButtons(state);
    }
}

// Finish the session and save all captured frames as one PNG.
void finishAndSave(HWND hwnd, ControllerState* state) {
    if (
        !state ||
        state->capturePending ||
        !state->session
    ) {
        return;
    }

    setStatus(state, L"Stitching your scrolling screenshot...");

    EnableWindow(state->captureButton, FALSE);
    EnableWindow(state->finishButton, FALSE);

    const std::size_t frameCount =
        state->session->FrameCount();

    std::wstring savedPath;
    std::wstring error;

    if (!state->session->SaveStitchedPng(savedPath, error)) {
        std::wstring message =
            L"Could not create the final screenshot.\n\n";

        message += error;

        MessageBoxW(
            hwnd,
            message.c_str(),
            L"Windows Scroll Capture - Stitching Error",
            MB_OK | MB_ICONERROR
        );

        setStatus(state, L"Saving failed. You can try again.");
        updateButtons(state);
        return;
    }

    std::wstring message =
        L"Scrolling screenshot saved successfully!\n\n";

    message += L"Frames captured: ";
    message += std::to_wstring(frameCount);

    message += L"\n\nSaved location:\n";
    message += savedPath;

    MessageBoxW(
        hwnd,
        message.c_str(),
        L"Windows Scroll Capture - Complete",
        MB_OK | MB_ICONINFORMATION
    );

    state->session.reset();

    DestroyWindow(hwnd);
}

LRESULT CALLBACK controllerWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    ControllerState* state = getState(hwnd);

    switch (message) {

    case WM_NCCREATE: {
        auto* info =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                info->lpCreateParams
            )
        );

        return TRUE;
    }

    case WM_CREATE: {
        state = getState(hwnd);
        HINSTANCE instance = GetModuleHandleW(nullptr);

        state->status = CreateWindowExW(
            0,
            L"STATIC",
            L"Initial screenshot captured. Tap Capture More.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 18, 520, 38,
            hwnd, nullptr, instance, nullptr
        );

        state->captureButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Capture More",
            WS_CHILD | WS_VISIBLE |
                WS_TABSTOP | BS_PUSHBUTTON,
            15, 85, 130, 42,
            hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_CAPTURE_MORE)
            ),
            instance,
            nullptr
        );

        state->finishButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Finish && Save",
            WS_CHILD | WS_VISIBLE |
                WS_TABSTOP | BS_PUSHBUTTON,
            155, 85, 130, 42,
            hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_FINISH_SAVE)
            ),
            instance,
            nullptr
        );

        state->cancelButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE |
                WS_TABSTOP | BS_PUSHBUTTON,
            295, 85, 115, 42,
            hwnd,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_CANCEL)
            ),
            instance,
            nullptr
        );

        updateButtons(state);
        return 0;
    }

    // Allow interaction without activating the controller.
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_PAINT: {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    if (state) {
        RECT& area = state->previewRect;

        Gdiplus::Graphics graphics(hdc);

        graphics.SetInterpolationMode(
            Gdiplus::InterpolationModeHighQualityBicubic
        );

        Gdiplus::SolidBrush background(
            Gdiplus::Color(255, 25, 28, 35)
        );

        graphics.FillRectangle(
            &background,
            Gdiplus::Rect(
                area.left,
                area.top,
                area.right - area.left,
                area.bottom - area.top
            )
        );

        if (state->previewBitmap) {
            const float imageWidth =
                static_cast<float>(
                    state->previewBitmap->GetWidth()
                );

            const float imageHeight =
                static_cast<float>(
                    state->previewBitmap->GetHeight()
                );

            const float boxWidth =
                static_cast<float>(area.right - area.left);

            const float boxHeight =
                static_cast<float>(area.bottom - area.top);

            const float scale = (std::min)(
                boxWidth / imageWidth,
                boxHeight / imageHeight
            );

            const int drawWidth =
                static_cast<int>(imageWidth * scale);

            const int drawHeight =
                static_cast<int>(imageHeight * scale);

            const int drawX =
                area.left + (area.right - area.left - drawWidth) / 2;

            const int drawY =
                area.top + (area.bottom - area.top - drawHeight) / 2;

            graphics.DrawImage(
                state->previewBitmap.get(),
                drawX,
                drawY,
                drawWidth,
                drawHeight
            );
        }
        else {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 220, 220));

            TextOutW(
                hdc,
                area.left + 20,
                area.top + 20,
                L"Screenshot preview will appear here",
                38
            );
        }
    }

    EndPaint(hwnd, &ps);
    return 0;
}

    case WM_COMMAND:
        if (!state) {
            return 0;
        }

        switch (LOWORD(wParam)) {

        case ID_CAPTURE_MORE:
            captureMore(hwnd, state);
            return 0;

        case ID_FINISH_SAVE:
            finishAndSave(hwnd, state);
            return 0;

        case ID_CANCEL:
            KillTimer(hwnd, CAPTURE_SETTLE_TIMER);
            state->session.reset();
            DestroyWindow(hwnd);
            return 0;
        }

        return 0;

    case WM_TIMER:
        if (
            wParam == CAPTURE_SETTLE_TIMER &&
            state &&
            state->capturePending
        ) {
            KillTimer(hwnd, CAPTURE_SETTLE_TIMER);

            std::wstring error;

            const bool success =
                captureCurrentFrame(hwnd, state, error);

            state->capturePending = false;

            if (!success) {
                setStatus(state, error);
                updateButtons(state);
                return 0;
            }

            std::wstring status =
                L"Frames captured: ";

            status += std::to_wstring(
                state->session->FrameCount()
            );

            status += L". Tap Capture More to continue.";

            setStatus(state, status);
            updateButtons(state);
        }

        return 0;

    case WM_CLOSE:
        if (state) {
            KillTimer(hwnd, CAPTURE_SETTLE_TIMER);
            state->session.reset();
        }

        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, CAPTURE_SETTLE_TIMER);
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

} // namespace

void runScrollController(const RECT& captureRegion) {

    ControllerState state{};
    state.region = captureRegion;

    // Start the session and capture the initial viewport
    // before displaying the floating controller.
    state.session =
        std::make_unique<ScrollCaptureSession>(captureRegion);

    if (!state.session->Ready()) {
        MessageBoxW(
            nullptr,
            L"Could not initialize the screenshot engine.",
            L"Windows Scroll Capture - Error",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    std::wstring error;

    if (!state.session->CaptureFrame(error)) {
        MessageBoxW(
            nullptr,
            error.c_str(),
            L"Windows Scroll Capture - Initial Capture Error",
            MB_OK | MB_ICONERROR
        );

        return;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = controllerWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = WINDOW_CLASS;

    if (
        !RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS
    ) {
        return;
    }

    constexpr int windowWidth = 560;
    constexpr int windowHeight = 475;

    const int virtualLeft =
        GetSystemMetrics(SM_XVIRTUALSCREEN);

    const int virtualTop =
        GetSystemMetrics(SM_YVIRTUALSCREEN);

    const int virtualWidth =
        GetSystemMetrics(SM_CXVIRTUALSCREEN);

    const int virtualHeight =
        GetSystemMetrics(SM_CYVIRTUALSCREEN);

    int x = captureRegion.left;

    int y =
        captureRegion.top - windowHeight - 12;

    if (y < virtualTop) {
        y = captureRegion.bottom + 12;
    }

    x = (std::max)(
        virtualLeft,
        (std::min)(
            x,
            virtualLeft + virtualWidth - windowWidth
        )
    );

    if (y + windowHeight > virtualTop + virtualHeight) {
        y = virtualTop + virtualHeight - windowHeight;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE,
        WINDOW_CLASS,
        L"Windows Scroll Capture",
        WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        &state
    );

    if (!hwnd) {
        return;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    MSG message{};

    int result = 0;

    while ((result = GetMessageW(
        &message,
        nullptr,
        0,
        0
    )) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
