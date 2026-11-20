
#include "image/image_stitcher.hpp"

#include <cstdint>
#include <iostream>
#include <string>

using namespace windows_snip;

namespace {

PixelFrame MakeViewport(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t documentOffset,
    std::uint32_t salt = 0
) {
    PixelFrame frame;

    frame.width = width;
    frame.height = height;
    frame.stride = static_cast<std::size_t>(width) * 4;

    frame.pixels.resize(frame.stride * height);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {

            const std::uint32_t globalY =
                documentOffset + y + salt;

            std::uint32_t value =
                x * 0x9E3779B9u ^
                globalY * 0x85EBCA6Bu;

            value ^= value >> 16;
            value *= 0x7FEB352Du;
            value ^= value >> 15;
            value *= 0x846CA68Bu;
            value ^= value >> 16;

            const std::size_t offset =
                static_cast<std::size_t>(y) * frame.stride +
                static_cast<std::size_t>(x) * 4;

            frame.pixels[offset + 0] =
                static_cast<std::uint8_t>(value);

            frame.pixels[offset + 1] =
                static_cast<std::uint8_t>(value >> 8);

            frame.pixels[offset + 2] =
                static_cast<std::uint8_t>(value >> 16);

            frame.pixels[offset + 3] = 255;
        }
    }

    return frame;
}

bool Fail(const wchar_t* message) {
    std::wcerr << L"[FAIL] " << message << L'\n';
    return false;
}

bool TestScrollingStitch() {

    constexpr std::uint32_t WIDTH = 96;
    constexpr std::uint32_t VIEWPORT_HEIGHT = 300;
    constexpr std::uint32_t SCROLL_STEP = 180;
    constexpr std::uint32_t EXPECTED_HEIGHT = 840;

    ImageStitcher stitcher;

    std::wstring error;

    std::wcout << L"\nTest 1: Normal scrolling and stitching\n";

    const PixelFrame firstFrame =
        MakeViewport(WIDTH, VIEWPORT_HEIGHT, 0);

    if (!stitcher.Begin(firstFrame, error)) {
        return Fail(error.c_str());
    }

    const std::uint32_t offsets[] = {
        180, 360, 540
    };

    for (const auto offset : offsets) {

        const PixelFrame nextFrame =
            MakeViewport(WIDTH, VIEWPORT_HEIGHT, offset);

        const auto result =
            stitcher.Append(nextFrame, error);

        if (result != StitchAppendResult::Appended) {
            return Fail(error.c_str());
        }

        if (stitcher.GetLastAppendedRows() != SCROLL_STEP) {
            return Fail(L"Incorrect number of rows appended.");
        }

        std::wcout
            << L"  Appended "
            << stitcher.GetLastAppendedRows()
            << L" rows\n";
    }

    PixelFrame output;

    if (!stitcher.GetResult(output, error)) {
        return Fail(error.c_str());
    }

    if (output.height != EXPECTED_HEIGHT) {
        return Fail(L"Final stitched image height is incorrect.");
    }

    if (output.width != WIDTH) {
        return Fail(L"Final stitched image width is incorrect.");
    }

    // Every output pixel must exactly match the original document.
    const PixelFrame expected =
        MakeViewport(WIDTH, EXPECTED_HEIGHT, 0);

    if (output.pixels != expected.pixels) {
        return Fail(L"Stitched pixels differ from the original document.");
    }

    std::wcout << L"[PASS] Correct image dimensions and pixel contents.\n";

    return true;
}

bool TestExpectedDisplacementWithDynamicContent() {

    constexpr std::uint32_t WIDTH = 96;
    constexpr std::uint32_t HEIGHT = 300;
    constexpr std::uint32_t SCROLL_STEP = 180;

    std::wcout
        << L"\nTest 4: Expected displacement with dynamic content\n";

    ImageStitcher stitcher;
    std::wstring error;

    const PixelFrame first =
        MakeViewport(WIDTH, HEIGHT, 0);

    PixelFrame next =
        MakeViewport(WIDTH, HEIGHT, SCROLL_STEP);

    // Simulate a changing animation/video region in the
    // overlapping part of the second captured frame.
    for (std::uint32_t y = 0; y < 60; ++y) {
        for (std::uint32_t x = 0; x < WIDTH / 3; ++x) {

            const std::size_t offset =
                static_cast<std::size_t>(y) * next.stride +
                static_cast<std::size_t>(x) * 4;

            next.pixels[offset + 0] = 255;
            next.pixels[offset + 1] = 0;
            next.pixels[offset + 2] = 255;
        }
    }

    if (!stitcher.Begin(first, error)) {
        return Fail(error.c_str());
    }

    const auto result = stitcher.Append(
        next,
        SCROLL_STEP,
        24,
        error
    );

    if (result != StitchAppendResult::Appended) {
        return Fail(error.c_str());
    }

    if (stitcher.GetLastAppendedRows() != SCROLL_STEP) {
        return Fail(L"Expected-displacement matching appended incorrect rows.");
    }

    PixelFrame output;

    if (!stitcher.GetResult(output, error)) {
        return Fail(error.c_str());
    }

    if (output.height != HEIGHT + SCROLL_STEP) {
        return Fail(L"Unexpected output height after dynamic-content stitching.");
    }

    std::wcout
        << L"[PASS] Expected displacement matched despite dynamic pixels.\n";

    return true;
}

bool TestNoMovement() {

    std::wcout << L"\nTest 2: No movement detection\n";

    ImageStitcher stitcher;

    std::wstring error;

    const PixelFrame frame =
        MakeViewport(96, 300, 0);

    if (!stitcher.Begin(frame, error)) {
        return Fail(error.c_str());
    }

    const auto result = stitcher.Append(frame, error);

    if (result != StitchAppendResult::NoMovement) {
        return Fail(L"Identical frames were not detected as stationary.");
    }

    if (stitcher.GetLastAppendedRows() != 0) {
        return Fail(L"Rows were appended despite no movement.");
    }

    std::wcout << L"[PASS] Identical frames correctly ignored.\n";

    return true;
}


bool TestNoOverlap() {

    std::wcout << L"\nTest 3: Unrelated frame rejection\n";

    ImageStitcher stitcher;

    std::wstring error;

    const PixelFrame first =
        MakeViewport(96, 300, 0);

    const PixelFrame unrelated =
        MakeViewport(96, 300, 0, 0x2468u);

    if (!stitcher.Begin(first, error)) {
        return Fail(error.c_str());
    }

    const auto result = stitcher.Append(unrelated, error);

    if (result != StitchAppendResult::NoOverlap) {
        return Fail(L"Unrelated image was incorrectly accepted.");
    }

    PixelFrame output;

    if (!stitcher.GetResult(output, error)) {
        return Fail(error.c_str());
    }

    if (output.height != 300) {
        return Fail(L"Failed append modified the stitched image.");
    }

    std::wcout << L"[PASS] Unrelated frames rejected safely.\n";

    return true;
}

} // namespace


int main() {

    std::wcout << L"=====================================\n";
    std::wcout << L" Windows Snip Tool - Stitcher Tests\n";
    std::wcout << L"=====================================\n";

    const bool test1 = TestScrollingStitch();
    const bool test2 = TestNoMovement();
    const bool test3 = TestNoOverlap();
    const bool test4 = TestExpectedDisplacementWithDynamicContent();

    std::wcout << L"\n=====================================\n";

    if (test1 && test2 && test3 && test4) {
        std::wcout << L"ALL TESTS PASSED\n";
        return 0;
    }

    std::wcout << L"TEST FAILURE\n";
    return 1;
}
