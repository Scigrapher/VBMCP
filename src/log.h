#pragma once

#include "common.h"

namespace mcpaddin {

struct ServerConfig;

class Logger {
public:
    static void Initialize(const ServerConfig& config);
    static void Shutdown();
    static void Info(const std::string& message);
    static void Warn(const std::string& message);
    static void Error(const std::string& message);
    static void Request(const std::string& message);
    static std::string Timestamp();
    static std::wstring TimestampW();
    static std::wstring Path();

private:
    static void Write(const char* level, const std::string& message);
    static std::wstring path_;
    static std::string minLevel_;
    static bool requests_;
    static bool initialized_;
};

void LogInfo(const ServerConfig& config, const std::string& message);
void LogWarn(const ServerConfig& config, const std::string& message);
void LogError(const ServerConfig& config, const std::string& message);

} // namespace mcpaddin
