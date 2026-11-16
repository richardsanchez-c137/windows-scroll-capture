#include "graphics/graphics_runtime.hpp"

#include <objbase.h>

#include <iomanip>
#include <sstream>

namespace windows_snip {

namespace {

std::wstring makeError(
    const wchar_t* component,
    HRESULT result
) {
    std::wostringstream stream;

    stream
        << component
        << L" initialization failed. HRESULT: 0x"
        << std::uppercase
        << std::hex
        << static_cast<unsigned long>(result);

    return stream.str();
}

} // namespace

bool GraphicsRuntime::Initialize(std::wstring& error) {
    error.clear();

    // Avoid recreating factories if already initialized.
    if (IsInitialized()) {
        return true;
    }

    Shutdown();

    // --------------------------------------------------------
    // 1. Initialize Direct2D
    // --------------------------------------------------------

    D2D1_FACTORY_OPTIONS d2dOptions{};

#ifdef _DEBUG
    d2dOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        &d2dOptions,
        reinterpret_cast<void**>(
            d2dFactory_.ReleaseAndGetAddressOf()
        )
    );

    if (FAILED(result)) {
        error = makeError(L"Direct2D", result);
        Shutdown();
        return false;
    }

    // --------------------------------------------------------
    // 2. Initialize DirectWrite
    // --------------------------------------------------------

    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(
            dwriteFactory_.ReleaseAndGetAddressOf()
        )
    );

    if (FAILED(result)) {
        error = makeError(L"DirectWrite", result);
        Shutdown();
        return false;
    }

    // --------------------------------------------------------
    // 3. Initialize Windows Imaging Component
    // --------------------------------------------------------

    result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(
            wicFactory_.ReleaseAndGetAddressOf()
        )
    );

    if (FAILED(result)) {
        error = makeError(
            L"Windows Imaging Component",
            result
        );

        Shutdown();
        return false;
    }

    return true;
}

void GraphicsRuntime::Shutdown() noexcept {
    wicFactory_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
}

bool GraphicsRuntime::IsInitialized() const noexcept {
    return
        d2dFactory_ &&
        dwriteFactory_ &&
        wicFactory_;
}

ID2D1Factory* GraphicsRuntime::GetD2DFactory() const noexcept {
    return d2dFactory_.Get();
}

IDWriteFactory* GraphicsRuntime::GetDWriteFactory() const noexcept {
    return dwriteFactory_.Get();
}

IWICImagingFactory* GraphicsRuntime::GetWICFactory() const noexcept {
    return wicFactory_.Get();
}

} // namespace windows_snip
