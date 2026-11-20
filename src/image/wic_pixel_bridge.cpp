
#include "image/wic_pixel_bridge.hpp"

#include <limits>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <vector>

namespace windows_snip {

namespace {

constexpr std::uint64_t MAX_IMAGE_BYTES =
    512ULL * 1024ULL * 1024ULL;

bool ValidateDimensions(
    UINT width,
    UINT height,
    UINT& stride,
    UINT& bufferSize,
    std::wstring& error
) {
    if (width == 0 || height == 0) {
        error = L"Image dimensions cannot be zero.";
        return false;
    }

    const std::uint64_t stride64 =
        static_cast<std::uint64_t>(width) * 4ULL;

    const std::uint64_t buffer64 =
        stride64 * height;

    if (stride64 > UINT_MAX ||
        buffer64 > UINT_MAX ||
        buffer64 > MAX_IMAGE_BYTES ||
        buffer64 > std::numeric_limits<std::size_t>::max()) {

        error = L"Image exceeds the supported memory limit.";
        return false;
    }

    stride = static_cast<UINT>(stride64);
    bufferSize = static_cast<UINT>(buffer64);

    return true;
}

} // namespace


bool WicBitmapToPixelFrame(
    IWICBitmap* source,
    IWICImagingFactory* factory,
    PixelFrame& output,
    std::wstring& error
) {
    error.clear();
    output = {};

    if (!source || !factory) {
        error = L"Invalid WIC bitmap or imaging factory.";
        return false;
    }

    try {
        UINT width = 0;
        UINT height = 0;

        HRESULT hr = source->GetSize(&width, &height);

        if (FAILED(hr)) {
            error = L"Could not retrieve WIC bitmap dimensions.";
            return false;
        }

        UINT stride = 0;
        UINT bufferSize = 0;

        if (!ValidateDimensions(
            width,
            height,
            stride,
            bufferSize,
            error
        )) {
            return false;
        }

        // Normalize the source to 32-bit BGRA.
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

        hr = factory->CreateFormatConverter(
            converter.GetAddressOf()
        );

        if (FAILED(hr)) {
            error = L"Could not create WIC format converter.";
            return false;
        }

        hr = converter->Initialize(
            source,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );

        if (FAILED(hr)) {
            error = L"Could not convert image to BGRA format.";
            return false;
        }

        PixelFrame result;

        result.width = width;
        result.height = height;
        result.stride = stride;

        result.pixels.resize(bufferSize);

        hr = converter->CopyPixels(
            nullptr,
            stride,
            bufferSize,
            result.pixels.data()
        );

        if (FAILED(hr)) {
            error = L"Could not copy WIC pixels into memory.";
            return false;
        }

        output = std::move(result);

        return true;
    }
    catch (const std::bad_alloc&) {
        error = L"Not enough memory to convert the WIC bitmap.";
        return false;
    }
    catch (...) {
        error = L"Unexpected error converting WIC bitmap.";
        return false;
    }
}


bool PixelFrameToWicBitmap(
    IWICImagingFactory* factory,
    const PixelFrame& frame,
    Microsoft::WRL::ComPtr<IWICBitmap>& output,
    std::wstring& error
) {
    error.clear();
    output.Reset();

    if (!factory || !frame.IsValid()) {
        error = L"Invalid imaging factory or pixel frame.";
        return false;
    }

    UINT stride = 0;
    UINT bufferSize = 0;

    if (!ValidateDimensions(
        frame.width,
        frame.height,
        stride,
        bufferSize,
        error
    )) {
        return false;
    }

    if (frame.stride != stride ||
        frame.pixels.size() < bufferSize) {

        error = L"Pixel frame must contain tightly packed BGRA data.";
        return false;
    }

    const HRESULT hr = factory->CreateBitmapFromMemory(
        frame.width,
        frame.height,
        GUID_WICPixelFormat32bppBGRA,
        stride,
        bufferSize,
        const_cast<BYTE*>(frame.pixels.data()),
        output.GetAddressOf()
    );

    if (FAILED(hr)) {
        output.Reset();
        error = L"Could not create WIC bitmap from stitched pixels.";
        return false;
    }

    return true;
}

bool ApplyFreeformAlphaMask(
    IWICBitmap* source,
    IWICImagingFactory* factory,
    const std::vector<POINT>& screenPoints,
    const RECT& selectionScreenRect,
    Microsoft::WRL::ComPtr<IWICBitmap>& output,
    std::wstring& error
) {
    error.clear();
    output.Reset();

    if (!source || !factory) {
        error = L"Invalid bitmap or WIC factory for freeform masking.";
        return false;
    }

    if (screenPoints.size() < 3) {
        error = L"Freeform selection requires at least three points.";
        return false;
    }

    const LONG rectWidth =
        selectionScreenRect.right - selectionScreenRect.left;

    const LONG rectHeight =
        selectionScreenRect.bottom - selectionScreenRect.top;

    if (rectWidth <= 0 || rectHeight <= 0) {
        error = L"Invalid freeform selection bounds.";
        return false;
    }

    try {
        PixelFrame frame;

        if (!WicBitmapToPixelFrame(
            source,
            factory,
            frame,
            error
        )) {
            return false;
        }

        struct PointD {
            double x;
            double y;
        };

        // Convert screen-space lasso coordinates into
        // the captured bitmap's pixel coordinate system.
        const double scaleX =
            static_cast<double>(frame.width) / rectWidth;

        const double scaleY =
            static_cast<double>(frame.height) / rectHeight;

        std::vector<PointD> polygon;
        polygon.reserve(screenPoints.size());

        for (const POINT& point : screenPoints) {
            polygon.push_back({
                (point.x - selectionScreenRect.left) * scaleX,
                (point.y - selectionScreenRect.top) * scaleY
            });
        }

        // Allocate only one temporary coverage row, rather
        // than another full-size image buffer.
        std::vector<std::uint16_t> rowCoverage(frame.width, 0);
        std::vector<double> intersections;
        intersections.reserve(polygon.size());

        constexpr int VERTICAL_SAMPLES = 4;

        // Rasterize the polygon using the even-odd fill rule.
        for (UINT y = 0; y < frame.height; ++y) {
            std::fill(
                rowCoverage.begin(),
                rowCoverage.end(),
                0
            );

            for (int sample = 0;
                 sample < VERTICAL_SAMPLES;
                 ++sample) {

                const double sampleY =
                    y + (sample + 0.5) / VERTICAL_SAMPLES;

                intersections.clear();

                for (std::size_t i = 0;
                     i < polygon.size();
                     ++i) {

                    const PointD& a = polygon[i];
                    const PointD& b =
                        polygon[(i + 1) % polygon.size()];

                    const bool crosses =
                        (a.y <= sampleY && b.y > sampleY) ||
                        (b.y <= sampleY && a.y > sampleY);

                    if (!crosses) {
                        continue;
                    }

                    const double intersectionX =
                        a.x +
                        (sampleY - a.y) *
                        (b.x - a.x) /
                        (b.y - a.y);

                    intersections.push_back(intersectionX);
                }

                std::sort(
                    intersections.begin(),
                    intersections.end()
                );

                // Fill pairs of intersections.
                for (std::size_t i = 0;
                     i + 1 < intersections.size();
                     i += 2) {

                    const double left = std::clamp(
                        intersections[i],
                        0.0,
                        static_cast<double>(frame.width)
                    );

                    const double right = std::clamp(
                        intersections[i + 1],
                        0.0,
                        static_cast<double>(frame.width)
                    );

                    if (right <= left) {
                        continue;
                    }

                    const UINT firstX =
                        static_cast<UINT>(std::floor(left));

                    const UINT endX =
                        static_cast<UINT>(std::ceil(right));

                    for (UINT x = firstX; x < endX; ++x) {
                        const double overlap =
                            (std::min)(
                                right,
                                static_cast<double>(x + 1)
                            ) -
                            (std::max)(
                                left,
                                static_cast<double>(x)
                            );

                        if (overlap <= 0.0) {
                            continue;
                        }

                        const auto contribution =
                            static_cast<std::uint16_t>(
                                std::lround(overlap * 255.0)
                            );

                        rowCoverage[x] += contribution;
                    }
                }
            }

            // Write the resulting straight-alpha BGRA pixels.
            for (UINT x = 0; x < frame.width; ++x) {
                const unsigned int alpha =
                    (std::min)(
                        255U,
                        (
                            static_cast<unsigned int>(rowCoverage[x]) + 2U
                        ) / VERTICAL_SAMPLES
                    );

                const std::size_t offset =
                    static_cast<std::size_t>(y) * frame.stride +
                    static_cast<std::size_t>(x) * 4;

                if (alpha == 0) {
                    // Avoid retaining hidden RGB values outside
                    // the selected polygon.
                    frame.pixels[offset + 0] = 0;
                    frame.pixels[offset + 1] = 0;
                    frame.pixels[offset + 2] = 0;
                }

                frame.pixels[offset + 3] =
                    static_cast<BYTE>(alpha);
            }
        }

        return PixelFrameToWicBitmap(
            factory,
            frame,
            output,
            error
        );
    }
    catch (const std::bad_alloc&) {
        error = L"Not enough memory to apply the freeform alpha mask.";
        return false;
    }
    catch (...) {
        error = L"Unexpected error while applying the freeform alpha mask.";
        return false;
    }
}

} // namespace windows_snip
