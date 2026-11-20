
#include "capture/window_capture.hpp"
#include "graphics/graphics_runtime.hpp"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>
#include <climits>
#include <exception>

namespace windows_snip {

namespace {

using CaptureItem =
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem;

using FramePool =
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;

using CaptureSession =
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession;

using CaptureFrame =
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;

using PixelFormat =
    winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

constexpr std::uint64_t MAX_OUTPUT_BYTES =
    512ULL * 1024ULL * 1024ULL;

constexpr auto FRAME_TIMEOUT =
    std::chrono::seconds(4);

std::wstring FormatHresult(
    const wchar_t* operation,
    HRESULT result
) {
    std::wostringstream stream;

    stream << operation
           << L" failed. HRESULT: 0x"
           << std::uppercase
           << std::hex
           << static_cast<unsigned long>(result);

    return stream.str();
}


struct ScopedTextureMap {
    ID3D11DeviceContext* context = nullptr;
    ID3D11Resource* resource = nullptr;

    ~ScopedTextureMap() {
        if (context && resource) {
            context->Unmap(resource, 0);
        }
    }
};


void CloseCapture(
    CaptureSession& session,
    FramePool& framePool
) noexcept {
    if (session) {
        try {
            session.Close();
        }
        catch (...) {
        }

        session = nullptr;
    }

    if (framePool) {
        try {
            framePool.Close();
        }
        catch (...) {
        }

        framePool = nullptr;
    }
}

} // namespace


bool WindowCapture::CaptureWindow(
    CaptureRuntime& runtime,
    GraphicsRuntime& graphics,
    HWND targetWindow,
    WindowCaptureResult& result,
    std::wstring& error
) {
    error.clear();
    result.Reset();

    if (!runtime.IsInitialized() ||
        !runtime.IsCaptureSupported()) {

        error = L"Windows Graphics Capture runtime is unavailable.";
        return false;
    }

    if (!graphics.IsInitialized() ||
        !graphics.GetWICFactory()) {

        error = L"Graphics runtime or WIC is not initialized.";
        return false;
    }

    if (!targetWindow || !IsWindow(targetWindow)) {
        error = L"The selected target window no longer exists.";
        return false;
    }

    if (!IsWindowVisible(targetWindow) ||
        IsIconic(targetWindow)) {

        error =
            L"The selected window must be visible and not minimized.";

        return false;
    }

    if (!GetWindowRect(targetWindow, &result.windowRect)) {
        error = L"Could not retrieve the selected window bounds.";
        return false;
    }

    CaptureItem item{ nullptr };
    FramePool framePool{ nullptr };
    CaptureSession session{ nullptr };
    CaptureFrame frame{ nullptr };

    try {

        // ----------------------------------------------------
        // 1. Create a GraphicsCaptureItem for this HWND.
        // ----------------------------------------------------

        auto interop =
            winrt::get_activation_factory<
                CaptureItem,
                IGraphicsCaptureItemInterop
            >();

        winrt::check_hresult(
            interop->CreateForWindow(
                targetWindow,
                winrt::guid_of<CaptureItem>(),
                winrt::put_abi(item)
            )
        );

        if (!item) {
            error =
                L"CreateForWindow returned an empty capture item.";

            return false;
        }

        const auto itemSize = item.Size();

        if (itemSize.Width <= 0 ||
            itemSize.Height <= 0) {

            error = L"The target window has invalid capture dimensions.";
            return false;
        }

        // ----------------------------------------------------
        // 2. Create a free-threaded frame pool.
        // ----------------------------------------------------

        constexpr int FRAME_BUFFER_COUNT = 2;

        framePool = FramePool::CreateFreeThreaded(
            runtime.GetWinRTDevice(),
            PixelFormat::B8G8R8A8UIntNormalized,
            FRAME_BUFFER_COUNT,
            itemSize
        );

        session = framePool.CreateCaptureSession(item);

        // ----------------------------------------------------
        // 3. Start capture and wait for the next frame.
        //
        // Polling here keeps all native D3D11 context operations
        // on this worker thread instead of using FrameArrived
        // concurrently on the frame pool's callback thread.
        // ----------------------------------------------------

        session.StartCapture();

        const auto deadline =
            std::chrono::steady_clock::now() + FRAME_TIMEOUT;

        while (std::chrono::steady_clock::now() < deadline) {

            frame = framePool.TryGetNextFrame();

            if (frame) {
                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10)
            );
        }

        if (!frame) {
            error = L"Timed out waiting for a window capture frame.";

            CloseCapture(session, framePool);

            return false;
        }

        // ----------------------------------------------------
        // 4. Get the native D3D11 frame texture.
        // ----------------------------------------------------

        auto surface = frame.Surface();

        auto surfaceAccess =
            surface.as<
                ::Windows::Graphics::DirectX::Direct3D11::
                IDirect3DDxgiInterfaceAccess
            >();

        Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;

        HRESULT hr = surfaceAccess->GetInterface(
            IID_PPV_ARGS(sourceTexture.GetAddressOf())
        );

        if (FAILED(hr)) {
            error = FormatHresult(
                L"GetInterface(ID3D11Texture2D)",
                hr
            );

            CloseCapture(session, framePool);

            return false;
        }

        D3D11_TEXTURE2D_DESC sourceDescription{};

        sourceTexture->GetDesc(&sourceDescription);

        if (sourceDescription.Format !=
            DXGI_FORMAT_B8G8R8A8_UNORM) {

            error = L"The captured window texture is not BGRA8.";

            CloseCapture(session, framePool);

            return false;
        }

        if (sourceDescription.SampleDesc.Count != 1) {
            error =
                L"Multisampled window capture textures are unsupported.";

            CloseCapture(session, framePool);

            return false;
        }

        const auto contentSize = frame.ContentSize();

        if (contentSize.Width <= 0 ||
            contentSize.Height <= 0 ||
            static_cast<UINT>(contentSize.Width) > sourceDescription.Width ||
            static_cast<UINT>(contentSize.Height) > sourceDescription.Height) {

            error = L"The captured window frame has invalid dimensions.";

            CloseCapture(session, framePool);

            return false;
        }

        const UINT width =
            static_cast<UINT>(contentSize.Width);

        const UINT height =
            static_cast<UINT>(contentSize.Height);

        const std::uint64_t stride64 =
            static_cast<std::uint64_t>(width) * 4ULL;

        const std::uint64_t bufferSize64 =
            stride64 * height;

        if (stride64 > UINT_MAX ||
            bufferSize64 > UINT_MAX ||
            bufferSize64 > MAX_OUTPUT_BYTES ||
            bufferSize64 > std::numeric_limits<std::size_t>::max()) {

            error = L"The captured window exceeds the image size limit.";

            CloseCapture(session, framePool);

            return false;
        }

        const UINT stride = static_cast<UINT>(stride64);
        const UINT bufferSize = static_cast<UINT>(bufferSize64);

        // ----------------------------------------------------
        // 5. Create a CPU-readable staging texture.
        // ----------------------------------------------------

        D3D11_TEXTURE2D_DESC stagingDescription =
            sourceDescription;

        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;

        hr = runtime.GetDevice()->CreateTexture2D(
            &stagingDescription,
            nullptr,
            stagingTexture.GetAddressOf()
        );

        if (FAILED(hr)) {
            error = FormatHresult(
                L"CreateTexture2D staging resource",
                hr
            );

            CloseCapture(session, framePool);

            return false;
        }

        runtime.GetContext()->CopyResource(
            stagingTexture.Get(),
            sourceTexture.Get()
        );

        // ----------------------------------------------------
        // 6. Map the staging texture and copy its pixels.
        // ----------------------------------------------------

        D3D11_MAPPED_SUBRESOURCE mapped{};

        hr = runtime.GetContext()->Map(
            stagingTexture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );

        if (FAILED(hr)) {
            error = FormatHresult(
                L"Map window staging texture",
                hr
            );

            CloseCapture(session, framePool);

            return false;
        }

        ScopedTextureMap mapGuard{
            runtime.GetContext(),
            stagingTexture.Get()
        };

        if (mapped.RowPitch < stride) {
            error = L"The captured texture has an invalid row pitch.";

            CloseCapture(session, framePool);

            return false;
        }

        std::vector<BYTE> pixels(bufferSize);

        const BYTE* sourcePixels =
            static_cast<const BYTE*>(mapped.pData);

        for (UINT row = 0; row < height; ++row) {

            const BYTE* sourceRow =
                sourcePixels +
                static_cast<std::size_t>(row) * mapped.RowPitch;

            BYTE* destinationRow =
                pixels.data() +
                static_cast<std::size_t>(row) * stride;

            std::memcpy(
                destinationRow,
                sourceRow,
                stride
            );
        }

        // MapGuard unmaps the staging texture automatically.
        // The frame pool can now be closed.

        frame.Close();
        frame = nullptr;

        CloseCapture(session, framePool);

        // ----------------------------------------------------
        // 7. Convert the captured pixels into a WIC bitmap.
        // ----------------------------------------------------

        hr = graphics.GetWICFactory()->CreateBitmapFromMemory(
            width,
            height,
            GUID_WICPixelFormat32bppBGRA,
            stride,
            bufferSize,
            pixels.data(),
            result.bitmap.GetAddressOf()
        );

        if (FAILED(hr)) {
            result.Reset();

            error = FormatHresult(
                L"WIC CreateBitmapFromMemory",
                hr
            );

            return false;
        }

        result.width = width;
        result.height = height;

        return true;
    }
    catch (const winrt::hresult_error& exception) {

        CloseCapture(session, framePool);

        result.Reset();

        const auto message = exception.message();

        error =
            L"Window Graphics Capture failed: " +
            std::wstring(message.c_str());

        return false;
    }
    catch (const std::exception&) {

        CloseCapture(session, framePool);

        result.Reset();

        error =
            L"A standard C++ exception occurred during window capture.";

        return false;
    }
    catch (...) {

        CloseCapture(session, framePool);

        result.Reset();

        error =
            L"An unexpected error occurred during window capture.";

        return false;
    }
}

} // namespace windows_snip
