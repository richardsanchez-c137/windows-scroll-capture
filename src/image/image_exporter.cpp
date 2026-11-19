
#include "image/image_exporter.hpp"

#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <climits>
#include <exception>

#include <wrl/client.h>

namespace windows_snip {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint64_t MAX_IMAGE_BYTES =
    512ULL * 1024ULL * 1024ULL;

std::wstring MakeHresultError(
    const wchar_t* operation,
    HRESULT result
) {
    wchar_t buffer[16]{};

    swprintf_s(
        buffer,
        L"0x%08X",
        static_cast<unsigned int>(result)
    );

    return std::wstring(operation) +
        L" failed. HRESULT: " + buffer;
}

std::wstring MakeWin32Error(const wchar_t* operation) {
    return std::wstring(operation) +
        L" failed. Win32 error: " +
        std::to_wstring(GetLastError());
}

bool CreateBgraConverter(
    IWICBitmap* bitmap,
    IWICImagingFactory* factory,
    ComPtr<IWICFormatConverter>& converter,
    UINT& width,
    UINT& height,
    std::wstring& error
) {
    if (!bitmap || !factory) {
        error = L"Bitmap or WIC factory is null.";
        return false;
    }

    HRESULT result = bitmap->GetSize(&width, &height);

    if (FAILED(result)) {
        error = MakeHresultError(L"Get bitmap dimensions", result);
        return false;
    }

    if (width == 0 || height == 0) {
        error = L"The image has invalid dimensions.";
        return false;
    }

    result = factory->CreateFormatConverter(
        converter.GetAddressOf()
    );

    if (FAILED(result)) {
        error = MakeHresultError(
            L"Create WIC format converter",
            result
        );
        return false;
    }

    result = converter->Initialize(
        bitmap,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );

    if (FAILED(result)) {
        error = MakeHresultError(
            L"Convert image to BGRA",
            result
        );
        return false;
    }

    return true;
}

} // namespace

// ============================================================
// Save PNG through the native Windows Save As dialog.
// ============================================================

ImageSaveResult ImageExporter::SavePng(
    HWND owner,
    IWICBitmap* bitmap,
    IWICImagingFactory* factory,
    std::wstring& error
) {
    error.clear();

    if (!bitmap || !factory) {
        error = L"Cannot save: image or WIC factory is unavailable.";
        return ImageSaveResult::Failed;
    }

    try {
        // ----------------------------------------------------
        // 1. Create the Windows Save As dialog.
        // ----------------------------------------------------

        ComPtr<IFileSaveDialog> dialog;

        HRESULT result = CoCreateInstance(
            CLSID_FileSaveDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(dialog.GetAddressOf())
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Create Save As dialog",
                result
            );
            return ImageSaveResult::Failed;
        }

        constexpr COMDLG_FILTERSPEC fileTypes[] = {
            { L"PNG Image", L"*.png" }
        };

        result = dialog->SetFileTypes(
            ARRAYSIZE(fileTypes),
            fileTypes
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Configure Save As file types",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = dialog->SetFileTypeIndex(1);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Set PNG file type",
                result
            );
            return ImageSaveResult::Failed;
        }

        dialog->SetDefaultExtension(L"png");
        dialog->SetFileName(L"Screenshot.png");

        DWORD options = 0;

        result = dialog->GetOptions(&options);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Get Save As dialog options",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = dialog->SetOptions(
            options |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST |
            FOS_OVERWRITEPROMPT |
            FOS_STRICTFILETYPES
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Set Save As dialog options",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = dialog->Show(owner);

        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return ImageSaveResult::Cancelled;
        }

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Display Save As dialog",
                result
            );
            return ImageSaveResult::Failed;
        }

        // ----------------------------------------------------
        // 2. Obtain the selected file path.
        // ----------------------------------------------------

        ComPtr<IShellItem> selectedItem;

        result = dialog->GetResult(
            selectedItem.GetAddressOf()
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Get selected save location",
                result
            );
            return ImageSaveResult::Failed;
        }

        PWSTR filePath = nullptr;

        result = selectedItem->GetDisplayName(
            SIGDN_FILESYSPATH,
            &filePath
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Get selected file path",
                result
            );
            return ImageSaveResult::Failed;
        }

        // ----------------------------------------------------
        // 3. Create a WIC PNG encoder.
        // ----------------------------------------------------

        ComPtr<IWICStream> stream;

        result = factory->CreateStream(
            stream.GetAddressOf()
        );

        if (FAILED(result)) {
            CoTaskMemFree(filePath);

            error = MakeHresultError(
                L"Create WIC output stream",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = stream->InitializeFromFilename(
            filePath,
            GENERIC_WRITE
        );

        CoTaskMemFree(filePath);
        filePath = nullptr;

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Initialize PNG output stream",
                result
            );
            return ImageSaveResult::Failed;
        }

        ComPtr<IWICBitmapEncoder> encoder;

        result = factory->CreateEncoder(
            GUID_ContainerFormatPng,
            nullptr,
            encoder.GetAddressOf()
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Create PNG encoder",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = encoder->Initialize(
            stream.Get(),
            WICBitmapEncoderNoCache
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Initialize PNG encoder",
                result
            );
            return ImageSaveResult::Failed;
        }

        // ----------------------------------------------------
        // 4. Create a frame and negotiate its pixel format.
        // ----------------------------------------------------

        ComPtr<IWICBitmapFrameEncode> frame;

        result = encoder->CreateNewFrame(
            frame.GetAddressOf(),
            nullptr
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Create PNG frame",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = frame->Initialize(nullptr);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Initialize PNG frame",
                result
            );
            return ImageSaveResult::Failed;
        }

        UINT width = 0;
        UINT height = 0;

        result = bitmap->GetSize(&width, &height);

        if (FAILED(result) || width == 0 || height == 0) {
            error = FAILED(result)
                ? MakeHresultError(L"Get image dimensions", result)
                : L"Cannot save an image with zero dimensions.";

            return ImageSaveResult::Failed;
        }

        result = frame->SetSize(width, height);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Set PNG dimensions",
                result
            );
            return ImageSaveResult::Failed;
        }

        GUID outputPixelFormat = GUID_WICPixelFormat32bppBGRA;

        result = frame->SetPixelFormat(&outputPixelFormat);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Negotiate PNG pixel format",
                result
            );
            return ImageSaveResult::Failed;
        }

        // Convert to the format requested by the encoder.
        ComPtr<IWICFormatConverter> converter;

        result = factory->CreateFormatConverter(
            converter.GetAddressOf()
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Create PNG pixel converter",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = converter->Initialize(
            bitmap,
            outputPixelFormat,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Convert image for PNG encoding",
                result
            );
            return ImageSaveResult::Failed;
        }

        // ----------------------------------------------------
        // 5. Encode and commit the PNG.
        // ----------------------------------------------------

        result = frame->WriteSource(
            converter.Get(),
            nullptr
        );

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Write PNG image data",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = frame->Commit();

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Commit PNG frame",
                result
            );
            return ImageSaveResult::Failed;
        }

        result = encoder->Commit();

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Commit PNG file",
                result
            );
            return ImageSaveResult::Failed;
        }

        return ImageSaveResult::Success;
    }
    catch (const std::exception&) {
        error =
            L"PNG export failed because of a standard C++ exception.";

        return ImageSaveResult::Failed;
    }
    catch (...) {
        error = L"An unexpected error occurred while saving the PNG.";
        return ImageSaveResult::Failed;
    }
}

// ============================================================
// Copy image to the Windows clipboard as CF_DIBV5.
// ============================================================

bool ImageExporter::CopyToClipboard(
    HWND owner,
    IWICBitmap* bitmap,
    IWICImagingFactory* factory,
    std::wstring& error
) {
    error.clear();

    if (!bitmap || !factory) {
        error = L"Cannot copy: image or WIC factory is unavailable.";
        return false;
    }

    try {
        // ----------------------------------------------------
        // 1. Convert the image to straight-alpha BGRA pixels.
        // ----------------------------------------------------

        ComPtr<IWICFormatConverter> converter;

        UINT width = 0;
        UINT height = 0;

        if (!CreateBgraConverter(
            bitmap,
            factory,
            converter,
            width,
            height,
            error
        )) {
            return false;
        }

        if (width > static_cast<UINT>(LONG_MAX) ||
            height > static_cast<UINT>(LONG_MAX)) {
            error = L"Image dimensions exceed clipboard limits.";
            return false;
        }

        const std::uint64_t stride64 =
            static_cast<std::uint64_t>(width) * 4ULL;

        const std::uint64_t pixelBytes64 =
            stride64 * static_cast<std::uint64_t>(height);

        const std::uint64_t totalBytes64 =
            sizeof(BITMAPV5HEADER) + pixelBytes64;

        if (stride64 > UINT_MAX ||
            pixelBytes64 > UINT_MAX ||
            totalBytes64 > MAX_IMAGE_BYTES ||
            totalBytes64 > (std::numeric_limits<SIZE_T>::max)()) {
            error = L"Image is too large to copy to the clipboard.";
            return false;
        }

        const UINT stride = static_cast<UINT>(stride64);
        const UINT pixelBytes = static_cast<UINT>(pixelBytes64);
        const SIZE_T totalBytes = static_cast<SIZE_T>(totalBytes64);

        // ----------------------------------------------------
        // 2. Build a top-down 32-bit DIBV5 header.
        // ----------------------------------------------------

        BITMAPV5HEADER header{};

        header.bV5Size = sizeof(BITMAPV5HEADER);
        header.bV5Width = static_cast<LONG>(width);
        header.bV5Height = -static_cast<LONG>(height);
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5SizeImage = pixelBytes;

        // Little-endian BGRA channel masks.
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;

        header.bV5CSType = LCS_sRGB;
        header.bV5Intent = LCS_GM_IMAGES;

        // ----------------------------------------------------
        // 3. Allocate movable global clipboard memory.
        // ----------------------------------------------------

        HGLOBAL globalMemory = GlobalAlloc(
            GMEM_MOVEABLE | GMEM_ZEROINIT,
            totalBytes
        );

        if (!globalMemory) {
            error = MakeWin32Error(L"GlobalAlloc for clipboard image");
            return false;
        }

        void* lockedMemory = GlobalLock(globalMemory);

        if (!lockedMemory) {
            error = MakeWin32Error(L"GlobalLock for clipboard image");
            GlobalFree(globalMemory);
            return false;
        }

        std::memcpy(
            lockedMemory,
            &header,
            sizeof(header)
        );

        BYTE* pixelDestination =
            static_cast<BYTE*>(lockedMemory) + sizeof(header);

        HRESULT result = converter->CopyPixels(
            nullptr,
            stride,
            pixelBytes,
            pixelDestination
        );

        GlobalUnlock(globalMemory);

        if (FAILED(result)) {
            error = MakeHresultError(
                L"Copy image pixels for clipboard",
                result
            );

            GlobalFree(globalMemory);
            return false;
        }

        // ----------------------------------------------------
        // 4. Transfer ownership to the Windows clipboard.
        // ----------------------------------------------------

        if (!OpenClipboard(owner)) {
            error = MakeWin32Error(L"OpenClipboard");
            GlobalFree(globalMemory);
            return false;
        }

        if (!EmptyClipboard()) {
            error = MakeWin32Error(L"EmptyClipboard");
            CloseClipboard();
            GlobalFree(globalMemory);
            return false;
        }

        HANDLE clipboardResult = SetClipboardData(
            CF_DIBV5,
            globalMemory
        );

        if (!clipboardResult) {
            error = MakeWin32Error(L"SetClipboardData");
            CloseClipboard();
            GlobalFree(globalMemory);
            return false;
        }

        // Windows owns globalMemory after successful SetClipboardData.
        globalMemory = nullptr;

        if (!CloseClipboard()) {
            error = MakeWin32Error(L"CloseClipboard");
            return false;
        }

        return true;
    }
    catch (...) {
        error = L"An unexpected error occurred while copying the image.";
        return false;
    }
}

} // namespace windows_snip
