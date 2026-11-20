
#include "capture/scroll_capture_worker.hpp"

#include "capture/capture_runtime.hpp"
#include "capture/scrollable_region_finder.hpp"
#include "capture/window_capture.hpp"
#include "graphics/graphics_runtime.hpp"
#include "image/wic_pixel_bridge.hpp"

#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace windows_snip {

namespace {

constexpr unsigned int MAX_CAPTURED_FRAMES = 250;

constexpr int FOREGROUND_SETTLE_TIME_MS = 250;
constexpr int SCROLL_SETTLE_TIME_MS = 700;
constexpr int WAIT_POLL_INTERVAL_MS = 25;

constexpr double SCROLL_END_PERCENT = 99.99;

void NotifyWindow(
    HWND window,
    UINT message,
    WPARAM value = 0
) noexcept {
    if (window && IsWindow(window)) {
        PostMessageW(window, message, value, 0);
    }
}


class WinrtApartment final {
public:
    bool Initialize(std::wstring& error) {
        try {
            winrt::init_apartment(
                winrt::apartment_type::multi_threaded
            );

            initialized_ = true;
            return true;
        }
        catch (const winrt::hresult_error& exception) {
            const auto message = exception.message();

            error =
                L"Could not initialize the worker COM apartment: ";

            error += message.c_str();

            return false;
        }
        catch (...) {
            error =
                L"Unexpected error initializing the worker COM apartment.";

            return false;
        }
    }

    ~WinrtApartment() {
        if (initialized_) {
            winrt::uninit_apartment();
        }
    }

private:
    bool initialized_ = false;
};


bool WaitWithCancellation(
    const std::atomic<bool>& cancelRequested,
    int durationMilliseconds
) {
    int elapsed = 0;

    while (elapsed < durationMilliseconds) {

        if (cancelRequested.load()) {
            return false;
        }

        const int interval = (std::min)(
            WAIT_POLL_INTERVAL_MS,
            durationMilliseconds - elapsed
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(interval)
        );

        elapsed += interval;
    }

    return !cancelRequested.load();
}


// ------------------------------------------------------------
// Determine the visible client rectangle in screen coordinates.
// ------------------------------------------------------------

bool GetClientScreenRect(
    HWND window,
    RECT& result,
    std::wstring& error
) {
    result = {};

    RECT client{};

    if (!GetClientRect(window, &client)) {
        error = L"Could not retrieve the target client rectangle.";
        return false;
    }

    POINT topLeft{
        client.left,
        client.top
    };

    POINT bottomRight{
        client.right,
        client.bottom
    };

    if (!ClientToScreen(window, &topLeft) ||
        !ClientToScreen(window, &bottomRight)) {

        error =
            L"Could not convert client coordinates into screen coordinates.";

        return false;
    }

    result = {
        topLeft.x,
        topLeft.y,
        bottomRight.x,
        bottomRight.y
    };

    if (result.right <= result.left ||
        result.bottom <= result.top) {

        error = L"The target application has an empty client area.";
        return false;
    }

    return true;
}


// ------------------------------------------------------------
// Crop a captured window frame using screen-space coordinates.
//
// WindowCaptureResult::windowRect provides the screen rectangle
// associated with the captured bitmap. We scale screen coordinates
// to pixel coordinates because capture resolution can differ from
// the window's logical dimensions.
// ------------------------------------------------------------

bool CropFrameToScreenRect(
    const PixelFrame& source,
    const RECT& sourceScreenRect,
    const RECT& cropScreenRect,
    PixelFrame& output,
    std::wstring& error
) {
    output = {};
    error.clear();

    if (!source.IsValid()) {
        error = L"The captured window frame is invalid.";
        return false;
    }

    const LONG sourceWidth =
        sourceScreenRect.right - sourceScreenRect.left;

    const LONG sourceHeight =
        sourceScreenRect.bottom - sourceScreenRect.top;

    if (sourceWidth <= 0 || sourceHeight <= 0) {
        error = L"The captured window has invalid screen bounds.";
        return false;
    }

    if (cropScreenRect.left < sourceScreenRect.left ||
        cropScreenRect.top < sourceScreenRect.top ||
        cropScreenRect.right > sourceScreenRect.right ||
        cropScreenRect.bottom > sourceScreenRect.bottom ||
        cropScreenRect.right <= cropScreenRect.left ||
        cropScreenRect.bottom <= cropScreenRect.top) {

        error =
            L"The detected scrollable viewport is outside the captured "
            L"window bounds.";

        return false;
    }

    const double scaleX =
        static_cast<double>(source.width) / sourceWidth;

    const double scaleY =
        static_cast<double>(source.height) / sourceHeight;

    const auto clampPixel = [](double value, UINT maximum) {
        return static_cast<UINT>(
            (std::clamp)(
                value,
                0.0,
                static_cast<double>(maximum)
            )
        );
    };

    const UINT left = clampPixel(
        std::floor(
            (cropScreenRect.left - sourceScreenRect.left) * scaleX
        ),
        source.width
    );

    const UINT top = clampPixel(
        std::floor(
            (cropScreenRect.top - sourceScreenRect.top) * scaleY
        ),
        source.height
    );

    const UINT right = clampPixel(
        std::ceil(
            (cropScreenRect.right - sourceScreenRect.left) * scaleX
        ),
        source.width
    );

    const UINT bottom = clampPixel(
        std::ceil(
            (cropScreenRect.bottom - sourceScreenRect.top) * scaleY
        ),
        source.height
    );

    if (right <= left || bottom <= top) {
        error = L"The calculated viewport crop is empty.";
        return false;
    }

    const UINT cropWidth = right - left;
    const UINT cropHeight = bottom - top;

    const std::uint64_t stride64 =
        static_cast<std::uint64_t>(cropWidth) * 4ULL;

    const std::uint64_t bytes64 =
        stride64 * cropHeight;

    constexpr std::uint64_t MAX_FRAME_BYTES =
        256ULL * 1024ULL * 1024ULL;

    if (stride64 > std::numeric_limits<std::size_t>::max() ||
        bytes64 > MAX_FRAME_BYTES ||
        bytes64 > std::numeric_limits<std::size_t>::max()) {

        error = L"The cropped viewport exceeds the image memory limit.";
        return false;
    }

    PixelFrame cropped;

    cropped.width = cropWidth;
    cropped.height = cropHeight;
    cropped.stride = static_cast<std::size_t>(stride64);

    cropped.pixels.resize(
        static_cast<std::size_t>(bytes64)
    );

    const std::size_t rowBytes = cropped.stride;

    for (UINT row = 0; row < cropHeight; ++row) {

        const auto* sourceRow =
            source.pixels.data() +
            static_cast<std::size_t>(top + row) * source.stride +
            static_cast<std::size_t>(left) * 4;

        auto* destinationRow =
            cropped.pixels.data() +
            static_cast<std::size_t>(row) * cropped.stride;

        std::memcpy(
            destinationRow,
            sourceRow,
            rowBytes
        );
    }

    output = std::move(cropped);

    return true;
}


// ------------------------------------------------------------
// Capture one window, convert it to pixels and crop to viewport.
// ------------------------------------------------------------

bool CaptureScrollableFrame(
    WindowCapture& windowCapture,
    CaptureRuntime& captureRuntime,
    GraphicsRuntime& graphicsRuntime,
    HWND targetWindow,
    const RECT& scrollViewport,
    PixelFrame& output,
    std::wstring& error
) {
    WindowCaptureResult captured;

    if (!windowCapture.CaptureWindow(
        captureRuntime,
        graphicsRuntime,
        targetWindow,
        captured,
        error
    )) {
        return false;
    }

    if (!captured.bitmap) {
        error = L"Window capture produced an empty frame.";
        return false;
    }

    PixelFrame fullFrame;

    if (!WicBitmapToPixelFrame(
        captured.bitmap.Get(),
        graphicsRuntime.GetWICFactory(),
        fullFrame,
        error
    )) {
        return false;
    }

    return CropFrameToScreenRect(
        fullFrame,
        captured.windowRect,
        scrollViewport,
        output,
        error
    );
}

} // namespace


// ------------------------------------------------------------
// Construction and lifecycle
// ------------------------------------------------------------

ScrollCaptureWorker::~ScrollCaptureWorker() {
    Cancel();
    Join();
}


bool ScrollCaptureWorker::Start(
    HWND notificationWindow,
    HWND targetWindow,
    const RECT& targetWindowRect,
    std::wstring& error
) {
    error.clear();

    if (!notificationWindow ||
        !IsWindow(notificationWindow)) {

        error = L"Invalid scrolling notification window.";
        return false;
    }

    if (!targetWindow ||
        !IsWindow(targetWindow)) {

        error = L"Invalid scrolling target window.";
        return false;
    }

    if (targetWindowRect.right <= targetWindowRect.left ||
        targetWindowRect.bottom <= targetWindowRect.top) {

        error = L"Invalid selected window bounds.";
        return false;
    }

    if (state_.load() == State::Running) {
        error = L"A scrolling capture session is already running.";
        return false;
    }

    Join();

    {
        std::lock_guard lock(resultMutex_);

        result_ = {};
        error_.clear();
    }

    cancelRequested_.store(false);
    capturedFrameCount_.store(0);
    state_.store(State::Running);

    try {
        worker_ = std::thread(
            &ScrollCaptureWorker::Run,
            this,
            notificationWindow,
            targetWindow,
            targetWindowRect
        );

        return true;
    }
    catch (const std::exception&) {
        state_.store(State::Failed);

        error =
            L"Could not create the scrolling capture worker thread.";

        return false;
    }
    catch (...) {
        state_.store(State::Failed);

        error =
            L"Unexpected error creating the scrolling capture worker.";

        return false;
    }
}


void ScrollCaptureWorker::Cancel() noexcept {
    if (state_.load() == State::Running) {
        cancelRequested_.store(true);
    }
}


void ScrollCaptureWorker::Join() noexcept {
    if (!worker_.joinable()) {
        return;
    }

    if (worker_.get_id() == std::this_thread::get_id()) {
        return;
    }

    try {
        worker_.join();
    }
    catch (...) {
    }
}


bool ScrollCaptureWorker::IsRunning() const noexcept {
    return state_.load() == State::Running;
}


ScrollCaptureWorker::State
ScrollCaptureWorker::GetState() const noexcept {
    return state_.load();
}


unsigned int
ScrollCaptureWorker::GetCapturedFrameCount() const noexcept {
    return capturedFrameCount_.load();
}


// ------------------------------------------------------------
// Result retrieval
// ------------------------------------------------------------

bool ScrollCaptureWorker::TakeResult(
    PixelFrame& result,
    std::wstring& error
) {
    error.clear();
    result = {};

    const State currentState = state_.load();

    if (currentState != State::Completed &&
        currentState != State::Cancelled) {

        error = L"The scrolling capture has no completed result.";
        return false;
    }

    Join();

    std::lock_guard lock(resultMutex_);

    if (!result_.IsValid()) {
        error = L"No stitched image is available.";
        return false;
    }

    result = std::move(result_);

    return true;
}


bool ScrollCaptureWorker::GetError(
    std::wstring& error
) const {
    std::lock_guard lock(resultMutex_);

    error = error_;

    return !error.empty();
}


// ------------------------------------------------------------
// Internal result management
// ------------------------------------------------------------

void ScrollCaptureWorker::StoreFailure(
    const std::wstring& error
) noexcept {
    try {
        std::lock_guard lock(resultMutex_);
        error_ = error;
    }
    catch (...) {
    }

    state_.store(State::Failed);
}


void ScrollCaptureWorker::StoreResult(
    PixelFrame&& result
) {
    std::lock_guard lock(resultMutex_);
    result_ = std::move(result);
}


// ------------------------------------------------------------
// Worker implementation
// ------------------------------------------------------------

void ScrollCaptureWorker::Run(
    HWND notificationWindow,
    HWND targetWindow,
    RECT targetWindowRect
) noexcept {

    WinrtApartment apartment;

    CaptureRuntime captureRuntime;
    GraphicsRuntime graphicsRuntime;
    WindowCapture windowCapture;
    ScrollableRegionFinder regionFinder;

    auto fail = [&](const std::wstring& message) {

        regionFinder.Reset();

        captureRuntime.Shutdown();
        graphicsRuntime.Shutdown();

        StoreFailure(message);

        NotifyWindow(
            notificationWindow,
            WM_APP_SCROLL_FAILED
        );
    };

    try {

        std::wstring error;

        // ----------------------------------------------------
        // 1. Initialize the worker's independent COM apartment.
        // ----------------------------------------------------

        if (!apartment.Initialize(error)) {
            fail(error);
            return;
        }

        // ----------------------------------------------------
        // 2. Initialize independent graphics and capture devices.
        // ----------------------------------------------------

        if (!graphicsRuntime.Initialize(error)) {
            fail(error);
            return;
        }

        if (!captureRuntime.Initialize(error)) {
            fail(error);
            return;
        }

        if (!captureRuntime.IsCaptureSupported()) {
            fail(L"Windows Graphics Capture is not supported.");
            return;
        }

        // ----------------------------------------------------
        // 3. Validate and activate the selected window.
        // ----------------------------------------------------

        if (!IsWindow(targetWindow) ||
            !IsWindowVisible(targetWindow) ||
            IsIconic(targetWindow)) {

            fail(
                L"The selected application window is no longer visible."
            );

            return;
        }

        HWND rootWindow = GetAncestor(targetWindow, GA_ROOT);

        if (!rootWindow) {
            rootWindow = targetWindow;
        }

        RECT currentWindowRect{};

        if (!GetWindowRect(targetWindow, &currentWindowRect)) {
            fail(L"Could not retrieve the selected window bounds.");
            return;
        }

        RECT intersection{};

        if (!IntersectRect(
            &intersection,
            &targetWindowRect,
            &currentWindowRect
        )) {
            fail(
                L"The selected window moved before capture could begin."
            );

            return;
        }

        if (GetForegroundWindow() != rootWindow &&
            !SetForegroundWindow(rootWindow)) {

            fail(
                L"Windows prevented activation of the scrolling target. "
                L"Bring the target application to the foreground and retry."
            );

            return;
        }

        if (!WaitWithCancellation(
            cancelRequested_,
            FOREGROUND_SETTLE_TIME_MS
        )) {
            state_.store(State::Cancelled);

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_CANCELLED
            );

            return;
        }

        if (cancelRequested_.load()) {
            state_.store(State::Cancelled);

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_CANCELLED
            );

            return;
        }

        // ----------------------------------------------------
        // 4. Identify the actual scrollable content viewport.
        // ----------------------------------------------------

        RECT scrollViewport{};

        if (!regionFinder.Find(
            targetWindow,
            scrollViewport,
            error
        )) {

            fail(
                L"Could not identify the scrollable content area. "
                L"The selected application must expose a vertical "
                L"UI Automation ScrollPattern.\n\nDetails: " +
                error
            );

            return;
        }

        // Use UI Automation to return the document to its beginning.
        if (!regionFinder.ScrollToTop(error)) {
            fail(error);
            return;
        }

        if (!WaitWithCancellation(
            cancelRequested_,
            SCROLL_SETTLE_TIME_MS
        )) {
            state_.store(State::Cancelled);

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_CANCELLED
            );

            return;
        }

        double currentScrollPercent = 0.0;

        if (!regionFinder.GetVerticalScrollPercent(
            currentScrollPercent,
            error
        )) {
            fail(error);
            return;
        }

        // A vertically scrollable element should expose a valid
        // normalized position between 0 and 100.
        if (currentScrollPercent < 0.0 ||
            currentScrollPercent > 100.0) {

            fail(
                L"The target returned an invalid vertical scroll position."
            );

            return;
        }

        OutputDebugStringW(
            L"Scrollable viewport identified. Starting at document top.\n"
        );

        // ----------------------------------------------------
        // 5. Capture the initial viewport.
        // ----------------------------------------------------

        PixelFrame firstFrame;

        if (!CaptureScrollableFrame(
            windowCapture,
            captureRuntime,
            graphicsRuntime,
            targetWindow,
            scrollViewport,
            firstFrame,
            error
        )) {
            fail(error);
            return;
        }

        ImageStitcher stitcher;

        if (!stitcher.Begin(firstFrame, error)) {
            fail(error);
            return;
        }

        capturedFrameCount_.store(1);

        NotifyWindow(
            notificationWindow,
            WM_APP_SCROLL_PROGRESS,
            1
        );

        // ----------------------------------------------------
        // 6. Automatically scroll, capture and stitch.
        // ----------------------------------------------------

        while (!cancelRequested_.load()) {

            if (capturedFrameCount_.load() >= MAX_CAPTURED_FRAMES) {
                OutputDebugStringW(
                    L"Scrolling capture reached its frame safety limit.\n"
                );

                break;
            }

            if (!IsWindow(targetWindow) ||
                !IsWindowVisible(targetWindow) ||
                IsIconic(targetWindow)) {

                fail(
                    L"The target window was closed, hidden or minimized."
                );

                return;
            }

            const double previousScrollPercent =
                currentScrollPercent;

            // ScrollDown advances approximately 75% of the
            // visible viewport, retaining approximately 25% overlap.
            if (!regionFinder.ScrollDown(error)) {
                fail(error);
                return;
            }

            if (!WaitWithCancellation(
                cancelRequested_,
                SCROLL_SETTLE_TIME_MS
            )) {
                break;
            }

            double nextScrollPercent = 0.0;

            if (!regionFinder.GetVerticalScrollPercent(
                nextScrollPercent,
                error
            )) {
                fail(error);
                return;
            }

            // No actual scroll movement means the document has
            // reached the end (or the provider refused the operation).
            if (nextScrollPercent <= previousScrollPercent + 0.01) {
                break;
            }

            PixelFrame nextFrame;

            if (!CaptureScrollableFrame(
                windowCapture,
                captureRuntime,
                graphicsRuntime,
                targetWindow,
                scrollViewport,
                nextFrame,
                error
            )) {
                fail(error);
                return;
            }

            if (cancelRequested_.load()) {
                break;
            }

            const StitchAppendResult appendResult =
                stitcher.Append(nextFrame, error);

            if (appendResult == StitchAppendResult::NoMovement) {
                break;
            }

            if (appendResult == StitchAppendResult::NoOverlap) {
                fail(
                    error.empty()
                        ? L"Could not find reliable overlap between "
                          L"successive viewport captures."
                        : error
                );

                return;
            }

            if (appendResult == StitchAppendResult::Failed) {
                fail(error);
                return;
            }

            if (appendResult != StitchAppendResult::Appended) {
                fail(L"Unexpected image stitching result.");
                return;
            }

            currentScrollPercent = nextScrollPercent;

            const unsigned int newCount =
                capturedFrameCount_.fetch_add(1) + 1;

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_PROGRESS,
                newCount
            );

            // Capture the final viewport before stopping.
            if (currentScrollPercent >= SCROLL_END_PERCENT) {
                break;
            }
        }

        // ----------------------------------------------------
        // 7. Retrieve the completed or partial stitched image.
        // ----------------------------------------------------

        PixelFrame finalImage;

        if (!stitcher.GetResult(finalImage, error)) {
            fail(error);
            return;
        }

        regionFinder.Reset();

        captureRuntime.Shutdown();
        graphicsRuntime.Shutdown();

        StoreResult(std::move(finalImage));

        if (cancelRequested_.load()) {

            state_.store(State::Cancelled);

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_CANCELLED,
                capturedFrameCount_.load()
            );
        }
        else {

            state_.store(State::Completed);

            NotifyWindow(
                notificationWindow,
                WM_APP_SCROLL_COMPLETED,
                capturedFrameCount_.load()
            );
        }
    }
    catch (const winrt::hresult_error& exception) {

        const auto message = exception.message();

        fail(
            L"Windows Runtime error during scrolling capture: " +
            std::wstring(message.c_str())
        );
    }
    catch (const std::bad_alloc&) {

        fail(
            L"Not enough memory to complete the scrolling screenshot."
        );
    }
    catch (const std::exception&) {

        fail(
            L"A standard C++ exception occurred during scrolling capture."
        );
    }
    catch (...) {

        fail(
            L"An unexpected error occurred during scrolling capture."
        );
    }
}

} // namespace windows_snip