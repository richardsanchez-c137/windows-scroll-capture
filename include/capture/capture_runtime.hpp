#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <string>

namespace windows_snip {

class CaptureRuntime {
public:
    CaptureRuntime() = default;
    ~CaptureRuntime() = default;

    CaptureRuntime(const CaptureRuntime&) = delete;
    CaptureRuntime& operator=(const CaptureRuntime&) = delete;

    // Initialize the Direct3D 11 capture device.
    bool Initialize(std::wstring& error);

    // Release all capture resources.
    void Shutdown() noexcept;

    // Check whether the capture device is initialized.
    bool IsInitialized() const noexcept;
    bool IsHardwareAccelerated() const noexcept;

    // Access the native Direct3D device.
    ID3D11Device* GetDevice() const noexcept;

    // Access the Windows Runtime Direct3D device.
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
    GetWinRTDevice() const;

    // Check whether Windows Graphics Capture is supported.
    bool IsCaptureSupported() const noexcept;

    // Access the immediate device context.
    ID3D11DeviceContext* GetContext() const noexcept;

    // Get the name of the GPU backing this Direct3D device.
    std::wstring GetAdapterDescription() const;

    // Get the supported Direct3D feature level.
    D3D_FEATURE_LEVEL GetFeatureLevel() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;

    // Windows Runtime representation of the Direct3D device.
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
    winrtDevice_{ nullptr };

    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_9_1;
    bool hardwareAccelerated_ = false;
};

} // namespace windows_snip