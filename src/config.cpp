#include "config.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace mcpaddin {

static std::wstring IntToWString(long value) {
    wchar_t buffer[32] = {0};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%ld", value);
    return buffer;
}

ServerConfig::ServerConfig()
    : bindAddress("0.0.0.0"),
      httpPort(8765),
      httpsPort(8766),
      enableHttp(true),
      enableHttps(false),
      allowUnauthenticatedRemote(false),
      allowQueryToken(false),
      logRequests(true),
      maxBodyBytes(16 * 1024 * 1024),
      requestTimeoutSeconds(120),
      ideTimeoutSeconds(300),
      maxConnections(64),
      keepAliveSeconds(30),
      sourceEncoding(L"auto"),
      debugLog(false) {
}

static std::string ReadIniString(const std::wstring& path, const wchar_t* section,
                                 const wchar_t* key, const std::string& defaultValue) {
    std::vector<wchar_t> buffer(4096);
    for (;;) {
        DWORD copied = GetPrivateProfileStringW(section, key, L"",
                                                &buffer[0], (DWORD)buffer.size(), path.c_str());
        if (copied < buffer.size() - 1) {
            return copied == 0 ? defaultValue : WideToUtf8(&buffer[0]);
        }
        buffer.resize(buffer.size() * 2);
    }
}

static std::wstring ReadIniWString(const std::wstring& path, const wchar_t* section,
                                   const wchar_t* key, const std::wstring& defaultValue) {
    std::vector<wchar_t> buffer(4096);
    for (;;) {
        DWORD copied = GetPrivateProfileStringW(section, key, L"",
                                                &buffer[0], (DWORD)buffer.size(), path.c_str());
        if (copied < buffer.size() - 1) {
            return copied == 0 ? defaultValue : std::wstring(&buffer[0], copied);
        }
        buffer.resize(buffer.size() * 2);
    }
}

static int ReadIniInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int defaultValue) {
    std::wstring value = ReadIniWString(path, section, key, L"");
    if (value.empty()) return defaultValue;
    wchar_t* end = NULL;
    long parsed = wcstol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != 0) return defaultValue;
    return (int)parsed;
}

static bool ReadIniBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultValue) {
    std::string value = ToLowerAscii(Trim(ReadIniString(path, section, key, "")));
    if (value.empty()) return defaultValue;
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    return defaultValue;
}

static bool WriteIniString(const std::wstring& path, const wchar_t* section,
                           const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
}

static bool WriteIniString(const std::wstring& path, const wchar_t* section,
                           const wchar_t* key, const std::string& value) {
    return WritePrivateProfileStringW(section, key, Utf8ToWide(value).c_str(), path.c_str()) != FALSE;
}

static bool ParseIntStrict(const std::string& value, int& out) {
    std::string trimmed = Trim(value);
    if (trimmed.empty()) return false;
    char* end = NULL;
    long parsed = strtol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || *end != 0) return false;
    out = (int)parsed;
    return true;
}

std::wstring ConfigManager::DefaultConfigPath() {
    std::wstring overridePath = GetEnvVar(L"MCPADDIN_CONFIG");
    if (!overridePath.empty()) return GetFullPath(overridePath);
    return PathJoin(PathJoin(GetAppDataDirectory(), L"MCPAddin"), L"mcpaddin.ini");
}

bool ConfigManager::IsLoopbackBind(const std::string& address) {
    std::string value = ToLowerAscii(Trim(address));
    return value == "127.0.0.1" || value == "localhost" || value == "::1" || value == "[::1]";
}

bool ConfigManager::GenerateToken(std::string& token) {
    std::vector<unsigned char> bytes(32);
    BOOLEAN (WINAPI* genRandom)(PVOID, ULONG) = NULL;
    HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
    if (!advapi) advapi = LoadLibraryW(L"advapi32.dll");
    if (advapi) {
        genRandom = (BOOLEAN (WINAPI*)(PVOID, ULONG))GetProcAddress(advapi, "SystemFunction036");
    }
    bool ok = false;
    if (genRandom) {
        ok = genRandom(&bytes[0], (ULONG)bytes.size()) != FALSE;
    }
    if (!ok) {
        GUID guid;
        FILETIME ft = FileTimeNow();
        DWORD pid = GetCurrentProcessId();
        DWORD tick = GetTickCount();
        if (SUCCEEDED(CoCreateGuid(&guid))) {
            const unsigned char* g = (const unsigned char*)&guid;
            const unsigned char* t = (const unsigned char*)&ft;
            const unsigned char* p = (const unsigned char*)&pid;
            const unsigned char* k = (const unsigned char*)&tick;
            for (size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = (unsigned char)(g[i % sizeof(guid)] ^ t[i % sizeof(ft)] ^
                                           p[i % sizeof(pid)] ^ k[i % sizeof(tick)] ^ (unsigned char)(i * 37));
            }
            ok = true;
        }
    }
    if (!ok) return false;
    token = HexEncode(bytes);
    return true;
}

bool ConfigManager::Load(ServerConfig& config, std::wstring* error) {
    config = ServerConfig();
    config.configPath = DefaultConfigPath();
    config.dataDirectory = PathDirectory(config.configPath);
    config.logPath = PathJoin(config.dataDirectory, L"mcpaddin.log");
    if (!EnsureDirectory(config.dataDirectory)) {
        if (error) *error = L"Cannot create configuration directory: " + config.dataDirectory;
        return false;
    }

    bool created = !FileExists(config.configPath);
    if (created) {
        WriteIniString(config.configPath, L"server", L"bindAddress", config.bindAddress);
        WriteIniString(config.configPath, L"server", L"httpPort", L"8765");
        WriteIniString(config.configPath, L"server", L"httpsPort", L"8766");
        WriteIniString(config.configPath, L"server", L"enableHttp", L"1");
        WriteIniString(config.configPath, L"server", L"enableHttps", L"0");
        WriteIniString(config.configPath, L"server", L"allowUnauthenticatedRemote", L"0");
        WriteIniString(config.configPath, L"server", L"allowQueryToken", L"0");
        WriteIniString(config.configPath, L"server", L"logRequests", L"1");
        WriteIniString(config.configPath, L"server", L"maxBodyBytes", L"16777216");
        WriteIniString(config.configPath, L"server", L"requestTimeoutSeconds", L"120");
        WriteIniString(config.configPath, L"server", L"ideTimeoutSeconds", L"300");
        WriteIniString(config.configPath, L"server", L"maxConnections", L"64");
        WriteIniString(config.configPath, L"server", L"keepAliveSeconds", L"30");
        WriteIniString(config.configPath, L"tls", L"certificateFile", L"");
        WriteIniString(config.configPath, L"tls", L"certificatePassword", L"");
        WriteIniString(config.configPath, L"tls", L"certificateStore", L"MY");
        WriteIniString(config.configPath, L"tls", L"certificateThumbprint", L"");
        WriteIniString(config.configPath, L"ide", L"vb6Exe", L"");
        WriteIniString(config.configPath, L"ide", L"sourceEncoding", L"auto");
        WriteIniString(config.configPath, L"diagnostics", L"debugLog", L"0");
    }

    config.bindAddress = ReadIniString(config.configPath, L"server", L"bindAddress", config.bindAddress);
    config.httpPort = ReadIniInt(config.configPath, L"server", L"httpPort", config.httpPort);
    config.httpsPort = ReadIniInt(config.configPath, L"server", L"httpsPort", config.httpsPort);
    config.enableHttp = ReadIniBool(config.configPath, L"server", L"enableHttp", config.enableHttp);
    config.enableHttps = ReadIniBool(config.configPath, L"server", L"enableHttps", config.enableHttps);
    config.allowUnauthenticatedRemote = ReadIniBool(config.configPath, L"server",
                                                    L"allowUnauthenticatedRemote",
                                                    config.allowUnauthenticatedRemote);
    config.allowQueryToken = ReadIniBool(config.configPath, L"server", L"allowQueryToken", config.allowQueryToken);
    config.logRequests = ReadIniBool(config.configPath, L"server", L"logRequests", config.logRequests);
    config.corsOrigin = ReadIniString(config.configPath, L"server", L"corsOrigin", config.corsOrigin);
    config.maxBodyBytes = (size_t)ReadIniInt(config.configPath, L"server", L"maxBodyBytes",
                                             (int)config.maxBodyBytes);
    config.requestTimeoutSeconds = ReadIniInt(config.configPath, L"server", L"requestTimeoutSeconds",
                                              config.requestTimeoutSeconds);
    config.ideTimeoutSeconds = ReadIniInt(config.configPath, L"server", L"ideTimeoutSeconds",
                                          config.ideTimeoutSeconds);
    config.maxConnections = ReadIniInt(config.configPath, L"server", L"maxConnections", config.maxConnections);
    config.keepAliveSeconds = ReadIniInt(config.configPath, L"server", L"keepAliveSeconds",
                                         config.keepAliveSeconds);

    config.certificateFile = ReadIniWString(config.configPath, L"tls", L"certificateFile", L"");
    config.certificatePassword = ReadIniString(config.configPath, L"tls", L"certificatePassword", "");
    config.certificateStore = ReadIniWString(config.configPath, L"tls", L"certificateStore", L"MY");
    config.certificateThumbprint = ReadIniWString(config.configPath, L"tls", L"certificateThumbprint", L"");

    config.vb6Exe = ReadIniWString(config.configPath, L"ide", L"vb6Exe", L"");
    config.sourceEncoding = ReadIniWString(config.configPath, L"ide", L"sourceEncoding", L"auto");
    config.debugLog = ReadIniBool(config.configPath, L"diagnostics", L"debugLog", false);

    config.authToken = ReadIniString(config.configPath, L"auth", L"token", "");
    std::wstring allowedRoots = ReadIniWString(config.configPath, L"filesystem", L"allowedRoots", L"");
    size_t start = 0;
    while (start <= allowedRoots.size()) {
        size_t end = allowedRoots.find(L';', start);
        if (end == std::wstring::npos) end = allowedRoots.size();
        std::wstring item = Trim(allowedRoots.substr(start, end - start));
        if (!item.empty()) config.allowedRoots.push_back(GetFullPath(item));
        if (end == allowedRoots.size()) break;
        start = end + 1;
    }

    std::wstring envToken = GetEnvVar(L"MCPADDIN_TOKEN");
    if (!envToken.empty()) config.authToken = WideToUtf8(envToken);
    std::wstring envBind = GetEnvVar(L"MCPADDIN_BIND");
    if (!envBind.empty()) config.bindAddress = WideToUtf8(envBind);
    std::wstring envHttp = GetEnvVar(L"MCPADDIN_HTTP_PORT");
    int parsedPort = 0;
    if (!envHttp.empty() && ParseIntStrict(WideToUtf8(envHttp), parsedPort)) config.httpPort = parsedPort;
    std::wstring envHttps = GetEnvVar(L"MCPADDIN_HTTPS_PORT");
    if (!envHttps.empty() && ParseIntStrict(WideToUtf8(envHttps), parsedPort)) config.httpsPort = parsedPort;
    std::wstring envLog = GetEnvVar(L"MCPADDIN_LOG");
    if (!envLog.empty()) config.logPath = GetFullPath(envLog);

    if (config.authToken.empty()) {
        if (!GenerateToken(config.authToken)) {
            if (error) *error = L"Could not generate a secure authentication token.";
            return false;
        }
        if (!WriteIniString(config.configPath, L"auth", L"token", config.authToken)) {
            if (error) *error = L"Could not persist the generated authentication token.";
            return false;
        }
    }
    if (!config.authToken.empty() && config.authToken.size() < 16) {
        if (error) *error = L"Authentication tokens must contain at least 16 characters.";
        return false;
    }
    if (config.corsOrigin.find('\r') != std::string::npos ||
        config.corsOrigin.find('\n') != std::string::npos) {
        if (error) *error = L"corsOrigin cannot contain line breaks.";
        return false;
    }

    if (config.maxBodyBytes < 1024) config.maxBodyBytes = 1024;
    if (config.maxBodyBytes > 64 * 1024 * 1024) config.maxBodyBytes = 64 * 1024 * 1024;
    if (config.maxConnections < 1) config.maxConnections = 1;
    if (config.maxConnections > 512) config.maxConnections = 512;
    if (config.keepAliveSeconds < 1) config.keepAliveSeconds = 1;
    if (config.requestTimeoutSeconds < 5) config.requestTimeoutSeconds = 5;
    if (config.ideTimeoutSeconds < 10) config.ideTimeoutSeconds = 10;

    if ((!config.enableHttp && !config.enableHttps) ||
        (config.enableHttp && (config.httpPort < 1 || config.httpPort > 65535)) ||
        (config.enableHttps && (config.httpsPort < 1 || config.httpsPort > 65535))) {
        if (error) *error = L"Invalid server port configuration.";
        return false;
    }
    if (config.enableHttp && config.enableHttps && config.httpPort == config.httpsPort) {
        if (error) *error = L"HTTP and HTTPS cannot use the same port.";
        return false;
    }
    if (!IsLoopbackBind(config.bindAddress) && config.authToken.empty() &&
        !config.allowUnauthenticatedRemote) {
        if (error) *error = L"Refusing remote unauthenticated exposure. Set an auth token.";
        return false;
    }

    if (created) {
        LogInfo(config, "Created configuration: " + WideToUtf8(config.configPath));
    }
    return true;
}

bool ConfigManager::Save(const ServerConfig& config, std::wstring* error) {
    if (!EnsureDirectory(PathDirectory(config.configPath))) {
        if (error) *error = L"Cannot create configuration directory.";
        return false;
    }
    WriteIniString(config.configPath, L"server", L"bindAddress", config.bindAddress);
    WriteIniString(config.configPath, L"server", L"httpPort", IntToWString(config.httpPort));
    WriteIniString(config.configPath, L"server", L"httpsPort", IntToWString(config.httpsPort));
    WriteIniString(config.configPath, L"server", L"enableHttp", config.enableHttp ? L"1" : L"0");
    WriteIniString(config.configPath, L"server", L"enableHttps", config.enableHttps ? L"1" : L"0");
    WriteIniString(config.configPath, L"server", L"allowUnauthenticatedRemote",
                   config.allowUnauthenticatedRemote ? L"1" : L"0");
    WriteIniString(config.configPath, L"server", L"allowQueryToken", config.allowQueryToken ? L"1" : L"0");
    WriteIniString(config.configPath, L"server", L"logRequests", config.logRequests ? L"1" : L"0");
    WriteIniString(config.configPath, L"server", L"maxBodyBytes", IntToWString((long)config.maxBodyBytes));
    WriteIniString(config.configPath, L"server", L"requestTimeoutSeconds", IntToWString(config.requestTimeoutSeconds));
    WriteIniString(config.configPath, L"server", L"ideTimeoutSeconds", IntToWString(config.ideTimeoutSeconds));
    WriteIniString(config.configPath, L"server", L"maxConnections", IntToWString(config.maxConnections));
    WriteIniString(config.configPath, L"server", L"keepAliveSeconds", IntToWString(config.keepAliveSeconds));
    WriteIniString(config.configPath, L"auth", L"token", config.authToken);
    WriteIniString(config.configPath, L"tls", L"certificateFile", config.certificateFile);
    WriteIniString(config.configPath, L"tls", L"certificatePassword", config.certificatePassword);
    WriteIniString(config.configPath, L"tls", L"certificateStore", config.certificateStore);
    WriteIniString(config.configPath, L"tls", L"certificateThumbprint", config.certificateThumbprint);
    WriteIniString(config.configPath, L"ide", L"vb6Exe", config.vb6Exe);
    WriteIniString(config.configPath, L"ide", L"sourceEncoding", config.sourceEncoding);
    WriteIniString(config.configPath, L"diagnostics", L"debugLog", config.debugLog ? L"1" : L"0");
    return true;
}

} // namespace mcpaddin
