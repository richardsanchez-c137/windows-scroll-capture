#pragma once

#include "capture/capture_runtime.hpp"

#include <windows.h>
#include <string>

namespace windows_snip {

class MonitorCapture {
public:
    MonitorCapture() = default;
    ~MonitorCapture() = default;

    MonitorCapture(const MonitorCapture&) = delete;
    MonitorCapture& operator=(const MonitorCapture&) = delete;

    // Capture one frame from the primary monitor.
    bool CapturePrimaryMonitorFrame(
        CaptureRuntime& runtime,
        UINT& frameWidth,
        UINT& frameHeight,
        std::wstring& error
    );
};

} // namespace windows_snip