#pragma once

#include "common.h"

namespace mcpaddin {

struct ServerConfig {
    std::wstring configPath;
    std::wstring logPath;
    std::wstring dataDirectory;

    std::string bindAddress;
    int httpPort;
    int httpsPort;
    bool enableHttp;
    bool enableHttps;

    std::string authToken;
    bool allowUnauthenticatedRemote;
    bool allowQueryToken;
    bool logRequests;
    std::string corsOrigin;

    size_t maxBodyBytes;
    int requestTimeoutSeconds;
    int ideTimeoutSeconds;
    int maxConnections;
    int keepAliveSeconds;

    std::wstring certificateFile;
    std::string certificatePassword;
    std::wstring certificateStore;
    std::wstring certificateThumbprint;

    std::wstring vb6Exe;
    std::wstring sourceEncoding;
    bool debugLog;

    std::vector<std::wstring> allowedRoots;

    ServerConfig();
};

class ConfigManager {
public:
    static bool Load(ServerConfig& config, std::wstring* error = NULL);
    static bool Save(const ServerConfig& config, std::wstring* error = NULL);
    static std::wstring DefaultConfigPath();
    static bool GenerateToken(std::string& token);
    static bool IsLoopbackBind(const std::string& address);
};

} // namespace mcpaddin
