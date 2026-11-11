
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ScrollCaptureSession {
public:
    explicit ScrollCaptureSession(const RECT& region);
    ~ScrollCaptureSession();

    bool Ready() const;

    bool CaptureFrame(std::wstring& error);

    bool SaveStitchedPng(
        std::wstring& savedPath,
        std::wstring& error,
        bool preview = false
    ) const;

    std::size_t FrameCount() const;

private:
    RECT region_{};
    int width_ = 0;
    int height_ = 0;

    ULONG_PTR gdiplusToken_ = 0;
    bool ready_ = false;

    std::vector<std::vector<std::uint32_t>> frames_;
};
