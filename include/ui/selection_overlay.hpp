#pragma once

#include <windows.h>
#include <string>

namespace windows_snip {

inline constexpr UINT WM_APP_SELECTION_COMPLETED = WM_APP + 10;
inline constexpr UINT WM_APP_SELECTION_CANCELLED = WM_APP + 11;

class SelectionOverlay final {
public:
    SelectionOverlay() = default;
    ~SelectionOverlay();

    SelectionOverlay(const SelectionOverlay&) = delete;
    SelectionOverlay& operator=(const SelectionOverlay&) = delete;

    bool Start(HWND owner, std::wstring& error);
    void Shutdown() noexcept;

    bool IsActive() const noexcept;

    RECT GetSelectionScreenRect() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT HandleMessage(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    bool RegisterWindowClass(std::wstring& error);
    bool CreateSurface(std::wstring& error);

    void DestroySurface() noexcept;
    void RenderOverlay();

    void UpdateSelection(POINT currentPosition);
    void CompleteSelection();
    void CancelSelection();

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    HINSTANCE instance_ = nullptr;

    POINT virtualOrigin_{};
    POINT dragStart_{};

    RECT selectionClient_{};
    RECT selectionScreen_{};

    int virtualWidth_ = 0;
    int virtualHeight_ = 0;

    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    DWORD* pixels_ = nullptr;

    bool dragging_ = false;
    bool hasSelection_ = false;
    bool active_ = false;
};

} // namespace windows_snip