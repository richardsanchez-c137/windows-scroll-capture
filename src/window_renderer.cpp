#include "graphics/window_renderer.hpp"

#include <algorithm>
#include <cwchar>
#include <sstream>
#include <iterator>

namespace windows_snip {

namespace {

std::wstring makeRenderError(
    const wchar_t* operation,
    HRESULT result
) {
    std::wostringstream stream;

    stream << operation
           << L" failed. HRESULT: 0x"
           << std::hex
           << std::uppercase
           << static_cast<unsigned long>(result);

    return stream.str();
}

} // namespace

bool WindowRenderer::Initialize(
    HWND window,
    GraphicsRuntime& graphics,
    std::wstring& error
) {
    error.clear();
    Shutdown();

    if (!window || !IsWindow(window)) {
        error = L"Invalid application window.";
        return false;
    }

    if (!graphics.IsInitialized()) {
        error = L"Graphics runtime has not been initialized.";
        return false;
    }

    window_ = window;
    d2dFactory_ = graphics.GetD2DFactory();

    HRESULT result = graphics.GetDWriteFactory()->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        20.0f,
        L"en-us",
        textFormat_.GetAddressOf()
    );

    if (FAILED(result)) {
        error = makeRenderError(L"Header text format creation", result);
        Shutdown();
        return false;
    }

    result = graphics.GetDWriteFactory()->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f,
        L"en-us",
        buttonTextFormat_.GetAddressOf()
    );

    if (FAILED(result)) {
        error = makeRenderError(L"Button text format creation", result);
        Shutdown();
        return false;
    }

    result = graphics.GetDWriteFactory()->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        23.0f,
        L"en-us",
        messageTextFormat_.GetAddressOf()
    );

    if (FAILED(result)) {
        error = makeRenderError(L"Message text format creation", result);
        Shutdown();
        return false;
    }

    result = graphics.GetDWriteFactory()->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f,
        L"en-us",
        captionTextFormat_.GetAddressOf()
    );

    if (FAILED(result)) {
        error = makeRenderError(L"Caption text format creation", result);
        Shutdown();
        return false;
    }

    messageTextFormat_->SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT_CENTER
    );
    captionTextFormat_->SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT_CENTER
    );

    messageTextFormat_->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );
    captionTextFormat_->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER
    );

    if (!CreateDeviceResources(error)) {
        Shutdown();
        return false;
    }

    return true;
}

bool WindowRenderer::CreateDeviceResources(
    std::wstring& error
) {
    error.clear();

    if (!d2dFactory_ || !window_) {
        error = L"Renderer is not initialized.";
        return false;
    }

    RECT client{};

    if (!GetClientRect(window_, &client)) {
        error = L"Could not retrieve the window dimensions.";
        return false;
    }

    const UINT width = static_cast<UINT>(
        (std::max)(1L, client.right - client.left)
    );

    const UINT height = static_cast<UINT>(
        (std::max)(1L, client.bottom - client.top)
    );

    const D2D1_SIZE_U size = D2D1::SizeU(width, height);

    const D2D1_RENDER_TARGET_PROPERTIES targetProperties =
        D2D1::RenderTargetProperties();

    const D2D1_HWND_RENDER_TARGET_PROPERTIES windowProperties =
        D2D1::HwndRenderTargetProperties(
            window_,
            size,
            D2D1_PRESENT_OPTIONS_NONE
        );

    HRESULT result = d2dFactory_->CreateHwndRenderTarget(
        targetProperties,
        windowProperties,
        renderTarget_.GetAddressOf()
    );

    if (FAILED(result)) {
        error = makeRenderError(L"Direct2D render target creation", result);
        return false;
    }

    // Create the brushes used throughout the interface.
    auto createBrush = [&](const D2D1_COLOR_F& color,
                           Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush,
                           const wchar_t* operation) -> bool {
        const HRESULT brushResult = renderTarget_->CreateSolidColorBrush(
            color,
            brush.GetAddressOf()
        );

        if (FAILED(brushResult)) {
            error = makeRenderError(operation, brushResult);
            return false;
        }

        return true;
    };

    if (!createBrush(
        D2D1::ColorF(0.94f, 0.95f, 0.97f, 1.0f),
        textBrush_,
        L"Primary text brush creation"
    )) {
        return false;
    }

    if (!createBrush(
        D2D1::ColorF(0.62f, 0.65f, 0.70f, 1.0f),
        mutedTextBrush_,
        L"Muted text brush creation"
    )) {
        return false;
    }

    if (!createBrush(
        D2D1::ColorF(0.27f, 0.55f, 0.96f, 1.0f),
        accentBrush_,
        L"Accent brush creation"
    )) {
        return false;
    }

    if (!createBrush(
        D2D1::ColorF(0.16f, 0.18f, 0.22f, 1.0f),
        buttonBrush_,
        L"Button brush creation"
    )) {
        return false;
    }

    if (!createBrush(
        D2D1::ColorF(0.27f, 0.30f, 0.35f, 1.0f),
        borderBrush_,
        L"Border brush creation"
    )) {
        return false;
    }

    if (!createBrush(
        D2D1::ColorF(0.22f, 0.24f, 0.28f, 1.0f),
        separatorBrush_,
        L"Separator brush creation"
    )) {
        return false;
    }

    return true;
}

void WindowRenderer::Render() {
    if (!d2dFactory_ || !window_) {
        return;
    }

    if (!renderTarget_) {
        std::wstring error;

        if (!CreateDeviceResources(error)) {
            OutputDebugStringW(error.c_str());
            OutputDebugStringW(L"\n");
            return;
        }
    }

    const D2D1_SIZE_F size = renderTarget_->GetSize();

    renderTarget_->BeginDraw();

    // Main background.
    renderTarget_->Clear(
        D2D1::ColorF(0.075f, 0.082f, 0.098f, 1.0f)
    );

    // Small helper for drawing text.
    auto drawText = [&](const wchar_t* value,
                        IDWriteTextFormat* format,
                        float left,
                        float top,
                        float right,
                        float bottom,
                        ID2D1Brush* brush) {
        renderTarget_->DrawTextW(
            value,
            static_cast<UINT32>(std::wcslen(value)),
            format,
            D2D1::RectF(left, top, right, bottom),
            brush
        );
    };

    auto drawSeparator = [&](float x, float top, float bottom) {
        renderTarget_->DrawLine(
            D2D1::Point2F(x, top),
            D2D1::Point2F(x, bottom),
            separatorBrush_.Get(),
            1.0f
        );
    };

    auto drawChevron = [&](float x, float y) {
        renderTarget_->DrawLine(
            D2D1::Point2F(x - 4.0f, y - 2.0f),
            D2D1::Point2F(x, y + 2.0f),
            mutedTextBrush_.Get(),
            1.5f
        );

        renderTarget_->DrawLine(
            D2D1::Point2F(x, y + 2.0f),
            D2D1::Point2F(x + 4.0f, y - 2.0f),
            mutedTextBrush_.Get(),
            1.5f
        );
    };

    // ---------------------------------------------------------
    // 1. Application header
    // ---------------------------------------------------------

    // Simple capture-style application icon.
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(28.0f, 22.0f, 54.0f, 45.0f),
            4.0f,
            4.0f
        ),
        accentBrush_.Get(),
        2.0f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(34.0f, 28.0f),
        D2D1::Point2F(34.0f, 39.0f),
        textBrush_.Get(),
        1.5f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(34.0f, 28.0f),
        D2D1::Point2F(44.0f, 28.0f),
        textBrush_.Get(),
        1.5f
    );

    drawText(
        L"Windows Snip Tool",
        textFormat_.Get(),
        66.0f,
        17.0f,
        300.0f,
        51.0f,
        textBrush_.Get()
    );

    drawSeparator(20.0f, 66.0f, 66.0f);

    renderTarget_->DrawLine(
        D2D1::Point2F(20.0f, 66.0f),
        D2D1::Point2F(size.width - 20.0f, 66.0f),
        separatorBrush_.Get(),
        1.0f
    );

    // ---------------------------------------------------------
    // 2. Main capture toolbar
    // ---------------------------------------------------------

    // New button.
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(28.0f, 82.0f, 132.0f, 128.0f),
            7.0f,
            7.0f
        ),
        accentBrush_.Get()
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(48.0f, 96.0f),
        D2D1::Point2F(48.0f, 114.0f),
        textBrush_.Get(),
        2.0f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(39.0f, 105.0f),
        D2D1::Point2F(57.0f, 105.0f),
        textBrush_.Get(),
        2.0f
    );

    drawText(
        L"New",
        buttonTextFormat_.Get(),
        65.0f,
        87.0f,
        121.0f,
        123.0f,
        textBrush_.Get()
    );

    // Screenshot and video mode selector.
    drawSeparator(150.0f, 86.0f, 124.0f);

    // Screenshot mode - selected.
    if (!videoModeSelected_) {
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(169.0f, 82.0f, 217.0f, 128.0f),
                6.0f,
                6.0f
            ),
            buttonBrush_.Get()
        );
    }

    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(182.0f, 96.0f, 204.0f, 113.0f),
            3.0f,
            3.0f
        ),
        textBrush_.Get(),
        1.7f
    );

    renderTarget_->DrawEllipse(
        D2D1::Ellipse(
            D2D1::Point2F(193.0f, 104.5f),
            4.0f,
            4.0f
        ),
        textBrush_.Get(),
        1.5f
    );

    if (!videoModeSelected_) {
        renderTarget_->FillRectangle(
            D2D1::RectF(180.0f, 125.0f, 206.0f, 128.0f),
            accentBrush_.Get()
        );
    }

    if (videoModeSelected_) {
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(225.0f, 82.0f, 267.0f, 128.0f),
                6.0f,
                6.0f
            ),
            buttonBrush_.Get()
        );
    }

    // Video mode.
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(231.0f, 96.0f, 252.0f, 113.0f),
            3.0f,
            3.0f
        ),
        mutedTextBrush_.Get(),
        1.7f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(252.0f, 100.0f),
        D2D1::Point2F(260.0f, 96.0f),
        mutedTextBrush_.Get(),
        1.7f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(260.0f, 96.0f),
        D2D1::Point2F(260.0f, 113.0f),
        mutedTextBrush_.Get(),
        1.7f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(260.0f, 113.0f),
        D2D1::Point2F(252.0f, 109.0f),
        mutedTextBrush_.Get(),
        1.7f
    );

    drawSeparator(278.0f, 86.0f, 124.0f);

    const wchar_t* captureModeLabel = L"Rectangle";

    switch (captureMode_) {
    case CaptureMode::Rectangle:
        captureModeLabel = L"Rectangle";
        break;

    case CaptureMode::Window:
        captureModeLabel = L"Window";
        break;

    case CaptureMode::FullScreen:
        captureModeLabel = L"Full screen";
        break;

    case CaptureMode::Freeform:
        captureModeLabel = L"Freeform";
        break;
    }

    const wchar_t* delayLabel = L"No delay";

    switch (captureDelaySeconds_) {
    case 0:
        delayLabel = L"No delay";
        break;

    case 3:
        delayLabel = L"3 seconds";
        break;

    case 5:
        delayLabel = L"5 seconds";
        break;

    case 10:
        delayLabel = L"10 seconds";
        break;
    }

    // Capture mode selector.
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(296.0f, 84.0f, 450.0f, 126.0f),
            6.0f,
            6.0f
        ),
        borderBrush_.Get(),
        1.0f
    );

    renderTarget_->DrawRectangle(
        D2D1::RectF(310.0f, 96.0f, 330.0f, 113.0f),
        textBrush_.Get(),
        1.5f
    );

    drawText(
        captureModeLabel,
        buttonTextFormat_.Get(),
        340.0f,
        87.0f,
        418.0f,
        123.0f,
        textBrush_.Get()
    );

    drawChevron(433.0f, 105.0f);

    // Capture delay selector.
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(466.0f, 84.0f, 610.0f, 126.0f),
            6.0f,
            6.0f
        ),
        borderBrush_.Get(),
        1.0f
    );

    renderTarget_->DrawEllipse(
        D2D1::Ellipse(
            D2D1::Point2F(485.0f, 105.0f),
            9.0f,
            9.0f
        ),
        textBrush_.Get(),
        1.5f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(485.0f, 105.0f),
        D2D1::Point2F(485.0f, 99.0f),
        textBrush_.Get(),
        1.5f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(485.0f, 105.0f),
        D2D1::Point2F(490.0f, 108.0f),
        textBrush_.Get(),
        1.5f
    );

    drawText(
        delayLabel,
        buttonTextFormat_.Get(),
        503.0f,
        87.0f,
        580.0f,
        123.0f,
        textBrush_.Get()
    );

    drawChevron(594.0f, 105.0f);

    // More options menu.
    const float moreX = size.width - 48.0f;

    for (int i = 0; i < 3; ++i) {
        renderTarget_->FillEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(
                    moreX + static_cast<float>(i) * 7.0f,
                    105.0f
                ),
                1.8f,
                1.8f
            ),
            textBrush_.Get()
        );
    }

    // Toolbar bottom separator.
    renderTarget_->DrawLine(
        D2D1::Point2F(20.0f, 145.0f),
        D2D1::Point2F(size.width - 20.0f, 145.0f),
        separatorBrush_.Get(),
        1.0f
    );

    // ---------------------------------------------------------
    // 3. Empty state / main content
    // ---------------------------------------------------------

    const float centerX = size.width * 0.5f;
    const float centerY = 285.0f;

    // Main capture illustration: screen outline.
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(
                centerX - 48.0f,
                centerY - 35.0f,
                centerX + 48.0f,
                centerY + 27.0f
            ),
            7.0f,
            7.0f
        ),
        borderBrush_.Get(),
        2.0f
    );

    // Capture corners.
    const float left = centerX - 34.0f;
    const float right = centerX + 34.0f;
    const float top = centerY - 21.0f;
    const float bottom = centerY + 13.0f;
    const float cornerLength = 11.0f;

    // Top-left.
    renderTarget_->DrawLine(
        D2D1::Point2F(left, top + cornerLength),
        D2D1::Point2F(left, top),
        accentBrush_.Get(),
        2.5f
    );
    renderTarget_->DrawLine(
        D2D1::Point2F(left, top),
        D2D1::Point2F(left + cornerLength, top),
        accentBrush_.Get(),
        2.5f
    );

    // Top-right.
    renderTarget_->DrawLine(
        D2D1::Point2F(right - cornerLength, top),
        D2D1::Point2F(right, top),
        accentBrush_.Get(),
        2.5f
    );
    renderTarget_->DrawLine(
        D2D1::Point2F(right, top),
        D2D1::Point2F(right, top + cornerLength),
        accentBrush_.Get(),
        2.5f
    );

    // Bottom-left.
    renderTarget_->DrawLine(
        D2D1::Point2F(left, bottom - cornerLength),
        D2D1::Point2F(left, bottom),
        accentBrush_.Get(),
        2.5f
    );
    renderTarget_->DrawLine(
        D2D1::Point2F(left, bottom),
        D2D1::Point2F(left + cornerLength, bottom),
        accentBrush_.Get(),
        2.5f
    );

    // Bottom-right.
    renderTarget_->DrawLine(
        D2D1::Point2F(right - cornerLength, bottom),
        D2D1::Point2F(right, bottom),
        accentBrush_.Get(),
        2.5f
    );
    renderTarget_->DrawLine(
        D2D1::Point2F(right, bottom),
        D2D1::Point2F(right, bottom - cornerLength),
        accentBrush_.Get(),
        2.5f
    );

    // Screen stand.
    renderTarget_->DrawLine(
        D2D1::Point2F(centerX, centerY + 27.0f),
        D2D1::Point2F(centerX, centerY + 39.0f),
        borderBrush_.Get(),
        2.0f
    );

    renderTarget_->DrawLine(
        D2D1::Point2F(centerX - 17.0f, centerY + 40.0f),
        D2D1::Point2F(centerX + 17.0f, centerY + 40.0f),
        borderBrush_.Get(),
        2.0f
    );

    // Main instruction.
    drawText(
        L"Press Windows logo key + Shift + S",
        messageTextFormat_.Get(),
        80.0f,
        centerY + 85.0f,
        size.width - 80.0f,
        centerY + 125.0f,
        textBrush_.Get()
    );

    // Supporting instruction.
    drawText(
        L"to start a snip, or select New to capture your screen.",
        captionTextFormat_.Get(),
        80.0f,
        centerY + 130.0f,
        size.width - 80.0f,
        centerY + 165.0f,
        mutedTextBrush_.Get()
    );
    // ---------------------------------------------------------
    // 4. Capture mode dropdown
    // ---------------------------------------------------------

    if (captureModeMenuOpen_) {

        constexpr float MENU_LEFT = 296.0f;
        constexpr float MENU_TOP = 136.0f;
        constexpr float MENU_RIGHT = 474.0f;
        constexpr float ROW_HEIGHT = 40.0f;

        // Menu background.
        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    MENU_LEFT,
                    MENU_TOP,
                    MENU_RIGHT,
                    MENU_TOP + 168.0f
                ),
                7.0f,
                7.0f
            ),
            buttonBrush_.Get()
        );

        const wchar_t* options[] = {
            L"Rectangle",
            L"Window",
            L"Full screen",
            L"Freeform"
        };

        for (int i = 0; i < 4; ++i) {

            const float rowTop =
                MENU_TOP + 4.0f + (i * ROW_HEIGHT);

            const bool selected =
                static_cast<int>(captureMode_) == i;

            // Highlight the currently selected mode.
            if (selected) {
                renderTarget_->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(
                            MENU_LEFT + 5.0f,
                            rowTop + 1.0f,
                            MENU_RIGHT - 5.0f,
                            rowTop + 39.0f
                        ),
                        4.0f,
                        4.0f
                    ),
                    borderBrush_.Get()
                );

                renderTarget_->FillRectangle(
                    D2D1::RectF(
                        MENU_LEFT + 8.0f,
                        rowTop + 11.0f,
                        MENU_LEFT + 11.0f,
                        rowTop + 29.0f
                    ),
                    accentBrush_.Get()
                );
            }

            drawText(
                options[i],
                buttonTextFormat_.Get(),
                MENU_LEFT + 23.0f,
                rowTop + 3.0f,
                MENU_RIGHT - 12.0f,
                rowTop + 37.0f,
                textBrush_.Get()
            );
        }

        // Menu outline.
        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    MENU_LEFT,
                    MENU_TOP,
                    MENU_RIGHT,
                    MENU_TOP + 168.0f
                ),
                7.0f,
                7.0f
            ),
            borderBrush_.Get(),
            1.0f
        );
    }
    // ---------------------------------------------------------
    // Capture delay dropdown
    // ---------------------------------------------------------

    if (delayMenuOpen_) {

        constexpr float MENU_LEFT = 583.0f;
        constexpr float MENU_TOP = 136.0f;
        constexpr float MENU_RIGHT = 761.0f;
        constexpr float ROW_HEIGHT = 40.0f;

        renderTarget_->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    MENU_LEFT,
                    MENU_TOP,
                    MENU_RIGHT,
                    MENU_TOP + 168.0f
                ),
                7.0f,
                7.0f
            ),
            buttonBrush_.Get()
        );

        const wchar_t* options[] = {
            L"No delay",
            L"3 seconds",
            L"5 seconds",
            L"10 seconds"
        };

        const unsigned int delays[] = {
            0, 3, 5, 10
        };

        for (int i = 0; i < 4; ++i) {

            const float rowTop =
                MENU_TOP + 4.0f + (i * ROW_HEIGHT);

            const bool selected =
                captureDelaySeconds_ == delays[i];

            if (selected) {
                renderTarget_->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(
                            MENU_LEFT + 5.0f,
                            rowTop + 1.0f,
                            MENU_RIGHT - 5.0f,
                            rowTop + 39.0f
                        ),
                        4.0f,
                        4.0f
                    ),
                    borderBrush_.Get()
                );

                renderTarget_->FillRectangle(
                    D2D1::RectF(
                        MENU_LEFT + 8.0f,
                        rowTop + 11.0f,
                        MENU_LEFT + 11.0f,
                        rowTop + 29.0f
                    ),
                    accentBrush_.Get()
                );
            }

            drawText(
                options[i],
                buttonTextFormat_.Get(),
                MENU_LEFT + 23.0f,
                rowTop + 3.0f,
                MENU_RIGHT - 12.0f,
                rowTop + 37.0f,
                textBrush_.Get()
            );
        }

        renderTarget_->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    MENU_LEFT,
                    MENU_TOP,
                    MENU_RIGHT,
                    MENU_TOP + 168.0f
                ),
                7.0f,
                7.0f
            ),
            borderBrush_.Get(),
            1.0f
        );
    }

    const HRESULT result = renderTarget_->EndDraw();

    if (result == D2DERR_RECREATE_TARGET) {
        textBrush_.Reset();
        mutedTextBrush_.Reset();
        accentBrush_.Reset();
        buttonBrush_.Reset();
        borderBrush_.Reset();
        separatorBrush_.Reset();
        renderTarget_.Reset();
    }
    else if (FAILED(result)) {
        OutputDebugStringW(L"Direct2D rendering failed.\n");
    }
}

void WindowRenderer::Resize(
    UINT width,
    UINT height
) {
    if (!renderTarget_ || width == 0 || height == 0) {
        return;
    }

    const HRESULT result = renderTarget_->Resize(
        D2D1::SizeU(width, height)
    );

    if (result == D2DERR_RECREATE_TARGET) {
        textBrush_.Reset();
        mutedTextBrush_.Reset();
        accentBrush_.Reset();
        buttonBrush_.Reset();
        borderBrush_.Reset();
        separatorBrush_.Reset();
        renderTarget_.Reset();
    }
    else if (FAILED(result)) {
        OutputDebugStringW(
            L"Direct2D render target resize failed.\n"
        );
    }
}

void WindowRenderer::HandleMouseButtonUp(float x, float y) {
    if (!renderTarget_ || !window_) {
        return;
    }

    // Convert Windows client pixels into Direct2D DIPs.
    FLOAT dpiX = 96.0f;
    FLOAT dpiY = 96.0f;

    renderTarget_->GetDpi(&dpiX, &dpiY);

    if (dpiX > 0.0f) {
        x *= 96.0f / dpiX;
    }

    if (dpiY > 0.0f) {
        y *= 96.0f / dpiY;
    }

    bool changed = false;
    bool menuItemHandled = false;

    // Capture-mode menu selection.
    if (captureModeMenuOpen_) {

        if (x >= 296.0f && x <= 474.0f &&
            y >= 140.0f && y < 300.0f) {

            const int index =
                static_cast<int>((y - 140.0f) / 40.0f);

            captureMode_ = static_cast<CaptureMode>(index);

            menuItemHandled = true;
        }

        captureModeMenuOpen_ = false;
        changed = true;
    }

    // Delay-menu selection.
    if (delayMenuOpen_) {

        if (x >= 583.0f && x <= 761.0f &&
            y >= 140.0f && y < 300.0f) {

            const int index =
                static_cast<int>((y - 140.0f) / 40.0f);

            constexpr unsigned int delays[] = {
                0, 3, 5, 10
            };

            captureDelaySeconds_ = delays[index];

            menuItemHandled = true;
        }

        delayMenuOpen_ = false;
        changed = true;
    }

    // Process toolbar actions if the click did not select
    // an item from an already-open dropdown.
    if (!menuItemHandled) {

        // Screenshot mode.
        if (y >= 82.0f && y <= 128.0f &&
            x >= 169.0f && x <= 217.0f) {

            videoModeSelected_ = false;
            captureModeMenuOpen_ = false;
            delayMenuOpen_ = false;
            changed = true;
        }

        // Video mode.
        else if (y >= 82.0f && y <= 128.0f &&
                 x >= 225.0f && x <= 267.0f) {

            videoModeSelected_ = true;
            captureModeMenuOpen_ = false;
            delayMenuOpen_ = false;
            changed = true;
        }

        // Capture-mode dropdown.
        else if (y >= 82.0f && y <= 128.0f &&
                 x >= 296.0f && x <= 450.0f) {

            captureModeMenuOpen_ = !captureModeMenuOpen_;
            delayMenuOpen_ = false;
            changed = true;
        }

        // Capture-delay dropdown.
        else if (y >= 82.0f && y <= 128.0f &&
                 x >= 466.0f && x <= 610.0f) {

            delayMenuOpen_ = !delayMenuOpen_;
            captureModeMenuOpen_ = false;
            changed = true;
        }
    }

    if (changed) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void WindowRenderer::Shutdown() noexcept {
    textBrush_.Reset();
    mutedTextBrush_.Reset();
    accentBrush_.Reset();
    buttonBrush_.Reset();
    borderBrush_.Reset();
    separatorBrush_.Reset();

    renderTarget_.Reset();

    textFormat_.Reset();
    buttonTextFormat_.Reset();
    messageTextFormat_.Reset();
    captionTextFormat_.Reset();

    d2dFactory_ = nullptr;
    window_ = nullptr;
}

bool WindowRenderer::IsInitialized() const noexcept {
    return
        window_ != nullptr &&
        d2dFactory_ != nullptr &&
        renderTarget_ != nullptr &&
        textBrush_ != nullptr &&
        mutedTextBrush_ != nullptr &&
        accentBrush_ != nullptr &&
        buttonBrush_ != nullptr &&
        borderBrush_ != nullptr &&
        separatorBrush_ != nullptr &&
        textFormat_ != nullptr &&
        buttonTextFormat_ != nullptr &&
        messageTextFormat_ != nullptr &&
        captionTextFormat_ != nullptr;
}

} // namespace windows_snip