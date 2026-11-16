
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>

namespace windows_snip {

class GraphicsRuntime {
public:
    GraphicsRuntime() = default;
    ~GraphicsRuntime() = default;

    GraphicsRuntime(const GraphicsRuntime&) = delete;
    GraphicsRuntime& operator=(const GraphicsRuntime&) = delete;

    // Initialize Direct2D, DirectWrite and Windows Imaging Component.
    bool Initialize(std::wstring& error);

    // Release all graphics resources.
    void Shutdown() noexcept;

    // Check whether all graphics factories are initialized.
    bool IsInitialized() const noexcept;

    ID2D1Factory* GetD2DFactory() const noexcept;
    IDWriteFactory* GetDWriteFactory() const noexcept;
    IWICImagingFactory* GetWICFactory() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
};

} // namespace windows_snip