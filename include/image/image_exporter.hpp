
#pragma once

#include <windows.h>
#include <wincodec.h>

#include <string>

namespace windows_snip {

enum class ImageSaveResult {
    Success,
    Cancelled,
    Failed
};

class ImageExporter final {
public:
    ImageExporter() = default;
    ~ImageExporter() = default;

    ImageExporter(const ImageExporter&) = delete;
    ImageExporter& operator=(const ImageExporter&) = delete;

    ImageSaveResult SavePng(
        HWND owner,
        IWICBitmap* bitmap,
        IWICImagingFactory* factory,
        std::wstring& error
    );

    bool CopyToClipboard(
        HWND owner,
        IWICBitmap* bitmap,
        IWICImagingFactory* factory,
        std::wstring& error
    );
};

} // namespace windows_snip
