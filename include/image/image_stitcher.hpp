
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace windows_snip {

struct PixelFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
    std::vector<std::uint8_t> pixels;

    bool IsValid() const noexcept;
};

enum class StitchAppendResult {
    Appended,
    NoMovement,
    NoOverlap,
    Failed
};

class ImageStitcher final {
public:
    ImageStitcher() = default;
    ~ImageStitcher() = default;

    ImageStitcher(const ImageStitcher&) = delete;
    ImageStitcher& operator=(const ImageStitcher&) = delete;

    // Start a stitching sequence with the first captured frame.
    bool Begin(
        const PixelFrame& firstFrame,
        std::wstring& error
    );

    // Compare and append using unrestricted overlap detection.
StitchAppendResult Append(
    const PixelFrame& nextFrame,
    std::wstring& error
);

// Compare and append using an estimated scroll displacement.
// expectedNewRows is the estimated number of newly revealed rows.
// searchRadiusRows allows for small differences in the estimate.
StitchAppendResult Append(
    const PixelFrame& nextFrame,
    std::uint32_t expectedNewRows,
    std::uint32_t searchRadiusRows,
    std::wstring& error
);

    // Retrieve the complete stitched image.
    bool GetResult(
        PixelFrame& result,
        std::wstring& error
    ) const;

    // Number of new rows appended by the last successful operation.
    std::uint32_t GetLastAppendedRows() const noexcept;

    void Reset() noexcept;

private:
    bool FindVerticalOverlap(
        const PixelFrame& previous,
        const PixelFrame& current,
        std::uint32_t& overlapRows,
        std::uint32_t expectedNewRows,
        std::uint32_t searchRadiusRows,
        std::wstring& error
    ) const;

    PixelFrame previousFrame_;
    PixelFrame stitchedImage_;

    std::uint32_t lastAppendedRows_ = 0;

    bool initialized_ = false;
};

} // namespace windows_snip
