
#include "scroll_capture.hpp"

#include <windows.h>
#include <ole2.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>
#include <cstddef>

using namespace Gdiplus;

namespace {

constexpr std::size_t MAX_FRAMES = 60;
constexpr int MAX_OUTPUT_HEIGHT = 30000;



double calculateOverlapScore(
    const std::vector<std::uint32_t>& previous,
    const std::vector<std::uint32_t>& current,
    int width,
    int height,
    int overlap
) {
    const int shift = height - overlap;

    double weightedDifference = 0.0;
    double totalWeight = 0.0;

    const int xStep = (std::max)(1, width / 160);
    const int yStep = (std::max)(1, overlap / 140);

    const int xStart = (std::max)(4, width / 10);
    const int xEnd = (std::min)(width - 4, width * 85 / 100);

    const int yStart = 20;
    const int yEnd = overlap - 36;

    if (xEnd <= xStart || yEnd <= yStart) {
        return 255.0;
    }

    auto luminance = [](std::uint32_t pixel) -> int {
        const int red = (pixel >> 16) & 0xFF;
        const int green = (pixel >> 8) & 0xFF;
        const int blue = pixel & 0xFF;

        return (77 * red + 150 * green + 29 * blue) >> 8;
    };

    auto colorDifference = [](
        std::uint32_t a,
        std::uint32_t b
    ) -> double {
        const int redA = (a >> 16) & 0xFF;
        const int greenA = (a >> 8) & 0xFF;
        const int blueA = a & 0xFF;

        const int redB = (b >> 16) & 0xFF;
        const int greenB = (b >> 8) & 0xFF;
        const int blueB = b & 0xFF;

        return (
            std::abs(redA - redB) +
            std::abs(greenA - greenB) +
            std::abs(blueA - blueB)
        ) / 3.0;
    };

    for (int y = yStart; y < yEnd; y += yStep) {

        const int previousY = shift + y;

        for (int x = xStart; x < xEnd; x += xStep) {

            const int previousIndex = previousY * width + x;
            const int currentIndex = y * width + x;

            const std::uint32_t pixelA = previous[previousIndex];
            const std::uint32_t pixelB = current[currentIndex];

            const double pixelColorDifference =
                colorDifference(pixelA, pixelB);

            const int previousDx =
                luminance(previous[previousIndex + 1]) -
                luminance(previous[previousIndex - 1]);

            const int currentDx =
                luminance(current[currentIndex + 1]) -
                luminance(current[currentIndex - 1]);

            const int previousDy =
                luminance(previous[previousIndex + width]) -
                luminance(previous[previousIndex - width]);

            const int currentDy =
                luminance(current[currentIndex + width]) -
                luminance(current[currentIndex - width]);

            const double horizontalDifference =
                std::abs(previousDx - currentDx);

            const double verticalDifference =
                std::abs(previousDy - currentDy);

            const double edgeStrength =
                std::abs(previousDx) +
                std::abs(previousDy) +
                std::abs(currentDx) +
                std::abs(currentDy);

            const double weight =
                1.0 + (std::min)(2.0, edgeStrength / 64.0);

            const double difference =
                pixelColorDifference * 0.60 +
                horizontalDifference * 0.20 +
                verticalDifference * 0.20;

            weightedDifference += difference * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0
        ? weightedDifference / totalWeight
        : 255.0;
}


int findBestOverlap(
    const std::vector<std::uint32_t>& previous,
    const std::vector<std::uint32_t>& current,
    int width,
    int height,
    double& bestScore
) {
    // One wheel event should normally move less than 75%
    // of the selected viewport.
    const int maximumShift =
        (std::min)(height * 3 / 4, 500);

    const int minimumShift = 20;

    const int minimumOverlap = height - maximumShift;
    const int maximumOverlap = height - minimumShift;

    int bestOverlap = 0;
    bestScore = 255.0;

    // Coarse search.
    for (
        int overlap = minimumOverlap;
        overlap <= maximumOverlap;
        overlap += 3
    ) {
        const double score = calculateOverlapScore(
            previous,
            current,
            width,
            height,
            overlap
        );

        if (score < bestScore) {
            bestScore = score;
            bestOverlap = overlap;
        }
    }

    if (bestOverlap == 0) {
        return 0;
    }

    // Fine search around the best coarse result.
    const int refineStart =
        (std::max)(minimumOverlap, bestOverlap - 3);

    const int refineEnd =
        (std::min)(maximumOverlap, bestOverlap + 3);

    for (
        int overlap = refineStart;
        overlap <= refineEnd;
        ++overlap
    ) {
        const double score = calculateOverlapScore(
            previous,
            current,
            width,
            height,
            overlap
        );

        if (score < bestScore) {
            bestScore = score;
            bestOverlap = overlap;
        }
    }

    return bestOverlap;
}


bool findPngEncoder(CLSID& encoder) {
    UINT count = 0;
    UINT size = 0;

    if (GetImageEncodersSize(&count, &size) != Ok || size == 0) {
        return false;
    }

    std::vector<BYTE> buffer(size);

    auto* codecs =
        reinterpret_cast<ImageCodecInfo*>(buffer.data());

    if (GetImageEncoders(count, size, codecs) != Ok) {
        return false;
    }

    for (UINT i = 0; i < count; ++i) {
        if (
            codecs[i].MimeType &&
            wcscmp(codecs[i].MimeType, L"image/png") == 0
        ) {
            encoder = codecs[i].Clsid;
            return true;
        }
    }

    return false;
}

} // namespace

ScrollCaptureSession::ScrollCaptureSession(const RECT& region)
    : region_(region) {

    width_ = region.right - region.left;
    // Exclude the three bottom-edge pixels containing
    // the persistent black capture boundary.
    height_ = (region.bottom - region.top) - 3;

    if (
        width_ <= 0 ||
        height_ <= 0 ||
        width_ > 10000 ||
        height_ > 10000
    ) {
        return;
    }

    GdiplusStartupInput startupInput{};

    ready_ =
        GdiplusStartup(
            &gdiplusToken_,
            &startupInput,
            nullptr
        ) == Ok;
}

ScrollCaptureSession::~ScrollCaptureSession() {
    frames_.clear();

    if (gdiplusToken_) {
        GdiplusShutdown(gdiplusToken_);
    }
}

bool ScrollCaptureSession::Ready() const {
    return ready_;
}

std::size_t ScrollCaptureSession::FrameCount() const {
    return frames_.size();
}

bool ScrollCaptureSession::CaptureFrame(
    std::wstring& error
) {
    if (!ready_) {
        error = L"GDI+ initialization failed.";
        return false;
    }

    if (frames_.size() >= MAX_FRAMES) {
        error = L"Maximum capture limit reached (60 frames).";
        return false;
    }

    HDC screen = GetDC(nullptr);

    if (!screen) {
        error = L"Could not access the desktop device context.";
        return false;
    }

    HDC memory = CreateCompatibleDC(screen);

    if (!memory) {
        ReleaseDC(nullptr, screen);
        error = L"Could not create a memory device context.";
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width_;
    bitmapInfo.bmiHeader.biHeight = -height_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixelData = nullptr;

    HBITMAP bitmap = CreateDIBSection(
        screen,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &pixelData,
        nullptr,
        0
    );

    if (!bitmap || !pixelData) {
        if (bitmap) {
            DeleteObject(bitmap);
        }

        DeleteDC(memory);
        ReleaseDC(nullptr, screen);

        error = L"Could not create screenshot bitmap.";
        return false;
    }

    HGDIOBJ previousBitmap = SelectObject(memory, bitmap);

    const BOOL copied = BitBlt(
        memory,
        0,
        0,
        width_,
        height_,
        screen,
        region_.left,
        region_.top,
        SRCCOPY | CAPTUREBLT
    );

    if (copied) {
        std::vector<std::uint32_t> pixels(
            static_cast<std::size_t>(width_) * height_
        );

        std::memcpy(
            pixels.data(),
            pixelData,
            pixels.size() * sizeof(std::uint32_t)
        );

        // Ensure captured pixels are fully opaque for GDI+.
        for (auto& pixel : pixels) {
            pixel |= 0xFF000000;
        }

        frames_.push_back(std::move(pixels));
    }

    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (!copied) {
        error = L"BitBlt failed while capturing the screen.";
        return false;
    }

    return true;
}

bool ScrollCaptureSession::SaveStitchedPng(
    std::wstring& savedPath,
    std::wstring& error,
    bool preview
) const {

    if (frames_.empty()) {
        error = L"No screenshot frames were captured.";
        return false;
    }

    // Determine overlap between consecutive screenshots.
    std::vector<int> overlaps(frames_.size(), 0);

    int totalHeight = height_;

    for (std::size_t i = 1; i < frames_.size(); ++i) {
        double score = 255.0;

        const int overlap = findBestOverlap(
            frames_[i - 1],
            frames_[i],
            width_,
            height_,
            score
        );

        if (overlap <= 0 || score > 38.0) {
            error =
                L"Could not reliably align screenshot frame " +
                std::to_wstring(i + 1) +
                L". Try a page with clearer visual content.";

            return false;
        }

        overlaps[i] = overlap;
        totalHeight += height_ - overlap;
    }

    if (
        totalHeight > MAX_OUTPUT_HEIGHT ||
        static_cast<std::uint64_t>(width_) * totalHeight >
            100000000ULL
    ) {
        error = L"Stitched image exceeds the current size limit.";
        return false;
    }

    Bitmap output(
        width_,
        totalHeight,
        PixelFormat32bppARGB
    );

    if (output.GetLastStatus() != Ok) {
        error = L"Could not allocate the final screenshot bitmap.";
        return false;
    }


    Gdiplus::BitmapData outputData{};

    Gdiplus::Rect outputRect(
        0,
        0,
        width_,
        totalHeight
    );

    if (output.LockBits(
        &outputRect,
        Gdiplus::ImageLockModeWrite,
        PixelFormat32bppARGB,
        &outputData
    ) != Gdiplus::Ok) {
        error = L"Could not lock the output bitmap.";
        return false;
    }

    int destinationY = 0;

    for (std::size_t i = 0; i < frames_.size(); ++i) {

        const int sourceY = overlaps[i];
        const int drawHeight = height_ - sourceY;

        for (int row = 0; row < drawHeight; ++row) {

            BYTE* destinationRow =
                static_cast<BYTE*>(outputData.Scan0) +
                static_cast<std::ptrdiff_t>(
                    destinationY + row
                ) * outputData.Stride;

            const std::uint32_t* sourceRow =
                frames_[i].data() +
                static_cast<std::size_t>(sourceY + row) *
                width_;

            std::memcpy(
                destinationRow,
                sourceRow,
                static_cast<std::size_t>(width_) *
                sizeof(std::uint32_t)
            );
        }

        destinationY += drawHeight;
    }

    if (output.UnlockBits(&outputData) != Gdiplus::Ok) {
        error = L"Could not unlock the output bitmap.";
        return false;
    }

    wchar_t localAppData[MAX_PATH]{};

    const DWORD pathLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData,
        MAX_PATH
    );

    if (pathLength == 0 || pathLength >= MAX_PATH) {
        error = L"Could not locate the local application data folder.";
        return false;
    }

    const std::filesystem::path outputFolder =
        std::filesystem::path(localAppData) /
        L"WindowsScrollCapture";

    std::error_code filesystemError;

    std::filesystem::create_directories(
        outputFolder,
        filesystemError
    );

    if (filesystemError) {
        error = L"Could not create the screenshot output folder.";
        return false;
    }

    std::filesystem::path outputPath;

    if (preview) {
        outputPath = outputFolder / L"LivePreview.png";
    }
    else {
        SYSTEMTIME time{};
        GetLocalTime(&time);

        wchar_t filename[128]{};

        swprintf_s(
            filename,
            L"Scrolling Screenshot %04u-%02u-%02u %02u%02u%02u-%03u.png",
            time.wYear,
            time.wMonth,
            time.wDay,
            time.wHour,
            time.wMinute,
            time.wSecond,
            time.wMilliseconds
        );

        outputPath = outputFolder / filename;
    }

    CLSID pngEncoder{};

    if (!findPngEncoder(pngEncoder)) {
        error = L"PNG encoder could not be found.";
        return false;
    }

    if (output.Save(outputPath.c_str(), &pngEncoder, nullptr) != Ok) {
        error = L"Could not save the stitched PNG image.";
        return false;
    }

    savedPath = outputPath.wstring();
    return true;
}
