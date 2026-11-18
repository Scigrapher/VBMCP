#pragma once

#include "common.h"
#include "config.h"
#include "http_server.h"
#include "json.h"
#include "tools.h"

#include <memory>

namespace mcpaddin {

class McpServer {
public:
    McpServer(const ServerConfig& config, IIdeBackend* backend);
    ~McpServer();

    bool Start(std::wstring& error);
    void Stop();
    bool IsRunning() const;
    Json Status();
    std::string HttpEndpoint() const;
    std::string HttpsEndpoint() const;

private:
    class Impl;
    Impl* impl_;
};

} // namespace mcpaddin
