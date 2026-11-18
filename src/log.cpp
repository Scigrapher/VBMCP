#include "log.h"
#include "config.h"

#include <cstdio>

namespace mcpaddin {

std::wstring Logger::path_;
std::string Logger::minLevel_ = "info";
bool Logger::requests_ = true;
bool Logger::initialized_ = false;

std::string Logger::Timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buffer[64] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

std::wstring Logger::TimestampW() {
    return Utf8ToWide(Timestamp());
}

void Logger::Initialize(const ServerConfig& config) {
    path_ = config.logPath;
    requests_ = config.logRequests;
    initialized_ = true;
    SetDebugLogEnabled(config.debugLog);
}

void Logger::Shutdown() {
    initialized_ = false;
    path_.clear();
}

std::wstring Logger::Path() {
    return path_;
}

void Logger::Info(const std::string& message) {
    Write("INFO", message);
}

void Logger::Warn(const std::string& message) {
    Write("WARN", message);
}

void Logger::Error(const std::string& message) {
    Write("ERROR", message);
}

void Logger::Request(const std::string& message) {
    if (!requests_) return;
    Write("HTTP", message);
}

void Logger::Write(const char* level, const std::string& message) {
    if (!initialized_ || path_.empty()) {
        LogDebug("%s %s", level, message.c_str());
        return;
    }
    if (FileSize(path_) > 5 * 1024 * 1024) {
        std::wstring oldPath = path_ + L".1";
        DeleteFileQuiet(oldPath);
        MoveFileExW(path_.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    std::string line = Timestamp() + " [" + level + "] " + message;
    AppendLogLine(path_, line);
    LogDebug("%s", line.c_str());
}

void LogInfo(const ServerConfig& config, const std::string& message) {
    if (config.logPath.empty()) {
        LogDebug("INFO %s", message.c_str());
    } else {
        std::string line = Logger::Timestamp() + " [INFO] " + message;
        AppendLogLine(config.logPath, line);
    }
}

void LogWarn(const ServerConfig& config, const std::string& message) {
    if (config.logPath.empty()) {
        LogDebug("WARN %s", message.c_str());
    } else {
        std::string line = Logger::Timestamp() + " [WARN] " + message;
        AppendLogLine(config.logPath, line);
    }
}

void LogError(const ServerConfig& config, const std::string& message) {
    if (config.logPath.empty()) {
        LogDebug("ERROR %s", message.c_str());
    } else {
        std::string line = Logger::Timestamp() + " [ERROR] " + message;
        AppendLogLine(config.logPath, line);
    }
}

} // namespace mcpaddin
