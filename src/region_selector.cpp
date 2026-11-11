
#include "region_selector.hpp"

#include <windows.h>
#include <windowsx.h>
#include <algorithm>

namespace {

constexpr wchar_t WINDOW_CLASS[] =
    L"WindowsScrollCaptureRegionSelector";

struct SelectorState {
    POINT start{};
    POINT current{};

    RECT selected{};

    int virtualLeft = 0;
    int virtualTop = 0;

    bool dragging = false;
    bool completed = false;
};

SelectorState* getState(HWND hwnd) {
    return reinterpret_cast<SelectorState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA)
    );
}

RECT makeSelection(POINT a, POINT b) {
    return RECT{
        (std::min)(a.x, b.x),
        (std::min)(a.y, b.y),
        (std::max)(a.x, b.x),
        (std::max)(a.y, b.y)
    };
}

LRESULT CALLBACK selectorWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    SelectorState* state = getState(hwnd);

    switch (message) {

    case WM_NCCREATE: {
        auto* createInfo =
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

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN:
        if (!state) {
            return 0;
        }

        state->start = {
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        state->current = state->start;
        state->dragging = true;

        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);

        return 0;

    case WM_MOUSEMOVE:
        if (state && state->dragging) {
            state->current = {
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            InvalidateRect(hwnd, nullptr, FALSE);
        }

        return 0;

    case WM_LBUTTONUP:
        if (state && state->dragging) {
            state->current = {
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam)
            };

            state->dragging = false;

            ReleaseCapture();

            RECT selection =
                makeSelection(state->start, state->current);

            if (
                selection.right - selection.left >= 40 &&
                selection.bottom - selection.top >= 40
            ) {
                state->selected = {
                    selection.left + state->virtualLeft,
                    selection.top + state->virtualTop,
                    selection.right + state->virtualLeft,
                    selection.bottom + state->virtualTop
                };

                state->completed = true;
                DestroyWindow(hwnd);
            }

            InvalidateRect(hwnd, nullptr, FALSE);
        }

        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client{};
        GetClientRect(hwnd, &client);

        HBRUSH background =
            CreateSolidBrush(RGB(18, 22, 32));

        FillRect(hdc, &client, background);
        DeleteObject(background);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        const wchar_t* instruction =
            L"DRAG TO SELECT THE AREA TO CAPTURE";

        TextOutW(hdc, 30, 30, instruction,
            static_cast<int>(wcslen(instruction)));

        const wchar_t* escapeText =
            L"Press ESC to cancel";

        TextOutW(hdc, 30, 60, escapeText,
            static_cast<int>(wcslen(escapeText)));

        if (state && state->dragging) {
            RECT selection =
                makeSelection(state->start, state->current);

            HPEN pen = CreatePen(
                PS_SOLID,
                3,
                RGB(0, 190, 255)
            );

            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

            Rectangle(
                hdc,
                selection.left,
                selection.top,
                selection.right,
                selection.bottom
            );

            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);

            DeleteObject(pen);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

bool selectCaptureRegion(RECT& selectedRegion) {

    SelectorState state{};

    state.virtualLeft =
        GetSystemMetrics(SM_XVIRTUALSCREEN);

    state.virtualTop =
        GetSystemMetrics(SM_YVIRTUALSCREEN);

    const int width =
        GetSystemMetrics(SM_CXVIRTUALSCREEN);

    const int height =
        GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = selectorWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    windowClass.lpszClassName = WINDOW_CLASS;

    RegisterClassExW(&windowClass);

    HWND overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        WINDOW_CLASS,
        L"Windows Scroll Capture - Select Area",
        WS_POPUP,
        state.virtualLeft,
        state.virtualTop,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        &state
    );

    if (!overlay) {
        return false;
    }

    SetLayeredWindowAttributes(
        overlay,
        0,
        170,
        LWA_ALPHA
    );

    ShowWindow(overlay, SW_SHOW);
    UpdateWindow(overlay);
    SetForegroundWindow(overlay);

    MSG message{};

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (state.completed) {
        selectedRegion = state.selected;
        return true;
    }

    return false;
}
