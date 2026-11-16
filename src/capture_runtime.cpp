#include "capture/capture_runtime.hpp"

#include <dxgi1_6.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <sstream>
#include <iomanip>

namespace windows_snip {

namespace {

std::wstring makeDeviceError(
    const wchar_t* driver,
    HRESULT result
) {
    std::wostringstream stream;

    stream << driver
           << L" Direct3D 11 device creation failed. HRESULT: 0x"
           << std::uppercase
           << std::hex
           << static_cast<unsigned long>(result);

    return stream.str();
}

// Create a Direct3D device using the requested driver.
HRESULT createDevice(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driverType,
    Microsoft::WRL::ComPtr<ID3D11Device>& device,
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context,
    D3D_FEATURE_LEVEL& featureLevel
) {
    constexpr UINT creationFlags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDevice(
        adapter,
        driverType,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.ReleaseAndGetAddressOf(),
        &featureLevel,
        context.ReleaseAndGetAddressOf()
    );

    // Older D3D11 runtimes may reject feature level 11_1.
    // Retry without it when that happens.
    if (result == E_INVALIDARG) {

        device.Reset();
        context.Reset();

        constexpr D3D_FEATURE_LEVEL compatibilityLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        result = D3D11CreateDevice(
            adapter,
            driverType,
            nullptr,
            creationFlags,
            compatibilityLevels,
            ARRAYSIZE(compatibilityLevels),
            D3D11_SDK_VERSION,
            device.ReleaseAndGetAddressOf(),
            &featureLevel,
            context.ReleaseAndGetAddressOf()
        );
    }

    return result;
}
using WinRTDirect3DDevice =
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

bool createWinRTDevice(
    ID3D11Device* nativeDevice,
    WinRTDirect3DDevice& winrtDevice,
    std::wstring& error
) {
    error.clear();
    winrtDevice = nullptr;

    if (!nativeDevice) {
        error = L"Cannot create WinRT device from a null Direct3D device.";
        return false;
    }

    // Obtain the DXGI interface from the Direct3D device.
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;

    HRESULT result = nativeDevice->QueryInterface(
        __uuidof(IDXGIDevice),
        reinterpret_cast<void**>(
            dxgiDevice.GetAddressOf()
        )
    );

    if (FAILED(result)) {
        std::wostringstream stream;

        stream << L"IDXGIDevice query failed. HRESULT: 0x"
               << std::uppercase
               << std::hex
               << static_cast<unsigned long>(result);

        error = stream.str();
        return false;
    }

    // Wrap the native DXGI device as a Windows Runtime device.
    result = ::CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.Get(),
        reinterpret_cast<::IInspectable**>(
            winrt::put_abi(winrtDevice)
        )
    );

    if (FAILED(result)) {
        std::wostringstream stream;

        stream << L"WinRT Direct3D device creation failed. HRESULT: 0x"
               << std::uppercase
               << std::hex
               << static_cast<unsigned long>(result);

        error = stream.str();
        return false;
    }

    return true;
}

} // namespace

bool CaptureRuntime::Initialize(std::wstring& error) {
    error.clear();

    if (IsInitialized()) {
        return true;
    }

    Shutdown();

    std::wstring bridgeError;

    auto attachWinRTDevice = [&]() -> bool {
        if (createWinRTDevice(
            device_.Get(),
            winrtDevice_,
            bridgeError
        )) {
            return true;
        }

        winrtDevice_ = nullptr;
        context_.Reset();
        device_.Reset();

        return false;
    };

    // --------------------------------------------------------
    // 1. Enumerate GPUs in high-performance preference order.
    // --------------------------------------------------------

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;

    HRESULT factoryResult = CreateDXGIFactory2(
        0,
        IID_PPV_ARGS(factory.GetAddressOf())
    );

    if (SUCCEEDED(factoryResult)) {

        for (UINT index = 0; ; ++index) {

            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

            const HRESULT enumResult =
                factory->EnumAdapterByGpuPreference(
                    index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(adapter.GetAddressOf())
                );

            if (enumResult == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            if (FAILED(enumResult)) {
                break;
            }

            DXGI_ADAPTER_DESC1 description{};

            if (FAILED(adapter->GetDesc1(&description))) {
                continue;
            }

            // Skip software adapters during hardware selection.
            if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }

            // When an explicit adapter is supplied, D3D_DRIVER_TYPE
            // must be UNKNOWN.
            const HRESULT deviceResult = createDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                device_,
                context_,
                featureLevel_
            );

            if (SUCCEEDED(deviceResult) && attachWinRTDevice()) {
                hardwareAccelerated_ = true;
                return true;
            }

            device_.Reset();
            context_.Reset();
        }
    }

    // --------------------------------------------------------
    // 2. Fall back to the default hardware adapter.
    // --------------------------------------------------------

    const HRESULT hardwareResult = createDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        device_,
        context_,
        featureLevel_
    );

    if (SUCCEEDED(hardwareResult) && attachWinRTDevice()) {
        hardwareAccelerated_ = true;
        return true;
    }

    device_.Reset();
    context_.Reset();

    // --------------------------------------------------------
    // 3. Fall back to the Windows software rasterizer (WARP).
    // --------------------------------------------------------

    const HRESULT warpResult = createDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        device_,
        context_,
        featureLevel_
    );

    if (SUCCEEDED(warpResult) && attachWinRTDevice()) {
        hardwareAccelerated_ = false;
        return true;
    }

    error =
        makeDeviceError(
            L"High-performance/default hardware",
            hardwareResult
        )
        + L"\n"
        + makeDeviceError(
            L"WARP fallback",
            warpResult
        );
        if (!bridgeError.empty()) {
            error += L"\n";
            error += bridgeError;
        }

    Shutdown();

    return false;
}

void CaptureRuntime::Shutdown() noexcept {
    winrtDevice_ = nullptr;

    context_.Reset();
    device_.Reset();

    featureLevel_ = D3D_FEATURE_LEVEL_9_1;
    hardwareAccelerated_ = false;
}

bool CaptureRuntime::IsInitialized() const noexcept {
    return
        device_ != nullptr &&
        context_ != nullptr &&
        winrtDevice_ != nullptr;
}

bool CaptureRuntime::IsHardwareAccelerated() const noexcept {
    return IsInitialized() && hardwareAccelerated_;
}

ID3D11Device* CaptureRuntime::GetDevice() const noexcept {
    return device_.Get();
}

ID3D11DeviceContext* CaptureRuntime::GetContext() const noexcept {
    return context_.Get();
}
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
CaptureRuntime::GetWinRTDevice() const {
    return winrtDevice_;
}
bool CaptureRuntime::IsCaptureSupported() const noexcept {
    if (!winrtDevice_) {
        return false;
    }

    try {
        return winrt::Windows::Graphics::Capture::
            GraphicsCaptureSession::IsSupported();
    }
    catch (...) {
        return false;
    }
}
std::wstring CaptureRuntime::GetAdapterDescription() const {
    if (!device_) {
        return L"";
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;

    HRESULT result = device_.As(&dxgiDevice);

    if (FAILED(result)) {
        return L"";
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;

    result = dxgiDevice->GetAdapter(
        adapter.GetAddressOf()
    );

    if (FAILED(result)) {
        return L"";
    }

    DXGI_ADAPTER_DESC description{};

    result = adapter->GetDesc(&description);

    if (FAILED(result)) {
        return L"";
    }

    return description.Description;
}

D3D_FEATURE_LEVEL CaptureRuntime::GetFeatureLevel() const noexcept {
    return featureLevel_;
}

} // namespace windows_snip
