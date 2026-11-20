
#pragma once

#include "image/image_stitcher.hpp"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace windows_snip {

// Worker-to-UI notification messages.
inline constexpr UINT WM_APP_SCROLL_PROGRESS =
    WM_APP + 20;

inline constexpr UINT WM_APP_SCROLL_COMPLETED =
    WM_APP + 21;

inline constexpr UINT WM_APP_SCROLL_FAILED =
    WM_APP + 22;

inline constexpr UINT WM_APP_SCROLL_CANCELLED =
    WM_APP + 23;

class ScrollCaptureWorker final {
public:

    enum class State {
        Idle,
        Running,
        Completed,
        Cancelled,
        Failed
    };

    ScrollCaptureWorker() = default;

    ~ScrollCaptureWorker();

    ScrollCaptureWorker(const ScrollCaptureWorker&) = delete;
    ScrollCaptureWorker& operator=(const ScrollCaptureWorker&) = delete;

    // Start a scrolling capture session.
    //
    // notificationWindow receives WM_APP_SCROLL_* messages.
    // targetWindow identifies the application underneath
    // the selected scrolling region.
    // viewport uses virtual-screen coordinates.
    bool Start(
        HWND notificationWindow,
        HWND targetWindow,
        const RECT& targetWindowRect,
        std::wstring& error
    );

    // Request cancellation.
    // A partial image is retained when possible.
    void Cancel() noexcept;

    // Wait for the worker to finish.
    void Join() noexcept;

    bool IsRunning() const noexcept;

    State GetState() const noexcept;

    // Transfer ownership of the completed pixel image.
    bool TakeResult(
        PixelFrame& result,
        std::wstring& error
    );

    // Retrieve the worker error.
    bool GetError(std::wstring& error) const;

    // Number of successfully captured viewport frames.
    unsigned int GetCapturedFrameCount() const noexcept;

private:

    void Run(
        HWND notificationWindow,
        HWND targetWindow,
        RECT targetWindowRect
    ) noexcept;

    void StoreFailure(const std::wstring& error) noexcept;

    void StoreResult(PixelFrame&& result);

    std::atomic<State> state_{ State::Idle };

    std::atomic<bool> cancelRequested_{ false };

    std::atomic<unsigned int> capturedFrameCount_{ 0 };

    std::thread worker_;

    mutable std::mutex resultMutex_;

    PixelFrame result_;

    std::wstring error_;
};

} // namespace windows_snip
