
#include "image/image_stitcher.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <array>

namespace windows_snip {

namespace {

constexpr std::uint64_t MAX_FRAME_BYTES =
    256ULL * 1024ULL * 1024ULL;

constexpr std::uint64_t MAX_OUTPUT_BYTES =
    512ULL * 1024ULL * 1024ULL;

constexpr double MAX_OVERLAP_ERROR = 20.0;

constexpr std::uint32_t MAX_SAMPLE_ROWS = 64;
constexpr std::uint32_t MAX_SAMPLE_COLUMNS = 32;

// Convert a frame into tightly packed BGRA pixels.
bool MakeTightCopy(
    const PixelFrame& source,
    PixelFrame& destination,
    std::wstring& error
) {
    if (!source.IsValid()) {
        error = L"Cannot copy an invalid pixel frame.";
        return false;
    }

    const std::uint64_t rowBytes =
        static_cast<std::uint64_t>(source.width) * 4ULL;

    const std::uint64_t totalBytes =
        rowBytes * source.height;

    if (totalBytes > MAX_FRAME_BYTES ||
        rowBytes > std::numeric_limits<std::size_t>::max() ||
        totalBytes > std::numeric_limits<std::size_t>::max()) {

        error = L"The captured frame exceeds the memory limit.";
        return false;
    }

    PixelFrame copy;

    copy.width = source.width;
    copy.height = source.height;
    copy.stride = static_cast<std::size_t>(rowBytes);

    copy.pixels.resize(static_cast<std::size_t>(totalBytes));

    for (std::uint32_t y = 0; y < source.height; ++y) {

        const auto* sourceRow =
            source.pixels.data() +
            static_cast<std::size_t>(y) * source.stride;

        auto* destinationRow =
            copy.pixels.data() +
            static_cast<std::size_t>(y) * copy.stride;

        std::memcpy(
            destinationRow,
            sourceRow,
            copy.stride
        );
    }

    destination = std::move(copy);

    return true;
}

// Check whether two frames contain exactly the same pixels.
bool FramesIdentical(
    const PixelFrame& first,
    const PixelFrame& second
) noexcept {

    if (first.width != second.width ||
        first.height != second.height) {
        return false;
    }

    const std::size_t rowBytes =
        static_cast<std::size_t>(first.width) * 4;

    for (std::uint32_t y = 0; y < first.height; ++y) {

        const auto* firstRow =
            first.pixels.data() +
            static_cast<std::size_t>(y) * first.stride;

        const auto* secondRow =
            second.pixels.data() +
            static_cast<std::size_t>(y) * second.stride;

        if (std::memcmp(firstRow, secondRow, rowBytes) != 0) {
            return false;
        }
    }

    return true;
}

// Calculate the average RGB difference between two vertical regions.
//
// Alpha is intentionally ignored because transparency does not help
// identify overlapping screenshot content.
double CalculateRegionDifference(
    const PixelFrame& first,
    const PixelFrame& second,
    std::uint32_t firstTop,
    std::uint32_t secondTop,
    std::uint32_t regionHeight
) noexcept {

    if (regionHeight == 0) {
        return 255.0;
    }

    const std::uint32_t rowSamples =
        std::min(regionHeight, MAX_SAMPLE_ROWS);

    const std::uint32_t columnSamples =
        std::min(first.width, MAX_SAMPLE_COLUMNS);

    if (rowSamples == 0 || columnSamples == 0) {
        return 255.0;
    }

    std::array<std::uint16_t,
        MAX_SAMPLE_ROWS * MAX_SAMPLE_COLUMNS> differences{};

    std::size_t sampleCount = 0;

    for (std::uint32_t rowIndex = 0;
         rowIndex < rowSamples;
         ++rowIndex) {

        const std::uint32_t relativeY =
            rowSamples == 1
                ? regionHeight / 2
                : static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(rowIndex) *
                     (regionHeight - 1)) /
                    (rowSamples - 1)
                );

        const std::uint32_t firstY =
            firstTop + relativeY;

        const std::uint32_t secondY =
            secondTop + relativeY;

        const auto* firstRow =
            first.pixels.data() +
            static_cast<std::size_t>(firstY) * first.stride;

        const auto* secondRow =
            second.pixels.data() +
            static_cast<std::size_t>(secondY) * second.stride;

        for (std::uint32_t columnIndex = 0;
             columnIndex < columnSamples;
             ++columnIndex) {

            const std::uint32_t x =
                columnSamples == 1
                    ? 0
                    : static_cast<std::uint32_t>(
                        (static_cast<std::uint64_t>(columnIndex) *
                         (first.width - 1)) /
                        (columnSamples - 1)
                    );

            const std::size_t pixelOffset =
                static_cast<std::size_t>(x) * 4;

            unsigned int pixelDifference = 0;

            // Compare B, G and R. Ignore alpha.
            for (std::size_t channel = 0; channel < 3; ++channel) {

                const int difference =
                    static_cast<int>(
                        firstRow[pixelOffset + channel]
                    ) -
                    static_cast<int>(
                        secondRow[pixelOffset + channel]
                    );

                pixelDifference += static_cast<unsigned int>(
                    difference < 0 ? -difference : difference
                );
            }

            differences[sampleCount++] =
                static_cast<std::uint16_t>(pixelDifference / 3);
        }
    }

    if (sampleCount == 0) {
        return 255.0;
    }

    // Ignore the highest 25% of pixel differences.
    // This reduces the influence of animations, cursors,
    // video frames and other changing webpage regions.
    std::sort(
        differences.begin(),
        differences.begin() +
            static_cast<std::ptrdiff_t>(sampleCount)
    );

    const std::size_t retainedSamples =
        (std::max)(
            static_cast<std::size_t>(1),
            (sampleCount * 3) / 4
        );

    std::uint64_t totalDifference = 0;

    for (std::size_t i = 0; i < retainedSamples; ++i) {
        totalDifference += differences[i];
    }

    return static_cast<double>(totalDifference) /
           static_cast<double>(retainedSamples);
}

} // namespace


// ------------------------------------------------------------
// PixelFrame
// ------------------------------------------------------------

bool PixelFrame::IsValid() const noexcept {

    if (width == 0 || height == 0 || stride == 0) {
        return false;
    }

    const std::uint64_t minimumStride =
        static_cast<std::uint64_t>(width) * 4ULL;

    if (minimumStride > std::numeric_limits<std::size_t>::max() ||
        stride < minimumStride) {
        return false;
    }

    if (height >
        std::numeric_limits<std::size_t>::max() / stride) {
        return false;
    }

    const std::size_t requiredBytes =
        stride * static_cast<std::size_t>(height);

    return pixels.size() >= requiredBytes;
}


// ------------------------------------------------------------
// ImageStitcher
// ------------------------------------------------------------

bool ImageStitcher::Begin(
    const PixelFrame& firstFrame,
    std::wstring& error
) {
    Reset();
    error.clear();

    try {

        PixelFrame normalized;

        if (!MakeTightCopy(
            firstFrame,
            normalized,
            error
        )) {
            return false;
        }

        // Keep the most recent viewport and accumulated output separately.
        previousFrame_ = normalized;
        stitchedImage_ = std::move(normalized);

        initialized_ = true;

        return true;
    }
    catch (const std::bad_alloc&) {

        Reset();

        error = L"Not enough memory to initialize image stitching.";

        return false;
    }
    catch (...) {

        Reset();

        error = L"Unexpected error initializing image stitching.";

        return false;
    }
}



bool ImageStitcher::FindVerticalOverlap(
    const PixelFrame& previous,
    const PixelFrame& current,
    std::uint32_t& overlapRows,
    std::uint32_t expectedNewRows,
    std::uint32_t searchRadiusRows,
    std::wstring& error
) const {

    overlapRows = 0;

    if (!previous.IsValid() || !current.IsValid()) {
        error = L"Cannot compare invalid image frames.";
        return false;
    }

    if (previous.width != current.width ||
        previous.height != current.height) {

        error = L"Scrolling frames must have identical dimensions.";
        return false;
    }

    const std::uint32_t height = previous.height;

    const std::uint32_t minimumOverlap =
        (std::max)(8U, height / 12U);

    if (minimumOverlap >= height) {
        error = L"The captured frame is too small for overlap detection.";
        return false;
    }

    double bestDifference =
        std::numeric_limits<double>::max();

    std::uint32_t bestOverlap = 0;

    const std::uint32_t maximumDelta =
        height - minimumOverlap;

    // Default: search the entire valid overlap range.
    std::uint32_t firstDelta = 1;
    std::uint32_t lastDelta = maximumDelta;

    // Restrict the search when an expected scroll displacement
    // is available.
    if (expectedNewRows != 0) {

        const std::uint64_t firstCandidate =
            expectedNewRows > searchRadiusRows
                ? static_cast<std::uint64_t>(
                    expectedNewRows - searchRadiusRows
                  )
                : 1ULL;

        const std::uint64_t lastCandidate =
            static_cast<std::uint64_t>(expectedNewRows) +
            searchRadiusRows;

        firstDelta = static_cast<std::uint32_t>(
            (std::max)(
                1ULL,
                firstCandidate
            )
        );

        lastDelta = static_cast<std::uint32_t>(
            (std::min)(
                static_cast<std::uint64_t>(maximumDelta),
                lastCandidate
            )
        );

        if (firstDelta > lastDelta) {
            error =
                L"The expected scroll displacement is outside "
                L"the valid overlap range.";

            return false;
        }
    }

    // Compare the bottom of the previous frame against the
    // corresponding top region of the current frame.
    for (std::uint32_t delta = firstDelta;
         delta <= lastDelta;
         ++delta) {

        const std::uint32_t candidateOverlap =
            height - delta;

        const double difference =
            CalculateRegionDifference(
                previous,
                current,
                delta,
                0,
                candidateOverlap
            );

        if (difference < bestDifference - 0.05) {
            bestDifference = difference;
            bestOverlap = candidateOverlap;
        }
    }

    if (bestOverlap == 0 ||
        bestDifference > MAX_OVERLAP_ERROR) {

        error =
            L"Could not find a reliable overlap between the captured frames.";

        return false;
    }

    overlapRows = bestOverlap;

    return true;
}

StitchAppendResult ImageStitcher::Append(
    const PixelFrame& nextFrame,
    std::wstring& error
) {
    return Append(nextFrame, 0, 0, error);
}


StitchAppendResult ImageStitcher::Append(
    const PixelFrame& nextFrame,
    std::uint32_t expectedNewRows,
    std::uint32_t searchRadiusRows,
    std::wstring& error
) {
    error.clear();
    lastAppendedRows_ = 0;

    if (!initialized_) {
        error = L"Image stitching has not been initialized.";
        return StitchAppendResult::Failed;
    }

    if (!nextFrame.IsValid()) {
        error = L"The next captured frame is invalid.";
        return StitchAppendResult::Failed;
    }

    try {

        if (nextFrame.width != previousFrame_.width ||
            nextFrame.height != previousFrame_.height) {

            error =
                L"The new frame dimensions do not match the previous frame.";

            return StitchAppendResult::Failed;
        }

        // No pixel difference means the viewport has not moved.
        if (FramesIdentical(previousFrame_, nextFrame)) {
            return StitchAppendResult::NoMovement;
        }

        std::uint32_t overlapRows = 0;

        if (!FindVerticalOverlap(
            previousFrame_,
            nextFrame,
            overlapRows,
            expectedNewRows,
            searchRadiusRows,
            error
        )) {
            return StitchAppendResult::NoOverlap;
        }

        const std::uint32_t appendedRows =
            nextFrame.height - overlapRows;

        if (appendedRows == 0) {
            return StitchAppendResult::NoMovement;
        }

        PixelFrame normalizedNext;

        if (!MakeTightCopy(
            nextFrame,
            normalizedNext,
            error
        )) {
            return StitchAppendResult::Failed;
        }

        const std::uint64_t newHeight64 =
            static_cast<std::uint64_t>(stitchedImage_.height) +
            appendedRows;

        const std::uint64_t newSize64 =
            newHeight64 * stitchedImage_.stride;

        if (newHeight64 > std::numeric_limits<std::uint32_t>::max() ||
            newSize64 > MAX_OUTPUT_BYTES ||
            newSize64 > std::numeric_limits<std::size_t>::max()) {

            error =
                L"The stitched image has reached its maximum size limit.";

            return StitchAppendResult::Failed;
        }

        const std::size_t oldSize =
            stitchedImage_.pixels.size();

        const std::size_t addedBytes =
            static_cast<std::size_t>(appendedRows) *
            stitchedImage_.stride;

        // resize either succeeds or leaves the original vector intact.
        stitchedImage_.pixels.resize(oldSize + addedBytes);

        // Copy only the newly revealed section.
        const auto* sourcePixels =
            normalizedNext.pixels.data() +
            static_cast<std::size_t>(overlapRows) *
            normalizedNext.stride;

        auto* destinationPixels =
            stitchedImage_.pixels.data() + oldSize;

        std::memcpy(
            destinationPixels,
            sourcePixels,
            addedBytes
        );

        stitchedImage_.height =
            static_cast<std::uint32_t>(newHeight64);

        // This viewport becomes the reference for the next frame.
        previousFrame_ = std::move(normalizedNext);

        lastAppendedRows_ = appendedRows;

        return StitchAppendResult::Appended;
    }
    catch (const std::bad_alloc&) {

        error = L"Not enough memory to append the captured frame.";

        return StitchAppendResult::Failed;
    }
    catch (...) {

        error = L"Unexpected error while stitching the captured frame.";

        return StitchAppendResult::Failed;
    }
}


bool ImageStitcher::GetResult(
    PixelFrame& result,
    std::wstring& error
) const {

    error.clear();

    if (!initialized_ || !stitchedImage_.IsValid()) {
        error = L"No stitched image is available.";
        return false;
    }

    try {

        result = stitchedImage_;

        return true;
    }
    catch (const std::bad_alloc&) {

        error = L"Not enough memory to retrieve the stitched image.";

        return false;
    }
    catch (...) {

        error = L"Unexpected error retrieving the stitched image.";

        return false;
    }
}


std::uint32_t ImageStitcher::GetLastAppendedRows() const noexcept {
    return lastAppendedRows_;
}


void ImageStitcher::Reset() noexcept {

    previousFrame_ = {};
    stitchedImage_ = {};

    lastAppendedRows_ = 0;
    initialized_ = false;
}

} // namespace windows_snip