#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <iostream>
#include <string>
#include <iomanip>

using Microsoft::WRL::ComPtr;

std::wstring TakeBstr(BSTR value) {
    if (!value) {
        return {};
    }

    std::wstring result(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}

int RunProbe() {
    ComPtr<IUIAutomation> automation;

    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(automation.GetAddressOf())
    );

    if (FAILED(hr)) {
        std::wcerr << L"UI Automation initialization failed: 0x"
                   << std::hex << hr << L"\n";
        return 1;
    }

    ComPtr<IUIAutomationTreeWalker> walker;

    hr = automation->get_ControlViewWalker(
        walker.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::wcerr << L"Could not obtain UI Automation tree walker.\n";
        return 1;
    }

    std::wcout
        << L"\n=== WINDOWS SCROLL CAPTURE - UIA PROBE ===\n\n"
        << L"Move the mouse over the content you want to scroll.\n"
        << L"Return to this console and press ENTER.\n";

    std::wstring input;
    std::getline(std::wcin, input);

    POINT point{};

    if (!GetCursorPos(&point)) {
        std::wcerr << L"Could not retrieve cursor position.\n";
        return 1;
    }

    std::wcout << L"\nCursor: (" << point.x
               << L", " << point.y << L")\n\n";

    ComPtr<IUIAutomationElement> current;

    hr = automation->ElementFromPoint(
        point,
        current.GetAddressOf()
    );

    if (FAILED(hr) || !current) {
        std::wcerr << L"Could not identify the element under cursor.\n";
        return 1;
    }

    int scrollPatternCount = 0;

    for (int depth = 0; current && depth < 20; ++depth) {
        BSTR rawName = nullptr;
        BSTR rawClass = nullptr;
        CONTROLTYPEID controlType = 0;

        std::wstring name;
        std::wstring className;

        if (SUCCEEDED(current->get_CurrentName(&rawName))) {
            name = TakeBstr(rawName);
        }

        if (SUCCEEDED(current->get_CurrentClassName(&rawClass))) {
            className = TakeBstr(rawClass);
        }

        current->get_CurrentControlType(&controlType);

        std::wcout << L"[" << depth << L"] "
                   << L"Name: "
                   << (name.empty() ? L"(unnamed)" : name)
                   << L"\n    Class: " << className
                   << L"\n    Control type: " << controlType
                   << L"\n";

        ComPtr<IUIAutomationScrollPattern> scrollPattern;

        HRESULT patternHr = current->GetCurrentPatternAs(
            UIA_ScrollPatternId,
            IID_PPV_ARGS(scrollPattern.GetAddressOf())
        );

        if (SUCCEEDED(patternHr) && scrollPattern) {
            BOOL horizontal = FALSE;
            BOOL vertical = FALSE;

            scrollPattern->get_CurrentHorizontallyScrollable(
                &horizontal
            );

            scrollPattern->get_CurrentVerticallyScrollable(
                &vertical
            );

            std::wcout
                << L"    *** SCROLL PATTERN FOUND ***\n"
                << L"    Horizontal: "
                << (horizontal ? L"Yes" : L"No")
                << L"\n    Vertical: "
                << (vertical ? L"Yes" : L"No")
                << L"\n";

            if (vertical) {
                double position = -1.0;
                double viewSize = 0.0;

                scrollPattern->get_CurrentVerticalScrollPercent(
                    &position
                );

                scrollPattern->get_CurrentVerticalViewSize(
                    &viewSize
                );

                std::wcout
                    << L"    Vertical position: "
                    << position << L"%\n"
                    << L"    Vertical view size: "
                    << viewSize << L"%\n";
            }

            ++scrollPatternCount;
        }
        else {
            std::wcout << L"    Scroll pattern: Not exposed\n";
        }

        std::wcout << L"\n";

        ComPtr<IUIAutomationElement> parent;

        hr = walker->GetParentElement(
            current.Get(),
            parent.GetAddressOf()
        );

        if (FAILED(hr) || !parent) {
            break;
        }

        current = parent;
    }

    std::wcout << L"====================================\n";

    std::wcout << L"Scroll-capable elements found: "
               << scrollPatternCount << L"\n";

    if (scrollPatternCount == 0) {
        std::wcout
            << L"No Scroll Pattern was exposed at this location.\n"
            << L"We will evaluate an alternative scrolling method.\n";
    }

    return 0;
}

int wmain() {
    HRESULT hr = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    if (FAILED(hr)) {
        std::wcerr << L"COM initialization failed.\n";
        return 1;
    }

    int result = RunProbe();

    CoUninitialize();

    return result;
}