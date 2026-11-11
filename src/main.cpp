
#include <windows.h>
#include <appmodel.h>
#include <string>
#include "region_selector.hpp"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include "scroll_controller.hpp"


// Safely retrieve a query parameter from the callback URI.
std::wstring getQueryValue(
    const winrt::Windows::Foundation::Uri& uri,
    const wchar_t* name
) {
    try {
        const auto query = uri.QueryParsed();

        return query.GetFirstValueByName(name).c_str();
    }
    catch (const winrt::hresult_error&) {
        return {};
    }
}

// Display the Snipping Tool callback result.

void handleCaptureResponse(
    const winrt::Windows::Foundation::Uri& uri
) {
    const std::wstring code =
        getQueryValue(uri, L"code");

    const std::wstring reason =
        getQueryValue(uri, L"reason");

    const std::wstring fileToken =
        getQueryValue(uri, L"file-access-token");

    std::wstring message;

    if (code == L"200" && !fileToken.empty()) {
        try {
            // Redeem the one-time token to obtain the captured image.
            const auto capturedFile =
                winrt::Windows::ApplicationModel::DataTransfer::
                    SharedStorageAccessManager::
                        RedeemTokenForFileAsync(fileToken).get();

            // Get this application's persistent local storage folder.
            const auto localFolder =
                winrt::Windows::Storage::ApplicationData::
                    Current().LocalFolder();

            // Copy the captured image into our application storage.
            const auto savedFile = capturedFile.CopyAsync(
                localFolder,
                capturedFile.Name(),
                winrt::Windows::Storage::
                    NameCollisionOption::GenerateUniqueName
            ).get();

            message =
                L"Screenshot captured and saved successfully!\n\n";

            message += L"File name:\n";
            message += savedFile.Name().c_str();

            message += L"\n\nSaved location:\n";
            message += savedFile.Path().c_str();
        }
        catch (const winrt::hresult_error& error) {
            message =
                L"Capture succeeded, but saving the image failed.\n\n";

            message += error.message().c_str();
        }
    }
    else {
        message = L"Snipping Tool capture failed or was cancelled.\n\n";

        message += L"Status code: ";
        message += code.empty() ? L"Unknown" : code;

        message += L"\n\nReason: ";
        message += reason.empty() ? L"Not provided" : reason;
    }

    MessageBoxW(
        nullptr,
        message.c_str(),
        L"Windows Scroll Capture - Screenshot Result",
        MB_OK | MB_ICONINFORMATION
    );
}


// Launch Snipping Tool using the packaged application's identity.
bool launchSnippingTool() {

    const winrt::Windows::Foundation::Uri captureUri{
        L"ms-screenclip://capture/image"
        L"?rectangle"
        L"&enabledModes=SnippingAllModes"
        L"&user-agent=WindowsScrollCapture"
        L"&api-version=1.2"
        L"&redirect-uri=scroller://capture-response"
    };

    return winrt::Windows::System::Launcher::
        LaunchUriAsync(captureUri).get();
}

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int
) {
    try {
        // Establish physical screen-coordinate awareness before
        // creating any windows or performing screen capture.
        if (!SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        )) {
            const DPI_AWARENESS awareness =
                GetAwarenessFromDpiAwarenessContext(
                    GetThreadDpiAwarenessContext()
                );

            if (awareness != DPI_AWARENESS_PER_MONITOR_AWARE) {
                MessageBoxW(
                    nullptr,
                    L"Could not establish Per-Monitor DPI awareness.",
                    L"Windows Scroll Capture - DPI Error",
                    MB_OK | MB_ICONERROR
                );

                return 1;
            }
        }

        winrt::init_apartment(
            winrt::apartment_type::multi_threaded
        );


// Detect whether this process has MSIX package identity.
UINT32 packageNameLength = 0;

const LONG packageStatus =
    GetCurrentPackageFullName(&packageNameLength, nullptr);

const bool isPackaged =
    packageStatus == ERROR_INSUFFICIENT_BUFFER;

if (
    packageStatus != ERROR_INSUFFICIENT_BUFFER &&
    packageStatus != APPMODEL_ERROR_NO_PACKAGE
) {
    throw winrt::hresult_error(
        HRESULT_FROM_WIN32(packageStatus)
    );
}

// Only retrieve WinRT activation arguments when packaged.
if (isPackaged) {

    const auto activationArgs =
        winrt::Windows::ApplicationModel::AppInstance::
            GetActivatedEventArgs();

    if (
        activationArgs &&
        activationArgs.Kind() ==
            winrt::Windows::ApplicationModel::Activation::
                ActivationKind::Protocol
    ) {
        const auto protocolArgs = activationArgs.try_as<
            winrt::Windows::ApplicationModel::Activation::
                IProtocolActivatedEventArgs
        >();

        if (protocolArgs) {
            handleCaptureResponse(protocolArgs.Uri());
            return 0;
        }

        MessageBoxW(
            nullptr,
            L"Protocol activation arguments could not be retrieved.",
            L"Windows Scroll Capture - Error",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }
}


    RECT selectedRegion{};

    if (selectCaptureRegion(selectedRegion)) {



        runScrollController(selectedRegion);
    }

    return 0;
    }
    catch (const winrt::hresult_error& error) {
        std::wstring message =
            L"Windows Runtime error:\n\n";

        message += error.message().c_str();

        MessageBoxW(
            nullptr,
            message.c_str(),
            L"Windows Scroll Capture - Error",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }
    catch (...) {
        MessageBoxW(
            nullptr,
            L"An unexpected error occurred.",
            L"Windows Scroll Capture",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }
}
