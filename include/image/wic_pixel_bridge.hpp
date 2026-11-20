#pragma once

#include "image/image_stitcher.hpp"

#include <wincodec.h>
#include <wrl/client.h>
#include <windows.h>
#include <vector>

#include <string>

namespace windows_snip {

bool WicBitmapToPixelFrame(
    IWICBitmap* source,
    IWICImagingFactory* factory,
    PixelFrame& output,
    std::wstring& error
);

bool PixelFrameToWicBitmap(
    IWICImagingFactory* factory,
    const PixelFrame& frame,
    Microsoft::WRL::ComPtr<IWICBitmap>& output,
    std::wstring& error
);

bool ApplyFreeformAlphaMask(
    IWICBitmap* source,
    IWICImagingFactory* factory,
    const std::vector<POINT>& screenPoints,
    const RECT& selectionScreenRect,
    Microsoft::WRL::ComPtr<IWICBitmap>& output,
    std::wstring& error
);

} // namespace windows_snip