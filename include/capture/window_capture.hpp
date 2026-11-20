
#pragma once

#include "capture/capture_runtime.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>

namespace windows_snip {

class GraphicsRuntime;

struct WindowCaptureResult {
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;

    UINT width = 0;
    UINT height = 0;

    RECT windowRect{};

    void Reset() noexcept {
        bitmap.Reset();
        width = 0;
        height = 0;
        windowRect = {};
    }
};

class WindowCapture final {
public:
    WindowCapture() = default;
    ~WindowCapture() = default;

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    bool CaptureWindow(
        CaptureRuntime& runtime,
        GraphicsRuntime& graphics,
        HWND targetWindow,
        WindowCaptureResult& result,
        std::wstring& error
    );
};

} // namespace windows_snip