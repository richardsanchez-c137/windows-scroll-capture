
#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace windows_snip {

enum class LogLevel {
    Trace,
    Info,
    Warning,
    Error,
    Critical
};

class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Initialize the application log file.
    bool Initialize(std::wstring& error);

    // Write a log message.
    void Write(
        LogLevel level,
        std::wstring_view message
    ) noexcept;

    // Close the log file.
    void Shutdown() noexcept;

    // Return the current log file location.
    std::filesystem::path GetLogFilePath() const;

    bool IsInitialized() const noexcept;

private:
    static std::string ToUtf8(std::wstring_view value);

    mutable std::mutex mutex_;

    std::ofstream file_;

    std::filesystem::path logFilePath_;

    bool initialized_ = false;
};

} // namespace windows_snip
