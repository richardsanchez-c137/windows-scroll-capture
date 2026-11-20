
#pragma once

#include <windows.h>

#include <rpc.h>
#include <rpcndr.h>
#include <oaidl.h>

#include <UIAutomation.h>

#include <wrl/client.h>

#include <string>

namespace windows_snip {

class ScrollableRegionFinder final {
public:
    ScrollableRegionFinder() = default;
    ~ScrollableRegionFinder() = default;

    ScrollableRegionFinder(const ScrollableRegionFinder&) = delete;
    ScrollableRegionFinder& operator=(const ScrollableRegionFinder&) = delete;

    // Find the primary vertical scrollable element inside a window.
    // Must be called from a thread with an initialized COM apartment.
    bool Find(
        HWND targetWindow,
        RECT& viewport,
        std::wstring& error
    );
    
    // Scroll the identified element down by one large increment.
    bool ScrollToTop(std::wstring& error);
    bool ScrollDown(std::wstring& error);

    // Retrieve the current vertical scroll position (0-100).
    bool GetVerticalScrollPercent(
        double& percent,
        std::wstring& error
    ) const;
        bool GetVerticalScrollMetrics(
        double& percent,
        double& viewSize,
        std::wstring& error
    ) const;

    bool IsInitialized() const noexcept;

    void Reset() noexcept;

private:
    Microsoft::WRL::ComPtr<IUIAutomation> automation_;

    Microsoft::WRL::ComPtr<IUIAutomationElement> scrollElement_;

    Microsoft::WRL::ComPtr<IUIAutomationScrollPattern>
        scrollPattern_;

    RECT viewport_{};
};

} // namespace windows_snip