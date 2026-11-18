#include "mcp_server.h"
#include "log.h"

#include <algorithm>
#include <deque>
#include <exception>
#include <map>
#include <memory>

namespace mcpaddin {

namespace {

const char* kServerName = "VB6 MCP Addin";
const char* kServerVersion = "1.0.0";
const char* kDefaultProtocolVersion = "2025-06-18";

std::string PortText(int port) {
    char buffer[32] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d", port);
    return buffer;
}

Json RpcError(const Json& id, int code, const std::string& message,
              const Json& data = Json::NullValue()) {
    Json response = Json::ObjectValue();
    response.Set("jsonrpc", MakeString("2.0"));
    response.Set("id", id);
    Json error = Json::ObjectValue();
    error.Set("code", MakeNumber((double)code));
    error.Set("message", MakeString(message));
    if (!data.IsNull()) error.Set("data", data);
    response.Set("error", error);
    return response;
}

Json RpcResult(const Json& id, const Json& result) {
    Json response = Json::ObjectValue();
    response.Set("jsonrpc", MakeString("2.0"));
    response.Set("id", id);
    response.Set("result", result);
    return response;
}

bool IsSupportedProtocol(const std::string& value) {
    return value == "2025-06-18" || value == "2025-03-26" || value == "2024-11-05";
}

Json InitializeResult(const std::string& requestedVersion) {
    Json result = Json::ObjectValue();
    std::string version = IsSupportedProtocol(requestedVersion) ? requestedVersion : kDefaultProtocolVersion;
    result.Set("protocolVersion", MakeString(version));
    Json capabilities = Json::ObjectValue();
    Json tools = Json::ObjectValue();
    tools.Set("listChanged", MakeBool(false));
    capabilities.Set("tools", tools);
    result.Set("capabilities", capabilities);
    Json serverInfo = Json::ObjectValue();
    serverInfo.Set("name", MakeString(kServerName));
    serverInfo.Set("version", MakeString(kServerVersion));
    result.Set("serverInfo", serverInfo);
    result.Set("instructions",
        MakeString("Controls a running Visual Basic 6 IDE. Use project_* and component_* tools for "
                   "the VBIDE automation model, form_* for designer text operations, and source_* "
                   "for allow-root-constrained text files. Tool arguments use the IDE's project and "
                   "component names (or VBP paths)."));
    return result;
}

Json ToolCallResult(const ToolResult& result) {
    Json response = Json::ObjectValue();
    Json content = Json::ArrayValue();
    Json item = Json::ObjectValue();
    item.Set("type", MakeString("text"));
    if (result.ok) {
        if (result.data.IsString()) item.Set("text", result.data);
        else item.Set("text", MakeString(result.data.Dump(true)));
    } else {
        item.Set("text", MakeString(result.data.Dump(true)));
    }
    content.push_back(item);
    response.Set("content", content);
    if (result.ok) {
        if (result.data.IsObject()) response.Set("structuredContent", result.data);
        else {
            Json wrapped = Json::ObjectValue();
            wrapped.Set("result", result.data);
            response.Set("structuredContent", wrapped);
        }
    } else {
        Json structured = result.data;
        structured.Set("isError", MakeBool(true));
        response.Set("structuredContent", structured);
    }
    response.Set("isError", MakeBool(!result.ok));
    return response;
}

class LegacySessions {
public:
    struct Session {
        std::string id;
        std::deque<std::string> messages;
        CRITICAL_SECTION lock;
        HANDLE event;
        bool closed;
        size_t queuedBytes;
        Session() : event(NULL), closed(false), queuedBytes(0) {
            InitializeCriticalSection(&lock);
            event = CreateEventW(NULL, TRUE, FALSE, NULL);
        }
        ~Session() {
            if (event) CloseHandle(event);
            DeleteCriticalSection(&lock);
        }
    };

    LegacySessions() {
        InitializeCriticalSection(&lock_);
    }
    ~LegacySessions() {
        CloseAll();
        DeleteCriticalSection(&lock_);
    }

    std::shared_ptr<Session> Create() {
        std::shared_ptr<Session> session(new Session());
        session->id = NewGuidString();
        if (session->id.empty()) session->id = NewGuidString() + NewGuidString();
        EnterCriticalSection(&lock_);
        sessions_[session->id] = session;
        LeaveCriticalSection(&lock_);
        return session;
    }

    std::shared_ptr<Session> Find(const std::string& id) {
        EnterCriticalSection(&lock_);
        std::map<std::string, std::shared_ptr<Session> >::iterator it = sessions_.find(id);
        std::shared_ptr<Session> result = it == sessions_.end() ? std::shared_ptr<Session>() : it->second;
        LeaveCriticalSection(&lock_);
        return result;
    }

    void Remove(const std::string& id) {
        EnterCriticalSection(&lock_);
        sessions_.erase(id);
        LeaveCriticalSection(&lock_);
    }

    void Enqueue(const std::shared_ptr<Session>& session, const std::string& message) {
        if (!session) return;
        EnterCriticalSection(&session->lock);
        if (!session->closed) {
            if (session->queuedBytes + message.size() > 8 * 1024 * 1024) {
                session->closed = true;
            } else {
                session->messages.push_back(message);
                session->queuedBytes += message.size();
            }
            SetEvent(session->event);
        }
        LeaveCriticalSection(&session->lock);
    }

    void Close(const std::shared_ptr<Session>& session) {
        if (!session) return;
        EnterCriticalSection(&session->lock);
        session->closed = true;
        SetEvent(session->event);
        LeaveCriticalSection(&session->lock);
        Remove(session->id);
    }

    void CloseAll() {
        EnterCriticalSection(&lock_);
        std::vector<std::shared_ptr<Session> > sessions;
        for (std::map<std::string, std::shared_ptr<Session> >::iterator it = sessions_.begin();
             it != sessions_.end(); ++it) sessions.push_back(it->second);
        sessions_.clear();
        LeaveCriticalSection(&lock_);
        for (size_t i = 0; i < sessions.size(); ++i) {
            EnterCriticalSection(&sessions[i]->lock);
            sessions[i]->closed = true;
            SetEvent(sessions[i]->event);
            LeaveCriticalSection(&sessions[i]->lock);
        }
    }

private:
    CRITICAL_SECTION lock_;
    std::map<std::string, std::shared_ptr<Session> > sessions_;
};

} // namespace

class McpServer::Impl {
public:
    Impl(const ServerConfig& config, IIdeBackend* backend)
        : config_(config), backend_(backend), http_(NULL), https_(NULL),
          running_(false), protocolVersion_(kDefaultProtocolVersion) {}

    ~Impl() {
        Stop();
    }

    bool Start(std::wstring& error);
    void Stop();
    bool IsRunning() const { return running_ != false; }
    Json Status();
    std::string HttpEndpoint() const;
    std::string HttpsEndpoint() const;

private:
    void HandleHttp(const HttpRequest& request, HttpResponder& responder);
    void HandleSse(const HttpRequest& request, HttpResponder& responder, bool authenticated);
    bool Authenticate(const HttpRequest& request) const;
    bool RequireAuth(const HttpRequest& request, HttpResponder& responder) const;
    void AddCors(const HttpRequest& request, HttpResponse& response) const;
    void AddCorsHeaders(const HttpRequest& request, std::map<std::string, std::string>& headers) const;
    Json ProcessBatch(const Json& batch, bool& hasResponse);
    Json ProcessSingle(const Json& request, bool& hasResponse);
    void LogRequest(const HttpRequest& request, int status, size_t bytes) const;

    const ServerConfig& config_;
    IIdeBackend* backend_;
    HttpServer* http_;
    HttpServer* https_;
    volatile bool running_;
    std::string protocolVersion_;
    LegacySessions sessions_;
};

bool McpServer::Impl::Start(std::wstring& error) {
    if (running_) return true;
    HttpHandler handler = [this](const HttpRequest& request, HttpResponder& responder) {
        HandleHttp(request, responder);
    };
    if (config_.enableHttp) {
        http_ = new HttpServer(config_, false, handler);
        if (!http_->Start(error)) {
            delete http_;
            http_ = NULL;
            return false;
        }
    }
    if (config_.enableHttps) {
        https_ = new HttpServer(config_, true, handler);
        if (!https_->Start(error)) {
            if (http_) {
                http_->Stop();
                delete http_;
                http_ = NULL;
            }
            delete https_;
            https_ = NULL;
            return false;
        }
    }
    running_ = true;
    return true;
}

void McpServer::Impl::Stop() {
    running_ = false;
    sessions_.CloseAll();
    if (http_) {
        http_->Stop();
        delete http_;
        http_ = NULL;
    }
    if (https_) {
        https_->Stop();
        delete https_;
        https_ = NULL;
    }
}

std::string McpServer::Impl::HttpEndpoint() const {
    if (!http_ || !config_.enableHttp) return std::string();
    std::string host = config_.bindAddress;
    if (host == "0.0.0.0" || host.empty()) host = "127.0.0.1";
    if (host == "::") host = "[::1]";
    else if (host.find(':') != std::string::npos && (host.empty() || host[0] != '[')) host = "[" + host + "]";
    return "http://" + host + ":" + PortText(http_->Port());
}

std::string McpServer::Impl::HttpsEndpoint() const {
    if (!https_ || !config_.enableHttps) return std::string();
    std::string host = config_.bindAddress;
    if (host == "0.0.0.0" || host.empty()) host = "127.0.0.1";
    if (host == "::") host = "[::1]";
    else if (host.find(':') != std::string::npos && (host.empty() || host[0] != '[')) host = "[" + host + "]";
    return "https://" + host + ":" + PortText(https_->Port());
}

Json McpServer::Impl::Status() {
    Json status = Json::ObjectValue();
    status.Set("running", MakeBool(running_));
    status.Set("serverName", MakeString(kServerName));
    status.Set("serverVersion", MakeString(kServerVersion));
    status.Set("protocolVersion", MakeString(protocolVersion_));
    status.Set("httpEndpoint", MakeString(HttpEndpoint()));
    status.Set("httpsEndpoint", MakeString(HttpsEndpoint()));
    status.Set("mcpEndpoint", MakeString(HttpEndpoint().empty() ? HttpsEndpoint() : HttpEndpoint() + "/mcp"));
    status.Set("configPath", Json::StringValue(config_.configPath));
    status.Set("logPath", Json::StringValue(config_.logPath));
    status.Set("authenticationRequired", MakeBool(!config_.authToken.empty()));
    status.Set("backend", MakeString(backend_->BackendName()));
    status.Set("backendAvailable", MakeBool(backend_->IsAvailable()));
    status.Set("ide", backend_->Status());
    return status;
}

bool McpServer::Impl::Authenticate(const HttpRequest& request) const {
    if (config_.authToken.empty()) return true;
    std::string authorization = request.Header("authorization");
    if (authorization.size() >= 7 && IEquals(authorization.substr(0, 7), "bearer ")) {
        if (ConstantTimeEquals(Trim(authorization.substr(7)), config_.authToken)) return true;
    }
    std::string headerToken = request.Header("x-mcp-token");
    if (!headerToken.empty() && ConstantTimeEquals(Trim(headerToken), config_.authToken)) return true;
    if (config_.allowQueryToken && ConstantTimeEquals(request.Query("token"), config_.authToken)) return true;
    return false;
}

bool McpServer::Impl::RequireAuth(const HttpRequest& request, HttpResponder& responder) const {
    if (Authenticate(request)) return true;
    HttpResponse response = ErrorHttpResponse(401, "Authentication required.");
    response.headers["WWW-Authenticate"] = "Bearer realm=\"VB6 MCP Addin\"";
    AddCors(request, response);
    responder.Send(response);
    return false;
}

void McpServer::Impl::AddCorsHeaders(const HttpRequest& request,
                                     std::map<std::string, std::string>& headers) const {
    if (config_.corsOrigin.empty()) return;
    std::string origin = request.Header("origin");
    if (config_.corsOrigin == "*") headers["Access-Control-Allow-Origin"] = "*";
    else if (!origin.empty() && IEquals(origin, config_.corsOrigin)) {
        headers["Access-Control-Allow-Origin"] = origin;
        headers["Vary"] = "Origin";
    }
    if (headers.find("Access-Control-Allow-Origin") != headers.end()) {
        headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-MCP-Token, MCP-Protocol-Version, MCP-Session-Id";
        headers["Access-Control-Allow-Methods"] = "GET, POST, DELETE, OPTIONS";
        headers["Access-Control-Expose-Headers"] = "MCP-Protocol-Version, MCP-Session-Id, WWW-Authenticate";
    }
}

void McpServer::Impl::AddCors(const HttpRequest& request, HttpResponse& response) const {
    AddCorsHeaders(request, response.headers);
}

void McpServer::Impl::LogRequest(const HttpRequest& request, int status, size_t bytes) const {
    char buffer[512] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s %s%s -> %d (%llu bytes) from %s%s",
                request.method.c_str(), request.path.c_str(),
                request.rawQuery.empty() ? "" : "?<query>", status,
                (unsigned long long)bytes, request.remoteAddress.c_str(),
                request.tls ? " tls" : "");
    Logger::Request(buffer);
}

void McpServer::Impl::HandleHttp(const HttpRequest& request, HttpResponder& responder) {
    if (request.method == "OPTIONS") {
        HttpResponse response;
        response.status = 204;
        AddCors(request, response);
        responder.Send(response);
        return;
    }
    if (request.path == "/health" && request.method == "GET") {
        Json health = Json::ObjectValue();
        health.Set("ok", MakeBool(running_));
        health.Set("service", MakeString(kServerName));
        health.Set("version", MakeString(kServerVersion));
        health.Set("backend", MakeString(backend_->BackendName()));
        health.Set("ideAvailable", MakeBool(backend_->IsAvailable()));
        HttpResponse response = JsonHttpResponse(200, health.Dump(false));
        AddCors(request, response);
        responder.Send(response);
        LogRequest(request, 200, response.body.size());
        return;
    }
    if (!RequireAuth(request, responder)) {
        LogRequest(request, 401, 0);
        return;
    }
    if (request.path == "/") {
        Json root = Json::ObjectValue();
        root.Set("service", MakeString(kServerName));
        root.Set("version", MakeString(kServerVersion));
        root.Set("mcp", MakeString("/mcp"));
        root.Set("legacySse", MakeString("/sse"));
        root.Set("health", MakeString("/health"));
        HttpResponse response = JsonHttpResponse(200, root.Dump(true));
        AddCors(request, response);
        responder.Send(response);
        LogRequest(request, 200, response.body.size());
        return;
    }
    if (request.path == "/sse" && request.method == "GET") {
        HandleSse(request, responder, true);
        return;
    }
    if (request.path == "/messages" && request.method == "POST") {
        std::string sessionId = request.Query("sessionId");
        if (sessionId.empty()) sessionId = request.Header("mcp-session-id");
        std::shared_ptr<LegacySessions::Session> session = sessions_.Find(sessionId);
        if (!session) {
            HttpResponse response = ErrorHttpResponse(404, "Unknown or expired SSE session.");
            AddCors(request, response);
            responder.Send(response);
            LogRequest(request, 404, response.body.size());
            return;
        }
        std::string contentType = ToLowerHeader(request.Header("content-type"));
        if (!contentType.empty() && contentType.find("application/json") == std::string::npos) {
            HttpResponse response = ErrorHttpResponse(415, "Content-Type must be application/json.");
            AddCors(request, response);
            responder.Send(response);
            return;
        }
        std::string parseError;
        Json body = Json::Parse(request.body, &parseError);
        if (!parseError.empty()) {
            sessions_.Enqueue(session, RpcError(Json::NullValue(), -32700, "Parse error", MakeString(parseError)).Dump(false));
            HttpResponse response;
            response.status = 202;
            AddCors(request, response);
            responder.Send(response);
            return;
        }
        bool hasResponse = false;
        Json responseJson;
        if (body.IsArray()) responseJson = ProcessBatch(body, hasResponse);
        else responseJson = ProcessSingle(body, hasResponse);
        if (hasResponse) sessions_.Enqueue(session, responseJson.Dump(false));
        HttpResponse accepted;
        accepted.status = 202;
        AddCors(request, accepted);
        responder.Send(accepted);
        LogRequest(request, 202, 0);
        return;
    }
    if ((request.path == "/mcp" || request.path == "/rpc") && request.method == "POST") {
        std::string contentType = ToLowerHeader(request.Header("content-type"));
        if (!contentType.empty() && contentType.find("application/json") == std::string::npos) {
            HttpResponse response = ErrorHttpResponse(415, "Content-Type must be application/json.");
            AddCors(request, response);
            responder.Send(response);
            LogRequest(request, 415, response.body.size());
            return;
        }
        std::string parseError;
        Json body = Json::Parse(request.body, &parseError);
        if (!parseError.empty()) {
            HttpResponse response = JsonHttpResponse(400, RpcError(Json::NullValue(), -32700, "Parse error",
                                                                  MakeString(parseError)).Dump(false));
            AddCors(request, response);
            responder.Send(response);
            LogRequest(request, 400, response.body.size());
            return;
        }
        bool hasResponse = false;
        Json responseJson;
        if (body.IsArray()) responseJson = ProcessBatch(body, hasResponse);
        else responseJson = ProcessSingle(body, hasResponse);
        if (!hasResponse) {
            HttpResponse accepted;
            accepted.status = 202;
            AddCors(request, accepted);
            responder.Send(accepted);
            LogRequest(request, 202, 0);
            return;
        }
        std::string accept = ToLowerHeader(request.Header("accept"));
        bool sseOnly = accept.find("application/json") == std::string::npos &&
                       accept.find("text/event-stream") != std::string::npos;
        HttpResponse response;
        if (sseOnly) {
            response = TextHttpResponse(200,
                "event: message\r\ndata: " + responseJson.Dump(false) + "\r\n\r\n",
                "text/event-stream; charset=utf-8");
            response.closeConnection = true;
        } else {
            response = JsonHttpResponse(200, responseJson.Dump(false));
        }
        response.headers["MCP-Protocol-Version"] = protocolVersion_;
        AddCors(request, response);
        responder.Send(response);
        LogRequest(request, 200, response.body.size());
        return;
    }
    if ((request.path == "/mcp" || request.path == "/rpc") && request.method == "GET") {
        HttpResponse response = ErrorHttpResponse(405, "This server uses POST for MCP JSON-RPC. Use /sse for the legacy SSE transport.");
        response.headers["Allow"] = "POST, DELETE, OPTIONS";
        AddCors(request, response);
        responder.Send(response);
        LogRequest(request, 405, response.body.size());
        return;
    }
    if ((request.path == "/mcp" || request.path == "/rpc") && request.method == "DELETE") {
        HttpResponse response;
        response.status = 204;
        AddCors(request, response);
        responder.Send(response);
        LogRequest(request, 204, 0);
        return;
    }
    HttpResponse response = ErrorHttpResponse(404, "Not found.");
    AddCors(request, response);
    responder.Send(response);
    LogRequest(request, 404, response.body.size());
}

void McpServer::Impl::HandleSse(const HttpRequest& request, HttpResponder& responder, bool authenticated) {
    if (!authenticated) return;
    std::shared_ptr<LegacySessions::Session> session = sessions_.Create();
    std::map<std::string, std::string> headers;
    headers["MCP-Session-Id"] = session->id;
    AddCorsHeaders(request, headers);
    if (!responder.BeginStream(200, "text/event-stream; charset=utf-8", headers)) {
        sessions_.Close(session);
        return;
    }
    std::string endpoint = "event: endpoint\r\ndata: /messages?sessionId=" + session->id + "\r\n\r\n";
    if (!responder.WriteStream(endpoint)) {
        sessions_.Close(session);
        return;
    }
    LogRequest(request, 200, endpoint.size());
    while (running_) {
        DWORD wait = WaitForSingleObject(session->event, 15000);
        if (!running_) break;
        std::deque<std::string> messages;
        EnterCriticalSection(&session->lock);
        messages.swap(session->messages);
        session->queuedBytes = 0;
        ResetEvent(session->event);
        bool closed = session->closed;
        LeaveCriticalSection(&session->lock);
        if (closed) break;
        if (wait == WAIT_TIMEOUT) {
            if (!responder.WriteStream(": keepalive\r\n\r\n")) break;
            continue;
        }
        bool writeOk = true;
        for (size_t i = 0; i < messages.size(); ++i) {
            std::string event = "event: message\r\ndata: " + messages[i] + "\r\n\r\n";
            if (!responder.WriteStream(event)) {
                writeOk = false;
                break;
            }
        }
        if (!writeOk) break;
    }
    sessions_.Close(session);
    responder.Close();
}

Json McpServer::Impl::ProcessBatch(const Json& batch, bool& hasResponse) {
    hasResponse = false;
    Json responses = Json::ArrayValue();
    if (batch.empty()) {
        hasResponse = true;
        return RpcError(Json::NullValue(), -32600, "Invalid empty JSON-RPC batch.");
    }
    for (size_t i = 0; i < batch.size(); ++i) {
        bool itemResponse = false;
        Json item = ProcessSingle(batch.at(i), itemResponse);
        if (itemResponse) {
            responses.push_back(item);
            hasResponse = true;
        }
    }
    if (!hasResponse) return Json::NullValue();
    return responses;
}

Json McpServer::Impl::ProcessSingle(const Json& request, bool& hasResponse) {
    hasResponse = false;
    if (!request.IsObject()) {
        hasResponse = true;
        return RpcError(Json::NullValue(), -32600, "Invalid JSON-RPC request.");
    }
    bool hasId = request.Has("id");
    Json id = hasId ? request.Get("id") : Json::NullValue();
    if (!request.Has("jsonrpc") || request.Get("jsonrpc").AsString() != "2.0") {
        if (!hasId) return Json::NullValue();
        hasResponse = true;
        return RpcError(id, -32600, "jsonrpc must be \"2.0\".");
    }
    if (!request.Has("method") || !request.Get("method").IsString()) {
        if (!hasId) return Json::NullValue();
        hasResponse = true;
        return RpcError(id, -32600, "JSON-RPC method is required.");
    }
    std::string method = request.Get("method").AsString();
    Json params = request.Has("params") ? request.Get("params") : Json::ObjectValue();

    if (!hasId) {
        if (method == "notifications/initialized" || method == "notifications/cancelled" ||
            method == "notifications/progress" || method == "notifications/roots/list_changed") {
            return Json::NullValue();
        }
        return Json::NullValue();
    }

    hasResponse = true;
    if (method == "initialize") {
        std::string requested = params.Get("protocolVersion").AsString();
        protocolVersion_ = IsSupportedProtocol(requested) ? requested : kDefaultProtocolVersion;
        return RpcResult(id, InitializeResult(requested));
    }
    if (method == "ping") {
        return RpcResult(id, Json::ObjectValue());
    }
    if (method == "tools/list") {
        Json result = Json::ObjectValue();
        result.Set("tools", GetToolDefinitions());
        return RpcResult(id, result);
    }
    if (method == "tools/call") {
        if (!params.IsObject() || !params.Get("name").IsString()) {
            return RpcError(id, -32602, "tools/call requires params.name.");
        }
        std::string toolName = params.Get("name").AsString();
        if (!IsKnownTool(toolName)) {
            return RpcError(id, -32602, "Unknown tool: " + toolName);
        }
        Json arguments = params.Get("arguments");
        if (!arguments.IsObject()) arguments = Json::ObjectValue();
        try {
            ToolResult toolResult = backend_->CallTool(toolName, arguments);
            if (toolResult.ok && toolName == "ide_status" && toolResult.data.IsObject()) {
                Json transport = Json::ObjectValue();
                transport.Set("httpEndpoint", MakeString(HttpEndpoint()));
                transport.Set("httpsEndpoint", MakeString(HttpsEndpoint()));
                transport.Set("mcpEndpoint", MakeString(HttpEndpoint().empty() ?
                                                         HttpsEndpoint() + "/mcp" : HttpEndpoint() + "/mcp"));
                transport.Set("authenticationRequired", MakeBool(!config_.authToken.empty()));
                toolResult.data.Set("transport", transport);
            }
            return RpcResult(id, ToolCallResult(toolResult));
        } catch (const std::exception& ex) {
            ToolResult failure = ToolResult::Failure(std::string("Tool exception: ") + ex.what(), "tool_exception");
            return RpcResult(id, ToolCallResult(failure));
        } catch (...) {
            ToolResult failure = ToolResult::Failure("Unknown tool exception.", "tool_exception");
            return RpcResult(id, ToolCallResult(failure));
        }
    }
    if (method == "resources/list" || method == "resources/templates/list" || method == "prompts/list") {
        Json result = Json::ObjectValue();
        if (method == "resources/list") result.Set("resources", Json::ArrayValue());
        else if (method == "resources/templates/list") result.Set("resourceTemplates", Json::ArrayValue());
        else result.Set("prompts", Json::ArrayValue());
        return RpcResult(id, result);
    }
    if (method == "logging/setLevel") {
        return RpcResult(id, Json::ObjectValue());
    }
    return RpcError(id, -32601, "Method not found: " + method);
}

McpServer::McpServer(const ServerConfig& config, IIdeBackend* backend)
    : impl_(new Impl(config, backend)) {
}

McpServer::~McpServer() {
    delete impl_;
}

bool McpServer::Start(std::wstring& error) {
    return impl_->Start(error);
}

void McpServer::Stop() {
    impl_->Stop();
}

bool McpServer::IsRunning() const {
    return impl_->IsRunning();
}

Json McpServer::Status() {
    return impl_->Status();
}

std::string McpServer::HttpEndpoint() const {
    return impl_->HttpEndpoint();
}

std::string McpServer::HttpsEndpoint() const {
    return impl_->HttpsEndpoint();
}

} // namespace mcpaddin
