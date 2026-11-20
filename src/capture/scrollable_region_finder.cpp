
#include "capture/scrollable_region_finder.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <cstdint>

namespace windows_snip {

namespace {

constexpr unsigned int MAX_PARENT_DEPTH = 32;
constexpr LONG MIN_VIEWPORT_DIMENSION = 32;

std::wstring MakeHresultError(
    const wchar_t* operation,
    HRESULT result
) {
    return
        std::wstring(operation) +
        L" failed. HRESULT: " +
        std::to_wstring(
            static_cast<unsigned long>(result)
        );
}

RECT IntersectRects(
    const RECT& first,
    const RECT& second
) noexcept {
    RECT result{};

    result.left = (std::max)(first.left, second.left);
    result.top = (std::max)(first.top, second.top);
    result.right = (std::min)(first.right, second.right);
    result.bottom = (std::min)(first.bottom, second.bottom);

    if (result.right < result.left ||
        result.bottom < result.top) {
        return {};
    }

    return result;
}

} // namespace



bool ScrollableRegionFinder::Find(
    HWND targetWindow,
    RECT& viewport,
    std::wstring& error
) {
    Reset();
    viewport = {};
    error.clear();

    if (!targetWindow || !IsWindow(targetWindow)) {
        error = L"Invalid target window for scrollable-region detection.";
        return false;
    }

    // Get the target window's client area in screen coordinates.
    RECT clientRect{};

    if (!GetClientRect(targetWindow, &clientRect)) {
        error = L"Could not retrieve the target window client area.";
        return false;
    }

    POINT topLeft{ clientRect.left, clientRect.top };
    POINT bottomRight{ clientRect.right, clientRect.bottom };

    if (!ClientToScreen(targetWindow, &topLeft) ||
        !ClientToScreen(targetWindow, &bottomRight)) {
        error = L"Could not convert client coordinates to screen coordinates.";
        return false;
    }

    const RECT clientScreenRect{
        topLeft.x,
        topLeft.y,
        bottomRight.x,
        bottomRight.y
    };

    if (clientScreenRect.right - clientScreenRect.left <
            MIN_VIEWPORT_DIMENSION ||
        clientScreenRect.bottom - clientScreenRect.top <
            MIN_VIEWPORT_DIMENSION) {
        error = L"The target window client area is too small.";
        return false;
    }

    try {
        // 1. Initialize UI Automation.
        HRESULT hr = CoCreateInstance(
            __uuidof(CUIAutomation),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation_.GetAddressOf())
        );

        if (FAILED(hr) || !automation_) {
            error = MakeHresultError(
                L"Create UI Automation client",
                hr
            );
            Reset();
            return false;
        }

        // 2. Obtain the UI Automation root for the selected HWND.
        Microsoft::WRL::ComPtr<IUIAutomationElement> windowRoot;

        hr = automation_->ElementFromHandle(
            targetWindow,
            windowRoot.GetAddressOf()
        );

        if (FAILED(hr) || !windowRoot) {
            error = MakeHresultError(
                L"Get UI Automation element for selected window",
                hr
            );
            Reset();
            return false;
        }

        // 3. Find all elements advertising ScrollPattern availability.
        VARIANT scrollAvailableValue{};
        scrollAvailableValue.vt = VT_BOOL;
        scrollAvailableValue.boolVal = VARIANT_TRUE;

        Microsoft::WRL::ComPtr<IUIAutomationCondition>
            scrollCondition;

        hr = automation_->CreatePropertyCondition(
            UIA_IsScrollPatternAvailablePropertyId,
            scrollAvailableValue,
            scrollCondition.GetAddressOf()
        );

        if (FAILED(hr) || !scrollCondition) {
            error = MakeHresultError(
                L"Create ScrollPattern search condition",
                hr
            );
            Reset();
            return false;
        }

        Microsoft::WRL::ComPtr<IUIAutomationElementArray>
            candidates;

        hr = windowRoot->FindAll(
            TreeScope_Subtree,
            scrollCondition.Get(),
            candidates.GetAddressOf()
        );

        if (FAILED(hr) || !candidates) {
            error = MakeHresultError(
                L"Search selected window for scrollable elements",
                hr
            );
            Reset();
            return false;
        }

        int candidateCount = 0;

        hr = candidates->get_Length(&candidateCount);

        if (FAILED(hr)) {
            error = MakeHresultError(
                L"Get scrollable element count",
                hr
            );
            Reset();
            return false;
        }

        // 4. Inspect every candidate and choose the largest
        //    visible vertically scrollable region.
        std::int64_t bestArea = 0;

        Microsoft::WRL::ComPtr<IUIAutomationElement>
            bestElement;

        Microsoft::WRL::ComPtr<IUIAutomationScrollPattern>
            bestPattern;

        RECT bestViewport{};

        int patternCount = 0;
        int verticalCount = 0;
        int visibleCount = 0;

        for (int index = 0; index < candidateCount; ++index) {
            Microsoft::WRL::ComPtr<IUIAutomationElement>
                candidate;

            hr = candidates->GetElement(
                index,
                candidate.GetAddressOf()
            );

            if (FAILED(hr) || !candidate) {
                continue;
            }

            Microsoft::WRL::ComPtr<IUnknown>
                patternUnknown;

            hr = candidate->GetCurrentPattern(
                UIA_ScrollPatternId,
                patternUnknown.GetAddressOf()
            );

            if (FAILED(hr) || !patternUnknown) {
                continue;
            }

            ++patternCount;

            Microsoft::WRL::ComPtr<IUIAutomationScrollPattern>
                candidatePattern;

            if (FAILED(patternUnknown.As(&candidatePattern)) ||
                !candidatePattern) {
                continue;
            }

            BOOL verticallyScrollable = FALSE;

            hr = candidatePattern->get_CurrentVerticallyScrollable(
                &verticallyScrollable
            );

            if (FAILED(hr) || !verticallyScrollable) {
                continue;
            }

            ++verticalCount;

            RECT elementRect{};

            hr = candidate->get_CurrentBoundingRectangle(
                &elementRect
            );

            if (FAILED(hr)) {
                continue;
            }

            const RECT clippedRect = IntersectRects(
                elementRect,
                clientScreenRect
            );

            const LONG width =
                clippedRect.right - clippedRect.left;

            const LONG height =
                clippedRect.bottom - clippedRect.top;

            if (width < MIN_VIEWPORT_DIMENSION ||
                height < MIN_VIEWPORT_DIMENSION) {
                continue;
            }

            ++visibleCount;

            const std::int64_t area =
                static_cast<std::int64_t>(width) * height;

            if (area > bestArea) {
                bestArea = area;
                bestElement = candidate;
                bestPattern = candidatePattern;
                bestViewport = clippedRect;
            }
        }

        // 5. Store the best matching scrollable region.
        if (bestElement && bestPattern) {
            scrollElement_ = bestElement;
            scrollPattern_ = bestPattern;
            viewport_ = bestViewport;

            viewport = viewport_;
            return true;
        }

        // Provide a diagnostic that tells us what UIA found.
        if (candidateCount == 0) {
            error =
                L"The selected window's UI Automation tree exposed "
                L"no elements with ScrollPattern availability.";
        }
        else if (verticalCount == 0) {
            error =
                L"UI Automation found " +
                std::to_wstring(patternCount) +
                L" ScrollPattern element(s), but none supported "
                L"vertical scrolling.";
        }
        else if (visibleCount == 0) {
            error =
                L"UI Automation found vertically scrollable elements, "
                L"but none had visible bounds inside the window client area.";
        }
        else {
            error =
                L"UI Automation could not select a usable scrollable region.";
        }

        Reset();
        return false;
    }
    catch (const std::exception&) {
        error =
            L"A C++ exception occurred while detecting the scrollable region.";

        Reset();
        return false;
    }
    catch (...) {
        error = L"An unexpected UI Automation error occurred.";

        Reset();
        return false;
    }
}

// ------------------------------------------------------------
// Scroll control
// ------------------------------------------------------------

bool ScrollableRegionFinder::ScrollToTop(
    std::wstring& error
) {
    error.clear();

    if (!scrollPattern_) {
        error = L"No UI Automation scroll pattern is available.";
        return false;
    }

    // -1.0 preserves the current horizontal scroll position.
    const HRESULT hr =
        scrollPattern_->SetScrollPercent(-1.0, 0.0);

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Reset vertical scroll position",
            hr
        );

        return false;
    }

    return true;
}

bool ScrollableRegionFinder::ScrollDown(
    std::wstring& error
) {
    error.clear();

    if (!scrollPattern_) {
        error = L"No UI Automation scroll pattern is available.";
        return false;
    }

    double currentPercent = 0.0;
    double viewSize = 0.0;

    HRESULT hr =
        scrollPattern_->get_CurrentVerticalScrollPercent(
            &currentPercent
        );

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Read vertical scroll position",
            hr
        );
        return false;
    }

    hr = scrollPattern_->get_CurrentVerticalViewSize(
        &viewSize
    );

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Read vertical scroll view size",
            hr
        );
        return false;
    }

    if (!std::isfinite(currentPercent) ||
        !std::isfinite(viewSize) ||
        currentPercent < 0.0 ||
        currentPercent > 100.0 ||
        viewSize <= 0.0 ||
        viewSize > 100.0) {

        error = L"UI Automation returned invalid vertical scroll metrics.";
        return false;
    }

    // No remaining vertical scroll range.
    if (viewSize >= 100.0 ||
        currentPercent >= 100.0) {
        return true;
    }

    // Calculate a movement of approximately 75% of the viewport.
    // This avoids large jumps on extremely long webpages.
    const double increment =
        (75.0 * viewSize) / (100.0 - viewSize);

    const double nextPercent =
        (std::min)(100.0, currentPercent + increment);

    if (nextPercent <= currentPercent + 0.01) {
        return true;
    }

    hr = scrollPattern_->SetScrollPercent(
        -1.0,
        nextPercent
    );

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Set vertical scroll position",
            hr
        );
        return false;
    }

    return true;
}

bool ScrollableRegionFinder::GetVerticalScrollMetrics(
    double& percent,
    double& viewSize,
    std::wstring& error
) const {
    error.clear();
    percent = 0.0;
    viewSize = 0.0;

    if (!scrollPattern_) {
        error = L"No UI Automation scroll pattern is available.";
        return false;
    }

    HRESULT hr =
        scrollPattern_->get_CurrentVerticalScrollPercent(
            &percent
        );

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Read vertical scroll position",
            hr
        );
        return false;
    }

    hr = scrollPattern_->get_CurrentVerticalViewSize(
        &viewSize
    );

    if (FAILED(hr)) {
        error = MakeHresultError(
            L"Read vertical scroll view size",
            hr
        );
        return false;
    }

    return true;
}


bool ScrollableRegionFinder::GetVerticalScrollPercent(
    double& percent,
    std::wstring& error
) const {
    double viewSize = 0.0;

    return GetVerticalScrollMetrics(
        percent,
        viewSize,
        error
    );
}


bool ScrollableRegionFinder::IsInitialized() const noexcept {
    return
        automation_ != nullptr &&
        scrollElement_ != nullptr &&
        scrollPattern_ != nullptr;
}


void ScrollableRegionFinder::Reset() noexcept {
    scrollPattern_.Reset();
    scrollElement_.Reset();
    automation_.Reset();

    viewport_ = {};
}

} // namespace windows_snip
