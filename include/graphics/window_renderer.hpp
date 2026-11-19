#pragma once

#include "graphics/graphics_runtime.hpp"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <wincodec.h>

#include <string>

namespace windows_snip {

class WindowRenderer {
public:
    WindowRenderer() = default;
    ~WindowRenderer() = default;

    WindowRenderer(const WindowRenderer&) = delete;
    WindowRenderer& operator=(const WindowRenderer&) = delete;

    bool Initialize(
        HWND window,
        GraphicsRuntime& graphics,
        std::wstring& error
    );

    void Render();
    void Resize(UINT width, UINT height);
    bool HandleMouseButtonUp(float x, float y);
    void Shutdown() noexcept;
    bool SetCapturedImage(
        IWICBitmap* bitmap,
        IWICImagingFactory* wicFactory,
        std::wstring& error
    );

    bool IsInitialized() const noexcept;

private:
    bool CreateDeviceResources(std::wstring& error);

    // Borrowed reference: GraphicsRuntime must outlive this renderer.
    ID2D1Factory* d2dFactory_ = nullptr;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;

    // UI brushes.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> buttonBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> separatorBrush_;

    // UI text formats.
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> messageTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> captionTextFormat_;

    // Original WIC image survives Direct2D target recreation.
    Microsoft::WRL::ComPtr<IWICBitmap> capturedImageSource_;

    // Direct2D representation used for rendering.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> capturedD2DBitmap_;

    HWND window_ = nullptr;

    bool videoModeSelected_ = false;
    bool captureModeMenuOpen_ = false;

    enum class CaptureMode {
        Rectangle,
        Window,
        FullScreen,
        Freeform,
        Scroll
    };

    CaptureMode captureMode_ = CaptureMode::Rectangle;
    bool delayMenuOpen_ = false;
    unsigned int captureDelaySeconds_ = 0;
};

} // namespace windows_snip