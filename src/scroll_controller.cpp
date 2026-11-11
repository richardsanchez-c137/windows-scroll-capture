
#include "scroll_controller.hpp"
#include "scroll_capture.hpp"

#include <windows.h>
#include <windowsx.h>
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

    RECT previewRect{20, 105, 540, 380};

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

HBRUSH getControllerBackgroundBrush() {
    static HBRUSH brush =
        CreateSolidBrush(RGB(24, 27, 34));

    return brush;
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

    if (!state->session->SaveStitchedPng(path, error, true)) {
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
            18, 58, 520, 38,
            hwnd, nullptr, instance, nullptr
        );

        state->captureButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Capture More",
            WS_CHILD | WS_VISIBLE |
                WS_TABSTOP | BS_OWNERDRAW,
            15, 360, 160, 44,
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
                WS_TABSTOP | BS_OWNERDRAW,
            190, 360, 160, 44,
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
                WS_TABSTOP | BS_OWNERDRAW,
            365, 360, 160, 44,
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

    case WM_CTLCOLORSTATIC: {
         HDC dc = reinterpret_cast<HDC>(wParam);

        SetTextColor(dc, RGB(226, 232, 240));
        SetBkColor(dc, RGB(24, 27, 34));
        SetBkMode(dc, OPAQUE);

        return reinterpret_cast<LRESULT>(
            getControllerBackgroundBrush()
        );
    }

    case WM_PAINT: {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client{};
GetClientRect(hwnd, &client);

RECT header{0, 0, client.right, 42};

HBRUSH headerBrush =
    CreateSolidBrush(RGB(31, 35, 44));

FillRect(hdc, &header, headerBrush);
DeleteObject(headerBrush);

SetBkMode(hdc, TRANSPARENT);
SetTextColor(hdc, RGB(235, 238, 245));

HFONT titleFont = CreateFontW(
    -17, 0, 0, 0, FW_SEMIBOLD,
    FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE,
    L"Segoe UI"
);

HGDIOBJ oldFont = SelectObject(hdc, titleFont);

TextOutW(
    hdc,
    18,
    12,
    L"Windows Scroll Capture",
    22
);

SelectObject(hdc, oldFont);
DeleteObject(titleFont);

// Draw the custom close icon.
HPEN closePen = CreatePen(
    PS_SOLID,
    2,
    RGB(190, 195, 205)
);

oldFont = SelectObject(hdc, closePen);

MoveToEx(hdc, client.right - 27, 14, nullptr);
LineTo(hdc, client.right - 15, 26);

MoveToEx(hdc, client.right - 15, 14, nullptr);
LineTo(hdc, client.right - 27, 26);

SelectObject(hdc, oldFont);
DeleteObject(closePen);

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

    case WM_NCHITTEST: {
    POINT point{
        GET_X_LPARAM(lParam),
        GET_Y_LPARAM(lParam)
    };

    ScreenToClient(hwnd, &point);

    RECT client{};
    GetClientRect(hwnd, &client);

    if (
        point.y >= 0 &&
        point.y < 42 &&
        point.x < client.right - 44
    ) {
        return HTCAPTION;
    }

    return HTCLIENT;
}

    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        RECT client{};
        GetClientRect(hwnd, &client);

        if (
            x >= client.right - 44 &&
            y >= 0 &&
            y < 42
        ) {
            KillTimer(hwnd, CAPTURE_SETTLE_TIMER);

            if (state) {
                state->session.reset();
            }

            DestroyWindow(hwnd);
            return 0;
        }

        break;
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

    case WM_DRAWITEM: {
        auto* item =
            reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        if (!item || item->CtlType != ODT_BUTTON) {
            return FALSE;
        }

        COLORREF fill{};
        const wchar_t* label = L"";

        switch (item->CtlID) {
        case ID_CAPTURE_MORE:
            fill = RGB(53, 120, 210);
            label = L"Capture More";
            break;

        case ID_FINISH_SAVE:
            fill = RGB(35, 145, 105);
            label = L"Finish && Save";
            break;

        case ID_CANCEL:
            fill = RGB(60, 67, 78);
            label = L"Cancel";
            break;

        default:
            return FALSE;
        }

        if (item->itemState & ODS_DISABLED) {
            fill = RGB(55, 60, 68);
        }
        else if (item->itemState & ODS_SELECTED) {
            fill = RGB(35, 45, 60);
        }

        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, fill);

        HGDIOBJ oldBrush =
            SelectObject(item->hDC, brush);

        HGDIOBJ oldPen =
            SelectObject(item->hDC, pen);

        const RECT& r = item->rcItem;

        RoundRect(
            item->hDC,
            r.left,
            r.top,
            r.right,
            r.bottom,
            10,
            10
        );

        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);

        DeleteObject(brush);
        DeleteObject(pen);

        SetBkMode(item->hDC, TRANSPARENT);

        SetTextColor(
            item->hDC,
            RGB(255, 255, 255)
        );

        RECT textRect = r;

        DrawTextW(
            item->hDC,
            label,
            -1,
            &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );

        if (item->itemState & ODS_FOCUS) {
            RECT focusRect = r;
            InflateRect(&focusRect, -5, -5);
            DrawFocusRect(item->hDC, &focusRect);
        }

        return TRUE;
    }

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

            std::wstring previewError;

            if (!refreshPreview(hwnd, state, previewError)) {
                setStatus(
                    state,
                    L"Frame captured, but preview refresh failed."
                );

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

    if (!refreshPreview(nullptr, &state, error)) {
        MessageBoxW(
            nullptr,
            error.c_str(),
            L"Windows Scroll Capture - Preview Error",
            MB_OK | MB_ICONWARNING
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
        getControllerBackgroundBrush();
    windowClass.lpszClassName = WINDOW_CLASS;

    if (
        !RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS
    ) {
        return;
    }

    constexpr int windowWidth = 560;
    constexpr int windowHeight = 455;

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
        WS_POPUP,
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
