
#pragma once

#include "capture/capture_runtime.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>

namespace windows_snip {

class GraphicsRuntime;

// Holds the result of a successful monitor-region capture.
struct MonitorCaptureResult {
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;

    UINT width = 0;
    UINT height = 0;

    RECT screenRect{};

    void Reset() noexcept {
        bitmap.Reset();
        width = 0;
        height = 0;
        screenRect = {};
    }
};

class MonitorCapture final {
public:
    MonitorCapture() = default;
    ~MonitorCapture() = default;

    MonitorCapture(const MonitorCapture&) = delete;
    MonitorCapture& operator=(const MonitorCapture&) = delete;

    // Capture a selected rectangle from the monitor containing it.
    // The rectangle uses virtual-screen coordinates.
    bool CaptureMonitorRegion(
        CaptureRuntime& runtime,
        GraphicsRuntime& graphics,
        const RECT& screenRect,
        MonitorCaptureResult& result,
        std::wstring& error
    );
};

} // namespace windows_snip