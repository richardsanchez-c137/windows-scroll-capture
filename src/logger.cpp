#include "core/logger.hpp"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace windows_snip {

namespace {

const char* getLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:    return "TRACE";
    case LogLevel::Info:     return "INFO";
    case LogLevel::Warning:  return "WARNING";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    }

    return "UNKNOWN";
}

} // namespace

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(std::wstring& error) {
    std::lock_guard<std::mutex> lock(mutex_);

    error.clear();

    if (initialized_) {
        return true;
    }

    // --------------------------------------------------------
    // 1. Locate the local application data directory.
    // --------------------------------------------------------

    const DWORD required = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        nullptr,
        0
    );

    if (required == 0) {
        error = L"Could not locate the LOCALAPPDATA directory.";
        return false;
    }

    std::vector<wchar_t> pathBuffer(required);

    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        pathBuffer.data(),
        required
    );

    if (written == 0 || written >= required) {
        error = L"Could not read the LOCALAPPDATA directory.";
        return false;
    }

    try {
        // ----------------------------------------------------
        // 2. Create the application log directory.
        // ----------------------------------------------------

        const std::filesystem::path logDirectory =
            std::filesystem::path(pathBuffer.data()) /
            L"WindowsSnipTool" /
            L"Logs";

        std::error_code filesystemError;

        std::filesystem::create_directories(
            logDirectory,
            filesystemError
        );

        if (filesystemError) {
            error = L"Could not create the application log directory.";
            return false;
        }

        // ----------------------------------------------------
        // 3. Open the log file.
        // ----------------------------------------------------

        logFilePath_ =
            logDirectory / L"windows-snip-tool.log";

        file_.open(
            logFilePath_,
            std::ios::out |
            std::ios::app |
            std::ios::binary
        );

        if (!file_.is_open() || !file_.good()) {
            error = L"Could not open the application log file.";
            return false;
        }

        initialized_ = true;

        file_
            << "\n========== Windows Snip Tool session started ==========\n";

        file_.flush();

        return file_.good();

    }
    catch (const std::filesystem::filesystem_error&) {
        error = L"A filesystem error occurred while initializing logging.";
        return false;
    }
    catch (...) {
        error = L"An unexpected error occurred while initializing logging.";
        return false;
    }
}

void Logger::Write(
    LogLevel level,
    std::wstring_view message
) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_ || !file_.is_open()) {
            return;
        }

        // Get the current local timestamp.
        const auto now =
            std::chrono::system_clock::now();

        const std::time_t currentTime =
            std::chrono::system_clock::to_time_t(now);

        std::tm localTime{};

        if (localtime_s(&localTime, &currentTime) != 0) {
            return;
        }

        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;

        std::ostringstream record;

        record
            << '['
            << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
            << '.'
            << std::setw(3)
            << std::setfill('0')
            << milliseconds.count()
            << "] ["
            << getLevelName(level)
            << "] "
            << ToUtf8(message)
            << '\n';

        const std::string output = record.str();

        file_.write(
            output.data(),
            static_cast<std::streamsize>(output.size())
        );

        file_.flush();

    }
    catch (...) {
        // Logging must never crash the application.
    }
}

void Logger::Shutdown() noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);

        if (file_.is_open()) {
            file_
                << "========== Windows Snip Tool session ended ==========\n";

            file_.flush();
            file_.close();
        }

        initialized_ = false;

    }
    catch (...) {
        // Shutdown must remain safe during application teardown.
    }
}

std::filesystem::path Logger::GetLogFilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logFilePath_;
}

bool Logger::IsInitialized() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

std::string Logger::ToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    if (value.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<int>::max)()
        )) {
        throw std::length_error("Log message is too large.");
    }

    const int sourceLength =
        static_cast<int>(value.size());

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        sourceLength,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (required <= 0) {
        throw std::runtime_error("UTF-8 conversion failed.");
    }

    std::string result(
        static_cast<std::size_t>(required),
        '\0'
    );

    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        sourceLength,
        result.data(),
        required,
        nullptr,
        nullptr
    );

    if (written != required) {
        throw std::runtime_error("UTF-8 conversion was incomplete.");
    }

    return result;
}

} // namespace windows_snip
