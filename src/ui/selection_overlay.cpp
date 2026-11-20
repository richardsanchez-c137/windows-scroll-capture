
#include "ui/selection_overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <cstdlib>

#include <windowsx.h>

namespace windows_snip {

namespace {

constexpr wchar_t OVERLAY_CLASS_NAME[] =
    L"WindowsSnipTool.SelectionOverlay";

constexpr DWORD OVERLAY_ALPHA = 165;
constexpr DWORD SELECTION_BORDER_COLOR = 0xFF3291FF;
constexpr int BORDER_THICKNESS = 2;
constexpr int MIN_SELECTION_SIZE = 4;
constexpr int SELECTION_ESCAPE_HOTKEY_ID = 0x5342;

std::wstring makeWin32Error(const wchar_t* operation) {
    return std::wstring(operation) +
        L" failed. Win32 error: " +
        std::to_wstring(GetLastError());
}

struct WindowHitTestContext {
    POINT screenPosition{};
    DWORD currentProcessId = 0;
    HWND targetWindow = nullptr;
    RECT targetRect{};
};

BOOL CALLBACK EnumerateWindowAtPoint(
    HWND candidate,
    LPARAM parameter
) {
    auto* context =
        reinterpret_cast<WindowHitTestContext*>(parameter);

    if (!context || !IsWindowVisible(candidate)) {
        return TRUE;
    }

    if (candidate == GetDesktopWindow() ||
        candidate == GetShellWindow()) {
        return TRUE;
    }

    // Exclude our own application, including the selection overlay.
    DWORD processId = 0;

    GetWindowThreadProcessId(candidate, &processId);

    if (processId == context->currentProcessId) {
        return TRUE;
    }

    if (IsIconic(candidate)) {
        return TRUE;
    }

    RECT bounds{};

    if (!GetWindowRect(candidate, &bounds)) {
        return TRUE;
    }

    const POINT point = context->screenPosition;

    if (point.x < bounds.left ||
        point.x >= bounds.right ||
        point.y < bounds.top ||
        point.y >= bounds.bottom) {
        return TRUE;
    }

    context->targetWindow = candidate;
    context->targetRect = bounds;

    // Stop at the first eligible window in the Z-order.
    return FALSE;
}
} // namespace

SelectionOverlay::~SelectionOverlay() {
    Shutdown();
}

// ============================================================
// Window class
// ============================================================

bool SelectionOverlay::RegisterWindowClass(
    std::wstring& error
) {
    instance_ = GetModuleHandleW(nullptr);

    if (!instance_) {
        error = makeWin32Error(L"GetModuleHandleW");
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    windowClass.lpszClassName = OVERLAY_CLASS_NAME;

    if (!RegisterClassExW(&windowClass)) {
        const DWORD result = GetLastError();

        if (result != ERROR_CLASS_ALREADY_EXISTS) {
            error = makeWin32Error(L"RegisterClassExW");
            return false;
        }
    }

    return true;
}

// ============================================================
// Layered-window surface
// ============================================================

bool SelectionOverlay::CreateSurface(std::wstring& error) {
    error.clear();

    if (virtualWidth_ <= 0 || virtualHeight_ <= 0) {
        error = L"Invalid virtual desktop dimensions.";
        return false;
    }

    constexpr std::size_t MAX_PIXELS = 100'000'000;

    const auto width = static_cast<std::size_t>(virtualWidth_);
    const auto height = static_cast<std::size_t>(virtualHeight_);

    if (width > MAX_PIXELS / height ||
        width * height > MAX_PIXELS) {
        error = L"The virtual desktop is too large for the overlay.";
        return false;
    }

    memoryDc_ = CreateCompatibleDC(nullptr);

    if (!memoryDc_) {
        error = makeWin32Error(L"CreateCompatibleDC");
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = virtualWidth_;
    bitmapInfo.bmiHeader.biHeight = -virtualHeight_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    bitmap_ = CreateDIBSection(
        memoryDc_,
        &bitmapInfo,
        DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels_),
        nullptr,
        0
    );

    if (!bitmap_ || !pixels_) {
        error = makeWin32Error(L"CreateDIBSection");
        DestroySurface();
        return false;
    }

    previousBitmap_ = SelectObject(memoryDc_, bitmap_);

    if (!previousBitmap_ || previousBitmap_ == HGDI_ERROR) {
        error = makeWin32Error(L"SelectObject");
        previousBitmap_ = nullptr;
        DestroySurface();
        return false;
    }

    return true;
}

void SelectionOverlay::DestroySurface() noexcept {
    if (memoryDc_ && previousBitmap_ &&
        previousBitmap_ != HGDI_ERROR) {
        SelectObject(memoryDc_, previousBitmap_);
    }

    previousBitmap_ = nullptr;

    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    if (memoryDc_) {
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }

    pixels_ = nullptr;
}

// ============================================================
// Start and stop
// ============================================================

bool SelectionOverlay::Start(
    HWND owner,
    std::wstring& error,
    SelectionMode mode
) {
    error.clear();

    Shutdown();
    selectionMode_ = mode;

    hoveredWindow_ = nullptr;
    selectedWindow_ = nullptr;
    freeformScreenPoints_.clear();
    hoveredWindowRect_ = {};
    freeformScreenPoints_.clear();

    if (!owner || !IsWindow(owner)) {
        error = L"Invalid owner window.";
        return false;
    }

    owner_ = owner;

    // Obtain the entire virtual desktop, including
    // monitors positioned left or above the primary display.
    virtualOrigin_.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virtualOrigin_.y = GetSystemMetrics(SM_YVIRTUALSCREEN);

    virtualWidth_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualHeight_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (!RegisterWindowClass(error)) {
        Shutdown();
        return false;
    }

    if (!CreateSurface(error)) {
        Shutdown();
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_LAYERED |
        WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW,
        OVERLAY_CLASS_NAME,
        L"Screen Selection",
        WS_POPUP,
        virtualOrigin_.x,
        virtualOrigin_.y,
        virtualWidth_,
        virtualHeight_,
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (!window_) {
        error = makeWin32Error(L"CreateWindowExW");
        Shutdown();
        return false;
    }

    active_ = true;
    dragging_ = false;
    hasSelection_ = false;
    // Register Escape independently of keyboard focus.
    if (!RegisterHotKey(
        window_,
        SELECTION_ESCAPE_HOTKEY_ID,
        MOD_NOREPEAT,
        VK_ESCAPE
    )) {
        error = makeWin32Error(L"RegisterHotKey(Escape)");
        Shutdown();
        return false;
    }

    escapeHotKeyRegistered_ = true;
    selectionClient_ = {};
    selectionScreen_ = {};

    RenderOverlay();

    // Bring the overlay forward and allow it to receive
    // keyboard input for Escape cancellation.
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(window_);
    UpdateWindow(window_);

    return true;
}

void SelectionOverlay::Shutdown() noexcept {
    active_ = false;
    dragging_ = false;

    HWND oldWindow = window_;
    if (oldWindow && escapeHotKeyRegistered_) {
    UnregisterHotKey(
        oldWindow,
        SELECTION_ESCAPE_HOTKEY_ID
    );

    escapeHotKeyRegistered_ = false;
}

    if (oldWindow && GetCapture() == oldWindow) {
        ReleaseCapture();
    }

    if (oldWindow && IsWindow(oldWindow)) {
        DestroyWindow(oldWindow);
    }

    window_ = nullptr;
    owner_ = nullptr;
    instance_ = nullptr;

    DestroySurface();

    virtualOrigin_ = {};
    dragStart_ = {};
    virtualWidth_ = 0;
    virtualHeight_ = 0;

    selectionClient_ = {};
    selectionScreen_ = {};

    hasSelection_ = false;
}

bool SelectionOverlay::IsActive() const noexcept {
    return active_;
}

RECT SelectionOverlay::GetSelectionScreenRect() const noexcept {
    return selectionScreen_;
}
HWND SelectionOverlay::GetSelectedWindow() const noexcept {
    return selectedWindow_;
}
const std::vector<POINT>&
SelectionOverlay::GetFreeformSelectionScreenPoints() const noexcept {
    return freeformScreenPoints_;
}
// ============================================================
// Rendering
// ============================================================

void SelectionOverlay::RenderOverlay() {
    if (!window_ || !memoryDc_ || !pixels_) {
        return;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(virtualWidth_) *
        static_cast<std::size_t>(virtualHeight_);

    // A black layer with partial alpha dims the desktop.
    const DWORD dimColor = (OVERLAY_ALPHA << 24);

    std::fill_n(pixels_, pixelCount, dimColor);

    // Keep the selected area visually transparent while
    // retaining mouse hit-testing through the layered window.
    // Zero alpha would pass mouse messages to the window underneath.
    if (hasSelection_ &&
        selectionMode_ != SelectionMode::Freeform) {
        const int left = (std::max)(0L, selectionClient_.left);
        const int top = (std::max)(0L, selectionClient_.top);
        const int right = (std::min)(
            virtualWidth_,
            static_cast<int>(selectionClient_.right)
        );
        const int bottom = (std::min)(
            virtualHeight_,
            static_cast<int>(selectionClient_.bottom)
        );

        for (int y = top; y < bottom; ++y) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * virtualWidth_ + left;

            std::fill(
                pixels_ + offset,
                pixels_ + offset + (right - left),
                0x01000000
            );
        }

        // Draw a blue selection outline.
        auto setPixel = [&](int x, int y) {
            if (x >= 0 && x < virtualWidth_ &&
                y >= 0 && y < virtualHeight_) {
                pixels_[
                    static_cast<std::size_t>(y) * virtualWidth_ + x
                ] = SELECTION_BORDER_COLOR;
            }
        };

        for (int thickness = 0;
             thickness < BORDER_THICKNESS;
             ++thickness) {

            const int topY = top + thickness;
            const int bottomY = bottom - 1 - thickness;
            const int leftX = left + thickness;
            const int rightX = right - 1 - thickness;

            for (int x = left; x < right; ++x) {
                setPixel(x, topY);
                setPixel(x, bottomY);
            }

            for (int y = top; y < bottom; ++y) {
                setPixel(leftX, y);
                setPixel(rightX, y);
            }
        }
    }

    // Draw the freeform lasso boundary.
    if (selectionMode_ == SelectionMode::Freeform &&
        !freeformScreenPoints_.empty()) {

        auto setPixel = [&](int x, int y) {
            if (x >= 0 && x < virtualWidth_ &&
                y >= 0 && y < virtualHeight_) {

                pixels_[
                    static_cast<std::size_t>(y) * virtualWidth_ + x
                ] = SELECTION_BORDER_COLOR;
            }
        };

        auto drawLine = [&](POINT from, POINT to) {
            int x0 = from.x - virtualOrigin_.x;
            int y0 = from.y - virtualOrigin_.y;
            const int x1 = to.x - virtualOrigin_.x;
            const int y1 = to.y - virtualOrigin_.y;

            const int dx = std::abs(x1 - x0);
            const int sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0);
            const int sy = y0 < y1 ? 1 : -1;

            int error = dx + dy;

            while (true) {
                setPixel(x0, y0);
                setPixel(x0 + 1, y0);

                if (x0 == x1 && y0 == y1) {
                    break;
                }

                const int twiceError = 2 * error;

                if (twiceError >= dy) {
                    error += dy;
                    x0 += sx;
                }

                if (twiceError <= dx) {
                    error += dx;
                    y0 += sy;
                }
            }
        };

        for (std::size_t i = 1;
            i < freeformScreenPoints_.size();
            ++i) {

            drawLine(
                freeformScreenPoints_[i - 1],
                freeformScreenPoints_[i]
            );
        }

        // Close the lasso when the selection is complete.
        if (hasSelection_ && freeformScreenPoints_.size() >= 3) {
            drawLine(
                freeformScreenPoints_.back(),
                freeformScreenPoints_.front()
            );
        }
    }
    POINT destination{
        virtualOrigin_.x,
        virtualOrigin_.y
    };

    POINT source{ 0, 0 };

    SIZE size{
        virtualWidth_,
        virtualHeight_
    };

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDc = GetDC(nullptr);

    if (!screenDc) {
        OutputDebugStringW(
            L"Selection overlay: GetDC failed.\n"
        );
        return;
    }

    const BOOL updated = UpdateLayeredWindow(
        window_,
        screenDc,
        &destination,
        &size,
        memoryDc_,
        &source,
        0,
        &blend,
        ULW_ALPHA
    );

    ReleaseDC(nullptr, screenDc);

    if (!updated) {
        OutputDebugStringW(
            L"Selection overlay: UpdateLayeredWindow failed.\n"
        );
    }
}

// ============================================================
// Selection geometry
// ============================================================

void SelectionOverlay::UpdateSelection(
    POINT currentPosition
) {
    const LONG currentX = std::clamp<LONG>(
        currentPosition.x,
        0L,
        static_cast<LONG>(virtualWidth_)
    );

    const LONG currentY = std::clamp<LONG>(
        currentPosition.y,
        0L,
        static_cast<LONG>(virtualHeight_)
    );

    selectionClient_.left =
        (std::min)(dragStart_.x, currentX);

    selectionClient_.top =
        (std::min)(dragStart_.y, currentY);

    selectionClient_.right =
        (std::max)(dragStart_.x, currentX);

    selectionClient_.bottom =
        (std::max)(dragStart_.y, currentY);

    // Convert to exclusive right/bottom boundaries.
    if (selectionClient_.right < virtualWidth_) {
        ++selectionClient_.right;
    }

    if (selectionClient_.bottom < virtualHeight_) {
        ++selectionClient_.bottom;
    }

    // Convert virtual-desktop coordinates to screen coordinates.
    selectionScreen_.left =
        virtualOrigin_.x + selectionClient_.left;

    selectionScreen_.top =
        virtualOrigin_.y + selectionClient_.top;

    selectionScreen_.right =
        virtualOrigin_.x + selectionClient_.right;

    selectionScreen_.bottom =
        virtualOrigin_.y + selectionClient_.bottom;

    hasSelection_ = true;
}

void SelectionOverlay::UpdateFreeformSelection(
    POINT currentPosition
) {
    if (virtualWidth_ <= 0 || virtualHeight_ <= 0) {
        return;
    }

    const LONG x = std::clamp<LONG>(
        currentPosition.x,
        0,
        virtualWidth_ - 1
    );

    const LONG y = std::clamp<LONG>(
        currentPosition.y,
        0,
        virtualHeight_ - 1
    );

    const POINT screenPoint{
        virtualOrigin_.x + x,
        virtualOrigin_.y + y
    };

    // Avoid storing excessive points when the mouse barely moves.
    if (!freeformScreenPoints_.empty()) {
        const POINT& previous = freeformScreenPoints_.back();

        const long long dx =
            static_cast<long long>(screenPoint.x) - previous.x;

        const long long dy =
            static_cast<long long>(screenPoint.y) - previous.y;

        if (dx * dx + dy * dy < 4LL) {
            return;
        }
    }

    freeformScreenPoints_.push_back(screenPoint);

    LONG minX = screenPoint.x;
    LONG minY = screenPoint.y;
    LONG maxX = screenPoint.x;
    LONG maxY = screenPoint.y;

    for (const POINT& point : freeformScreenPoints_) {
        minX = (std::min)(minX, point.x);
        minY = (std::min)(minY, point.y);
        maxX = (std::max)(maxX, point.x);
        maxY = (std::max)(maxY, point.y);
    }

    selectionScreen_ = {
        minX,
        minY,
        maxX + 1,
        maxY + 1
    };

    selectionClient_ = {
        minX - virtualOrigin_.x,
        minY - virtualOrigin_.y,
        maxX - virtualOrigin_.x + 1,
        maxY - virtualOrigin_.y + 1
    };

    hasSelection_ = freeformScreenPoints_.size() >= 3;
}

HWND SelectionOverlay::FindWindowAtPoint(
    POINT screenPosition,
    RECT& windowRect
) const noexcept {

    windowRect = {};

    WindowHitTestContext context{};

    context.screenPosition = screenPosition;
    context.currentProcessId = GetCurrentProcessId();

    EnumWindows(
        EnumerateWindowAtPoint,
        reinterpret_cast<LPARAM>(&context)
    );

    if (!context.targetWindow) {
        return nullptr;
    }

    windowRect = context.targetRect;

    return context.targetWindow;
}


bool SelectionOverlay::UpdateHoveredWindow(
    POINT currentPosition
) {
    const POINT screenPosition{
        virtualOrigin_.x + currentPosition.x,
        virtualOrigin_.y + currentPosition.y
    };

    RECT targetRect{};

    HWND targetWindow = FindWindowAtPoint(
        screenPosition,
        targetRect
    );

    if (targetWindow == hoveredWindow_ &&
        EqualRect(&targetRect, &hoveredWindowRect_)) {

        return false;
    }

    hoveredWindow_ = targetWindow;
    hoveredWindowRect_ = targetRect;

    if (!targetWindow) {

        hasSelection_ = false;

        selectionClient_ = {};
        selectionScreen_ = {};

        return true;
    }

    // Clip the visible highlight to the virtual desktop.
    const LONG left = (std::max)(
        targetRect.left,
        virtualOrigin_.x
    );

    const LONG top = (std::max)(
        targetRect.top,
        virtualOrigin_.y
    );

    const LONG right = (std::min)(
        targetRect.right,
        virtualOrigin_.x + virtualWidth_
    );

    const LONG bottom = (std::min)(
        targetRect.bottom,
        virtualOrigin_.y + virtualHeight_
    );

    selectionClient_.left =
        left - virtualOrigin_.x;

    selectionClient_.top =
        top - virtualOrigin_.y;

    selectionClient_.right =
        right - virtualOrigin_.x;

    selectionClient_.bottom =
        bottom - virtualOrigin_.y;

    selectionScreen_ = targetRect;

    hasSelection_ =
        right > left &&
        bottom > top;

    return true;
}


// ============================================================
// Selection completion and cancellation
// ============================================================

void SelectionOverlay::CompleteSelection() {
    if (!active_ || !hasSelection_) {
        return;
    }

    dragging_ = false;

    HWND oldWindow = window_;
    HWND oldOwner = owner_;
        if (oldWindow && escapeHotKeyRegistered_) {
        UnregisterHotKey(
            oldWindow,
            SELECTION_ESCAPE_HOTKEY_ID
        );

        escapeHotKeyRegistered_ = false;
    }
    active_ = false;

    if (oldWindow && GetCapture() == oldWindow) {
        ReleaseCapture();
    }

    ShowWindow(oldWindow, SW_HIDE);

    if (oldOwner && IsWindow(oldOwner)) {
        PostMessageW(
            oldOwner,
            WM_APP_SELECTION_COMPLETED,
            0,
            0
        );
    }
}

void SelectionOverlay::CancelSelection() {
    if (!active_) {
        return;
    }

    dragging_ = false;
    active_ = false;
    selectionClient_ = {};
    selectionScreen_ = {};
    hasSelection_ = false;

    // Reset window-selection state.
    hoveredWindow_ = nullptr;
    selectedWindow_ = nullptr;
    hoveredWindowRect_ = {};

    HWND oldWindow = window_;
    HWND oldOwner = owner_;

    if (oldWindow && GetCapture() == oldWindow) {
        ReleaseCapture();
    }

    if (oldWindow) {
        ShowWindow(oldWindow, SW_HIDE);
    }

    if (oldOwner && IsWindow(oldOwner)) {
        PostMessageW(
            oldOwner,
            WM_APP_SELECTION_CANCELLED,
            0,
            0
        );
    }
}

// ============================================================
// Win32 window procedure
// ============================================================

LRESULT CALLBACK SelectionOverlay::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    SelectionOverlay* self =
        reinterpret_cast<SelectionOverlay*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
        );

    if (message == WM_NCCREATE) {
        const auto* createInfo =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        self = static_cast<SelectionOverlay*>(
            createInfo->lpCreateParams
        );

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self)
        );
    }

    if (self) {
        return self->HandleMessage(
            hwnd,
            message,
            wParam,
            lParam
        );
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

LRESULT SelectionOverlay::HandleMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {

    case WM_HOTKEY:

        if (wParam == SELECTION_ESCAPE_HOTKEY_ID) {
            CancelSelection();
            return 0;
        }

        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(
            LoadCursorW(
                nullptr,
                selectionMode_ == SelectionMode::Window
                    ? IDC_HAND
                    : IDC_CROSS
            )
        );
        return TRUE;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            CancelSelection();
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {

        if (!active_) {
            return 0;
        }

        POINT position{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        if (selectionMode_ == SelectionMode::Window) {

            UpdateHoveredWindow(position);

            dragging_ = true;
            SetCapture(hwnd);

            RenderOverlay();

            return 0;
        }

        if (selectionMode_ == SelectionMode::Freeform) {
            freeformScreenPoints_.clear();

            dragging_ = true;
            hasSelection_ = false;

            SetCapture(hwnd);

            UpdateFreeformSelection(position);
            RenderOverlay();

            return 0;
        }

        dragStart_ = position;

        dragging_ = true;
        hasSelection_ = true;

        SetCapture(hwnd);

        UpdateSelection(dragStart_);
        RenderOverlay();

        return 0;
    }

    case WM_MOUSEMOVE: {

        POINT position{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        if (selectionMode_ == SelectionMode::Window) {

            if (UpdateHoveredWindow(position)) {
                RenderOverlay();
            }

            return 0;
        }
        if (selectionMode_ == SelectionMode::Freeform) {
            if (dragging_) {
                UpdateFreeformSelection(position);
                RenderOverlay();
            }

            return 0;
        }

        if (dragging_) {
            UpdateSelection(position);
            RenderOverlay();
        }

        return 0;
    }

    case WM_LBUTTONUP: {

        if (!dragging_) {
            return 0;
        }

        POINT position{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        if (selectionMode_ == SelectionMode::Window) {

            UpdateHoveredWindow(position);

            dragging_ = false;

            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }

            if (hoveredWindow_ && hasSelection_) {

                selectedWindow_ = hoveredWindow_;

                CompleteSelection();
            }
            else {
                RenderOverlay();
            }

            return 0;
        }
        if (selectionMode_ == SelectionMode::Freeform) {
            UpdateFreeformSelection(position);

            dragging_ = false;

            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }

            const LONG width =
                selectionClient_.right - selectionClient_.left;

            const LONG height =
                selectionClient_.bottom - selectionClient_.top;

            if (freeformScreenPoints_.size() >= 3 &&
                width >= MIN_SELECTION_SIZE &&
                height >= MIN_SELECTION_SIZE) {

                hasSelection_ = true;
                RenderOverlay();
                CompleteSelection();
            }
            else {
                hasSelection_ = false;
                freeformScreenPoints_.clear();
                selectionClient_ = {};
                selectionScreen_ = {};

                RenderOverlay();
            }

            return 0;
        }

        UpdateSelection(position);

        dragging_ = false;

        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }

        const LONG width =
            selectionClient_.right - selectionClient_.left;

        const LONG height =
            selectionClient_.bottom - selectionClient_.top;

        if (width >= MIN_SELECTION_SIZE &&
            height >= MIN_SELECTION_SIZE) {

            CompleteSelection();
        }
        else {
            hasSelection_ = false;
            selectionClient_ = {};
            selectionScreen_ = {};

            RenderOverlay();
        }

        return 0;
    }

    case WM_CAPTURECHANGED:
        if (dragging_) {
            dragging_ = false;
            hasSelection_ = false;
            selectionClient_ = {};
            selectionScreen_ = {};
            RenderOverlay();
            freeformScreenPoints_.clear();
        }
        return 0;

    case WM_CLOSE:
        CancelSelection();
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);

        if (window_ == hwnd) {
            window_ = nullptr;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}

} // namespace windows_snip
