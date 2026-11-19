
#include "capture/monitor_capture.hpp"
#include "graphics/graphics_runtime.hpp"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace windows_snip {

namespace {

using CaptureItem =
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem;

using FramePool =
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;

using CaptureSession =
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession;

using PixelFormat =
    winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

struct CapturedFrameState {
    std::mutex mutex;
    std::condition_variable condition;

    bool processing = false;
    bool finished = false;

    std::wstring error;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    D3D11_TEXTURE2D_DESC textureDescription{};
};

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

void SignalCaptureError(
    const std::shared_ptr<CapturedFrameState>& state,
    const std::wstring& message
) noexcept {
    try {
        {
            std::lock_guard lock(state->mutex);

            if (state->finished) {
                return;
            }

            state->processing = false;
            state->finished = true;
            state->error = message;
        }

        state->condition.notify_one();
    }
    catch (...) {
        state->condition.notify_one();
    }
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

} // namespace

bool MonitorCapture::CaptureMonitorRegion(
    CaptureRuntime& runtime,
    GraphicsRuntime& graphics,
    const RECT& screenRect,
    MonitorCaptureResult& result,
    std::wstring& error
) {
    error.clear();
    result.Reset();

    // --------------------------------------------------------
    // 1. Validate the capture runtimes and requested rectangle.
    // --------------------------------------------------------

    if (!runtime.IsInitialized()) {
        error = L"Capture runtime is not initialized.";
        return false;
    }

    if (!runtime.IsCaptureSupported()) {
        error = L"Windows Graphics Capture is not supported.";
        return false;
    }

    if (!graphics.IsInitialized() || !graphics.GetWICFactory()) {
        error = L"Graphics runtime or WIC is not initialized.";
        return false;
    }

    if (screenRect.right <= screenRect.left ||
        screenRect.bottom <= screenRect.top) {
        error = L"Invalid screenshot selection rectangle.";
        return false;
    }

    // --------------------------------------------------------
    // 2. Identify the monitor containing the complete selection.
    // --------------------------------------------------------

    HMONITOR monitor = MonitorFromRect(
        &screenRect,
        MONITOR_DEFAULTTONULL
    );

    if (!monitor) {
        error = L"The selected region does not intersect a monitor.";
        return false;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        error = L"Could not retrieve monitor information.";
        return false;
    }

    const RECT monitorRect = monitorInfo.rcMonitor;

    // This first implementation deliberately rejects selections
    // that cross display boundaries.
    if (screenRect.left < monitorRect.left ||
        screenRect.top < monitorRect.top ||
        screenRect.right > monitorRect.right ||
        screenRect.bottom > monitorRect.bottom) {

        error =
            L"Please select a region contained entirely within one monitor.";

        return false;
    }

    const int monitorWidth =
        monitorRect.right - monitorRect.left;

    const int monitorHeight =
        monitorRect.bottom - monitorRect.top;

    if (monitorWidth <= 0 || monitorHeight <= 0) {
        error = L"Invalid monitor dimensions.";
        return false;
    }

    // --------------------------------------------------------
    // Capture resources.
    // --------------------------------------------------------

    FramePool framePool{ nullptr };
    CaptureSession session{ nullptr };

    winrt::event_token frameArrivedToken{};
    bool handlerRegistered = false;

    auto closeCapture = [&]() noexcept {

        if (handlerRegistered && framePool) {
            try {
                framePool.FrameArrived(frameArrivedToken);
            }
            catch (...) {
            }

            handlerRegistered = false;
        }

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
    };

    try {

        // ----------------------------------------------------
        // 3. Create GraphicsCaptureItem for the selected monitor.
        // ----------------------------------------------------

        auto captureInterop =
            winrt::get_activation_factory<
                CaptureItem,
                IGraphicsCaptureItemInterop
            >();

        CaptureItem item{ nullptr };

        winrt::check_hresult(
            captureInterop->CreateForMonitor(
                monitor,
                winrt::guid_of<CaptureItem>(),
                winrt::put_abi(item)
            )
        );

        if (!item) {
            error = L"CreateForMonitor returned an empty capture item.";
            return false;
        }

        const auto itemSize = item.Size();

        if (itemSize.Width <= 0 || itemSize.Height <= 0) {
            error = L"The capture item has invalid dimensions.";
            return false;
        }

        // ----------------------------------------------------
        // 4. Create a free-threaded Direct3D frame pool.
        // ----------------------------------------------------

        constexpr int FRAME_BUFFER_COUNT = 2;

        framePool = FramePool::CreateFreeThreaded(
            runtime.GetWinRTDevice(),
            PixelFormat::B8G8R8A8UIntNormalized,
            FRAME_BUFFER_COUNT,
            itemSize
        );

        auto state = std::make_shared<CapturedFrameState>();

        ID3D11Device* device = runtime.GetDevice();
        ID3D11DeviceContext* context = runtime.GetContext();

        // ----------------------------------------------------
        // 5. Receive the first frame through FrameArrived.
        // ----------------------------------------------------

        frameArrivedToken = framePool.FrameArrived(
            [state, device, context](
                const FramePool& sender,
                const winrt::Windows::Foundation::IInspectable&
            ) noexcept {

                try {

                    {
                        std::lock_guard lock(state->mutex);

                        if (state->finished || state->processing) {
                            return;
                        }

                        state->processing = true;
                    }

                    auto frame = sender.TryGetNextFrame();

                    if (!frame) {
                        std::lock_guard lock(state->mutex);
                        state->processing = false;
                        return;
                    }

                    // Obtain the native D3D11 texture underlying
                    // the Windows Runtime capture surface.
                    auto surface = frame.Surface();

                    auto surfaceAccess =
                        surface.as<
                            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess
                        >();

                    Microsoft::WRL::ComPtr<ID3D11Texture2D>
                        sourceTexture;

                    HRESULT hr = surfaceAccess->GetInterface(
                        IID_PPV_ARGS(sourceTexture.GetAddressOf())
                    );

                    if (FAILED(hr)) {
                        SignalCaptureError(
                            state,
                            FormatHresult(
                                L"GetInterface(ID3D11Texture2D)",
                                hr
                            )
                        );

                        return;
                    }

                    D3D11_TEXTURE2D_DESC sourceDescription{};
                    sourceTexture->GetDesc(&sourceDescription);

                    if (sourceDescription.Width == 0 ||
                        sourceDescription.Height == 0) {

                        SignalCaptureError(
                            state,
                            L"The captured texture has invalid dimensions."
                        );

                        return;
                    }

                    if (sourceDescription.Format !=
                        DXGI_FORMAT_B8G8R8A8_UNORM) {

                        SignalCaptureError(
                            state,
                            L"The captured texture format is not BGRA8."
                        );

                        return;
                    }

                    if (sourceDescription.SampleDesc.Count != 1) {
                        SignalCaptureError(
                            state,
                            L"Multisampled capture textures are not supported."
                        );

                        return;
                    }

                    // A staging texture is required because the original
                    // GPU texture cannot be directly CPU-mapped.
                    D3D11_TEXTURE2D_DESC stagingDescription =
                        sourceDescription;

                    stagingDescription.Usage = D3D11_USAGE_STAGING;
                    stagingDescription.BindFlags = 0;
                    stagingDescription.CPUAccessFlags =
                        D3D11_CPU_ACCESS_READ;
                    stagingDescription.MiscFlags = 0;

                    Microsoft::WRL::ComPtr<ID3D11Texture2D>
                        stagingTexture;

                    hr = device->CreateTexture2D(
                        &stagingDescription,
                        nullptr,
                        stagingTexture.GetAddressOf()
                    );

                    if (FAILED(hr)) {
                        SignalCaptureError(
                            state,
                            FormatHresult(
                                L"CreateTexture2D staging resource",
                                hr
                            )
                        );

                        return;
                    }

                    // Copy the captured GPU texture into CPU-readable memory.
                    context->CopyResource(
                        stagingTexture.Get(),
                        sourceTexture.Get()
                    );

                    {
                        std::lock_guard lock(state->mutex);

                        if (!state->finished) {
                            state->textureDescription =
                                sourceDescription;

                            state->stagingTexture =
                                std::move(stagingTexture);

                            state->processing = false;
                            state->finished = true;
                        }
                    }

                    state->condition.notify_one();

                    // The frame is released when this callback exits.
                }
                catch (const winrt::hresult_error& exception) {

                    const auto detail = exception.message();

                    SignalCaptureError(
                        state,
                        L"Frame acquisition failed: " +
                            std::wstring(detail.c_str(), detail.size())
                    );
                }
                catch (...) {

                    SignalCaptureError(
                        state,
                        L"Unexpected error while processing the capture frame."
                    );
                }
            }
        );

        handlerRegistered = true;

        // ----------------------------------------------------
        // 6. Start the capture session.
        // ----------------------------------------------------

        session = framePool.CreateCaptureSession(item);
        session.StartCapture();

        // Wait for the worker-thread callback.
        // Do not use the UI thread's dispatcher for this event.
        {
            std::unique_lock lock(state->mutex);

            const bool receivedFrame =
                state->condition.wait_for(
                    lock,
                    std::chrono::seconds(4),
                    [&]() {
                        return state->finished;
                    }
                );

            if (!receivedFrame) {
                error =
                    L"Timed out waiting for the first captured frame.";

                lock.unlock();
                closeCapture();
                return false;
            }

            if (!state->error.empty()) {
                error = state->error;

                lock.unlock();
                closeCapture();
                return false;
            }
        }

        // Release the capture session and frame pool.
        // The independent staging texture remains valid.
        closeCapture();

        // ----------------------------------------------------
        // 7. Retrieve the CPU-readable staging texture.
        // ----------------------------------------------------

        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;

        D3D11_TEXTURE2D_DESC textureDescription{};

        {
            std::lock_guard lock(state->mutex);

            stagingTexture = state->stagingTexture;
            textureDescription = state->textureDescription;
        }

        if (!stagingTexture) {
            error = L"No valid staging texture was received.";
            return false;
        }

        // ----------------------------------------------------
        // 8. Convert monitor coordinates to texture coordinates.
        // ----------------------------------------------------

        const double scaleX =
            static_cast<double>(textureDescription.Width) /
            static_cast<double>(monitorWidth);

        const double scaleY =
            static_cast<double>(textureDescription.Height) /
            static_cast<double>(monitorHeight);

        const int sourceLeft = std::clamp(
            static_cast<int>(std::floor(
                (screenRect.left - monitorRect.left) * scaleX
            )),
            0,
            static_cast<int>(textureDescription.Width)
        );

        const int sourceTop = std::clamp(
            static_cast<int>(std::floor(
                (screenRect.top - monitorRect.top) * scaleY
            )),
            0,
            static_cast<int>(textureDescription.Height)
        );

        const int sourceRight = std::clamp(
            static_cast<int>(std::ceil(
                (screenRect.right - monitorRect.left) * scaleX
            )),
            0,
            static_cast<int>(textureDescription.Width)
        );

        const int sourceBottom = std::clamp(
            static_cast<int>(std::ceil(
                (screenRect.bottom - monitorRect.top) * scaleY
            )),
            0,
            static_cast<int>(textureDescription.Height)
        );

        const UINT cropWidth =
            static_cast<UINT>(sourceRight - sourceLeft);

        const UINT cropHeight =
            static_cast<UINT>(sourceBottom - sourceTop);

        if (cropWidth == 0 || cropHeight == 0) {
            error = L"The calculated screenshot dimensions are empty.";
            return false;
        }

        // Keep the first implementation within a predictable memory limit.
        constexpr std::uint64_t MAX_OUTPUT_BYTES =
            512ULL * 1024ULL * 1024ULL;

        const std::uint64_t stride64 =
            static_cast<std::uint64_t>(cropWidth) * 4ULL;

        const std::uint64_t bufferSize64 =
            stride64 * static_cast<std::uint64_t>(cropHeight);

        if (stride64 > UINT_MAX ||
            bufferSize64 > UINT_MAX ||
            bufferSize64 > MAX_OUTPUT_BYTES) {

            error = L"The selected screenshot exceeds the image size limit.";
            return false;
        }

        const UINT stride = static_cast<UINT>(stride64);
        const UINT bufferSize = static_cast<UINT>(bufferSize64);

        // ----------------------------------------------------
        // 9. Map the staging texture and copy the selected pixels.
        // ----------------------------------------------------

        D3D11_MAPPED_SUBRESOURCE mapped{};

        HRESULT hr = context->Map(
            stagingTexture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );

        if (FAILED(hr)) {
            error = FormatHresult(
                L"Map staging texture",
                hr
            );

            return false;
        }

        ScopedTextureMap mapGuard{
            context,
            stagingTexture.Get()
        };

        std::vector<BYTE> pixelData(bufferSize);

        const BYTE* sourcePixels =
            static_cast<const BYTE*>(mapped.pData);

        for (UINT row = 0; row < cropHeight; ++row) {

            const BYTE* sourceRow =
                sourcePixels +
                static_cast<std::size_t>(sourceTop + row) *
                    mapped.RowPitch +
                static_cast<std::size_t>(sourceLeft) * 4;

            BYTE* destinationRow =
                pixelData.data() +
                static_cast<std::size_t>(row) * stride;

            std::memcpy(
                destinationRow,
                sourceRow,
                stride
            );
        }

        // mapGuard unmaps the texture automatically.

        // ----------------------------------------------------
        // 10. Create the final WIC bitmap.
        // ----------------------------------------------------

        hr = graphics.GetWICFactory()->CreateBitmapFromMemory(
            cropWidth,
            cropHeight,
            GUID_WICPixelFormat32bppBGRA,
            stride,
            bufferSize,
            pixelData.data(),
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

        result.width = cropWidth;
        result.height = cropHeight;
        result.screenRect = screenRect;

        return true;
    }
    catch (const winrt::hresult_error& exception) {

        closeCapture();

        result.Reset();

        error =
            L"Windows Graphics Capture failed: " +
            exception.message();

        return false;
    }
    catch (const std::exception&) {

        closeCapture();

        result.Reset();

        error =
            L"A standard C++ exception occurred during screenshot capture.";

        return false;
    }
    catch (...) {

        closeCapture();

        result.Reset();

        error =
            L"An unexpected error occurred during screenshot capture.";

        return false;
    }
}

} // namespace windows_snip
