#include "http_server.h"
#include "json.h"
#include "log.h"

#include <ws2tcpip.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace mcpaddin {

class HttpConnectionResponse;

namespace {

const size_t kMaxHeaderBytes = 64 * 1024;
const int kMaxHeaders = 200;
const int kMaxRequestsPerConnection = 100;

std::string IToA(long value) {
    char buffer[32] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%ld", value);
    return buffer;
}

std::string TrimHeader(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
    return value.substr(start, end - start);
}

bool IsValidHeaderName(const std::string& name) {
    if (name.empty()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!(ch >= 33 && ch <= 126) || ch == ':' || ch == ' ' || ch == '(' || ch == ')' ||
            ch == '<' || ch == '>' || ch == '@' || ch == ',' || ch == ';' || ch == '\\' ||
            ch == '"' || ch == '/' || ch == '[' || ch == ']' || ch == '?' || ch == '=' ||
            ch == '{' || ch == '}') return false;
    }
    return true;
}

bool ParseContentLength(const std::string& value, size_t& length) {
    if (value.empty()) return false;
    size_t result = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        size_t digit = (size_t)(value[i] - '0');
        if (result > (SIZE_MAX - digit) / 10) return false;
        result = result * 10 + digit;
    }
    length = result;
    return true;
}

bool ParseChunkSize(const std::string& line, size_t& size) {
    size_t pos = 0;
    size_t result = 0;
    bool any = false;
    while (pos < line.size()) {
        char ch = line[pos];
        int digit = -1;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else break;
        any = true;
        if (result > (SIZE_MAX - (size_t)digit) / 16) return false;
        result = result * 16 + (size_t)digit;
        ++pos;
    }
    if (!any) return false;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < line.size() && line[pos] != ';') return false;
    size = result;
    return true;
}

std::string GetSocketPeer(SOCKET socket) {
    sockaddr_storage addr;
    int len = sizeof(addr);
    if (getpeername(socket, (sockaddr*)&addr, &len) != 0) return std::string();
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    if (getnameinfo((sockaddr*)&addr, len, host, sizeof(host), service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) return std::string();
    return std::string(host) + ":" + service;
}

std::string BuildDateHeader() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buffer[64] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "%s, %02u %s %04u %02u:%02u:%02u GMT",
                days[st.wDayOfWeek % 7], st.wDay, months[(st.wMonth - 1) % 12], st.wYear,
                st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

void AddDefaultHeaders(std::map<std::string, std::string>& headers, bool tls) {
    headers["Date"] = BuildDateHeader();
    headers["Server"] = "MCPAddin/1.0";
    headers["X-Content-Type-Options"] = "nosniff";
    if (tls) headers["Strict-Transport-Security"] = "max-age=31536000";
}

bool SendAll(SOCKET socket, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        int chunk = (int)std::min<size_t>(data.size() - offset, 16384);
        int sent = send(socket, data.data() + offset, chunk, 0);
        if (sent <= 0) return false;
        offset += (size_t)sent;
    }
    return true;
}

} // namespace

std::string HttpRequest::Header(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = headers.find(ToLowerHeader(name));
    return it == headers.end() ? std::string() : it->second;
}

std::string HttpRequest::Query(const std::string& name) const {
    for (size_t i = 0; i < query.size(); ++i) {
        if (IEquals(query[i].first, name)) return query[i].second;
    }
    return std::string();
}

bool HttpRequest::HasHeader(const std::string& name) const {
    return headers.find(ToLowerHeader(name)) != headers.end();
}

std::string HttpStatusReason(int status) {
    switch (status) {
        case 100: return "Continue";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

std::string HttpUrlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            char* end = NULL;
            long decoded = strtol(hex, &end, 16);
            if (end == hex || *end != 0) {
                out.push_back(value[i]);
            } else {
                out.push_back((char)decoded);
                i += 2;
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

void HttpParseQuery(const std::string& query,
                    std::vector<std::pair<std::string, std::string> >& values) {
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        std::string part = query.substr(start, end - start);
        if (!part.empty()) {
            size_t eq = part.find('=');
            std::string name = eq == std::string::npos ? part : part.substr(0, eq);
            std::string value = eq == std::string::npos ? std::string() : part.substr(eq + 1);
            values.push_back(std::make_pair(HttpUrlDecode(name), HttpUrlDecode(value)));
        }
        if (end == query.size()) break;
        start = end + 1;
    }
}

std::string ToLowerHeader(const std::string& value) {
    std::string result = value;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] >= 'A' && result[i] <= 'Z') result[i] = (char)(result[i] - 'A' + 'a');
    }
    return result;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
    size_t maxLen = std::max(a.size(), b.size());
    size_t diff = a.size() ^ b.size();
    for (size_t i = 0; i < maxLen; ++i) {
        unsigned char ca = i < a.size() ? (unsigned char)a[i] : 0;
        unsigned char cb = i < b.size() ? (unsigned char)b[i] : 0;
        diff |= (size_t)(ca ^ cb);
    }
    return diff == 0;
}

HttpResponse JsonHttpResponse(int status, const std::string& body, const std::string& contentType) {
    HttpResponse response;
    response.status = status;
    response.contentType = contentType;
    response.body = body;
    return response;
}

HttpResponse TextHttpResponse(int status, const std::string& body, const std::string& contentType) {
    return JsonHttpResponse(status, body, contentType);
}

HttpResponse ErrorHttpResponse(int status, const std::string& message) {
    Json error = Json::ObjectValue();
    error.Set("error", Json::StringValue(message));
    std::string body = error.Dump(false);
    return JsonHttpResponse(status, body);
}

class HttpServer::Impl {
public:
    Impl(const ServerConfig& config, bool tls, const HttpHandler& handler)
        : config_(config), tls_(tls), handler_(handler), listenSocket_(INVALID_SOCKET),
          acceptThread_(NULL), running_(false), activeConnections_(0), port_(0),
          stopEvent_(NULL), activeEvent_(NULL), tlsContext_(NULL) {
        InitializeCriticalSection(&socketLock_);
        InitializeCriticalSection(&countLock_);
    }

    ~Impl() {
        Stop();
        DeleteCriticalSection(&socketLock_);
        DeleteCriticalSection(&countLock_);
    }

    bool Start(std::wstring& error);
    void Stop();
    bool IsRunning() const { return running_; }
    int Port() const { return port_; }
    bool IsTls() const { return tls_; }
    std::string ActualBindAddress() const { return actualBindAddress_; }

private:
    friend class HttpConnectionResponse;
    struct ConnectionContext {
        Impl* server;
        SOCKET socket;
    };
    static DWORD WINAPI AcceptThreadProc(LPVOID context);
    static DWORD WINAPI ConnectionThreadProc(LPVOID context);
    bool ConfigureSocket(SOCKET socket, std::wstring& error);
    bool SetReceiveTimeout(SOCKET socket, DWORD milliseconds);
    void AcceptLoop();
    void HandleConnection(SOCKET socket);
    bool ReadMore(SOCKET socket, TlsConnection* tls, std::string& buffer, std::wstring& error);
    bool ReadRequest(SOCKET socket, TlsConnection* tls, std::string& buffer,
                     HttpRequest& request, bool& closed, std::wstring& error);
    bool ParseHeaderBlock(const std::string& headerBlock, HttpRequest& request, std::wstring& error);
    bool ReadChunkedBody(SOCKET socket, TlsConnection* tls, std::string& buffer,
                         std::string& body, std::wstring& error);
    bool WriteAll(SOCKET socket, TlsConnection* tls, const std::string& data, std::wstring& error);
    bool WriteInterimContinue(SOCKET socket, TlsConnection* tls);
    void AddActive(SOCKET socket);
    void RemoveActive(SOCKET socket);
    void IncrementActive();
    void DecrementActive();
    void CloseActiveSockets();

    const ServerConfig& config_;
    bool tls_;
    HttpHandler handler_;
    SOCKET listenSocket_;
    HANDLE acceptThread_;
    bool running_;
    CRITICAL_SECTION socketLock_;
    CRITICAL_SECTION countLock_;
    std::set<SOCKET> activeSockets_;
    volatile LONG activeConnections_;
    int port_;
    HANDLE stopEvent_;
    HANDLE activeEvent_;
    TlsServerContext* tlsContext_;
    std::string actualBindAddress_;
};

class HttpConnectionResponse : public HttpResponder {
public:
    HttpConnectionResponse(HttpServer::Impl* server, SOCKET socket, TlsConnection* tls,
                           const HttpRequest& request, bool& sent, bool& close)
        : server_(server), socket_(socket), tls_(tls), request_(request),
          sent_(sent), close_(close), streaming_(false) {}

    bool Send(const HttpResponse& response) {
        if (sent_) return false;
        sent_ = true;
        close_ = response.closeConnection;
        std::map<std::string, std::string> headers = response.headers;
        AddDefaultHeaders(headers, tls_ != NULL);
        std::string body = response.body;
        bool noBody = request_.headRequest || response.status == 204 || response.status == 304 ||
                      response.status < 200;
        size_t contentLength = body.size();
        if (noBody) body.clear();
        if (!response.contentType.empty()) headers["Content-Type"] = response.contentType;
        headers["Content-Length"] = IToA((long)contentLength);
        std::string requestConnection = ToLowerHeader(request_.Header("connection"));
        bool keepAlive = !close_ && (request_.version == "HTTP/1.1" || requestConnection == "keep-alive");
        if (!keepAlive) close_ = true;
        std::string connectionValue = keepAlive ? "keep-alive" : "close";
        headers["Connection"] = connectionValue;
        std::string header = "HTTP/1.1 " + IToA(response.status) + " " +
                             HttpStatusReason(response.status) + "\r\n";
        for (std::map<std::string, std::string>::const_iterator it = headers.begin();
             it != headers.end(); ++it) {
            header += it->first + ": " + it->second + "\r\n";
        }
        header += "\r\n";
        std::wstring error;
        if (!server_->WriteAll(socket_, tls_, header, error)) return false;
        if (!body.empty() && !server_->WriteAll(socket_, tls_, body, error)) return false;
        return true;
    }

    bool BeginStream(int status, const std::string& contentType,
                     const std::map<std::string, std::string>& headers) {
        if (sent_) return false;
        sent_ = true;
        streaming_ = true;
        close_ = true;
        std::map<std::string, std::string> all = headers;
        AddDefaultHeaders(all, tls_ != NULL);
        all["Content-Type"] = contentType;
        all["Cache-Control"] = "no-cache";
        all["Connection"] = "close";
        all["X-Accel-Buffering"] = "no";
        std::string header = "HTTP/1.1 " + IToA(status) + " " +
                             HttpStatusReason(status) + "\r\n";
        for (std::map<std::string, std::string>::const_iterator it = all.begin(); it != all.end(); ++it) {
            header += it->first + ": " + it->second + "\r\n";
        }
        header += "\r\n";
        std::wstring error;
        return server_->WriteAll(socket_, tls_, header, error);
    }

    bool WriteStream(const std::string& data) {
        if (!streaming_ || data.empty()) return streaming_;
        std::wstring error;
        return server_->WriteAll(socket_, tls_, data, error);
    }

    void Close() {
        close_ = true;
        shutdown(socket_, SD_BOTH);
    }

    bool IsTls() const { return tls_ != NULL; }
    std::string RemoteAddress() const { return request_.remoteAddress; }

private:
    HttpServer::Impl* server_;
    SOCKET socket_;
    TlsConnection* tls_;
    const HttpRequest& request_;
    bool& sent_;
    bool& close_;
    bool streaming_;
};

HttpServer::HttpServer(const ServerConfig& config, bool tls, const HttpHandler& handler)
    : impl_(new Impl(config, tls, handler)) {
}

HttpServer::~HttpServer() {
    delete impl_;
}

bool HttpServer::Start(std::wstring& error) {
    return impl_->Start(error);
}

void HttpServer::Stop() {
    impl_->Stop();
}

bool HttpServer::IsRunning() const {
    return impl_->IsRunning();
}

int HttpServer::Port() const {
    return impl_->Port();
}

bool HttpServer::IsTls() const {
    return impl_->IsTls();
}

std::string HttpServer::ActualBindAddress() const {
    return impl_->ActualBindAddress();
}

bool HttpServer::Impl::Start(std::wstring& error) {
    if (running_) return true;
    stopEvent_ = CreateEventW(NULL, TRUE, FALSE, NULL);
    activeEvent_ = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!stopEvent_ || !activeEvent_) {
        error = L"Cannot create HTTP server synchronization objects.";
        return false;
    }
    if (tls_) {
        tlsContext_ = new TlsServerContext();
        if (!tlsContext_->Initialize(config_, error)) {
            delete tlsContext_;
            tlsContext_ = NULL;
            return false;
        }
    }

    WSADATA wsa;
    int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startup != 0) {
        error = L"WSAStartup failed: " + GetLastErrorMessage((DWORD)startup);
        return false;
    }

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    int desiredPort = tls_ ? config_.httpsPort : config_.httpPort;
    std::string portText = IToA(desiredPort);
    addrinfo* results = NULL;
    int gai = getaddrinfo(config_.bindAddress.c_str(), portText.c_str(), &hints, &results);
    if (gai != 0) {
        error = L"Cannot resolve bind address " + Utf8ToWide(config_.bindAddress) + L": " +
                Utf8ToWide(gai_strerrorA(gai));
        WSACleanup();
        return false;
    }
    bool bound = false;
    DWORD lastError = 0;
    for (addrinfo* ai = results; ai; ai = ai->ai_next) {
        listenSocket_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (listenSocket_ == INVALID_SOCKET) {
            lastError = (DWORD)WSAGetLastError();
            continue;
        }
        BOOL reuse = TRUE;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
        if (ai->ai_family == AF_INET6) {
            DWORD v6Only = 0;
            setsockopt(listenSocket_, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6Only, sizeof(v6Only));
        }
        if (bind(listenSocket_, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
            bound = true;
            break;
        }
        lastError = (DWORD)WSAGetLastError();
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    freeaddrinfo(results);
    if (!bound) {
        error = L"Cannot bind " + Utf8ToWide(config_.bindAddress) + L":" + Utf8ToWide(portText) +
                L": " + GetLastErrorMessage(lastError);
        WSACleanup();
        return false;
    }
    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        error = L"Cannot listen: " + GetLastErrorMessage((DWORD)WSAGetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    sockaddr_storage boundAddress;
    int boundLen = sizeof(boundAddress);
    if (getsockname(listenSocket_, (sockaddr*)&boundAddress, &boundLen) == 0) {
        char host[NI_MAXHOST] = {0};
        char service[NI_MAXSERV] = {0};
        if (getnameinfo((sockaddr*)&boundAddress, boundLen, host, sizeof(host), service, sizeof(service),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            actualBindAddress_ = std::string(host) + ":" + service;
            port_ = atoi(service);
        }
    }
    if (port_ == 0) port_ = desiredPort;
    running_ = true;
    acceptThread_ = CreateThread(NULL, 0, AcceptThreadProc, this, 0, NULL);
    if (!acceptThread_) {
        running_ = false;
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        WSACleanup();
        error = L"Cannot create the HTTP accept thread.";
        return false;
    }
    return true;
}

void HttpServer::Impl::Stop() {
    if (!running_) {
        if (tlsContext_) {
            tlsContext_->Shutdown();
            delete tlsContext_;
            tlsContext_ = NULL;
        }
        if (stopEvent_) CloseHandle(stopEvent_);
        if (activeEvent_) CloseHandle(activeEvent_);
        stopEvent_ = NULL;
        activeEvent_ = NULL;
        return;
    }
    running_ = false;
    if (stopEvent_) SetEvent(stopEvent_);
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    CloseActiveSockets();
    if (acceptThread_) {
        WaitForSingleObject(acceptThread_, 5000);
        CloseHandle(acceptThread_);
        acceptThread_ = NULL;
    }
    if (activeEvent_) WaitForSingleObject(activeEvent_, 10000);
    if (tlsContext_) {
        tlsContext_->Shutdown();
        delete tlsContext_;
        tlsContext_ = NULL;
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    if (activeEvent_) CloseHandle(activeEvent_);
    stopEvent_ = NULL;
    activeEvent_ = NULL;
    WSACleanup();
}

DWORD WINAPI HttpServer::Impl::AcceptThreadProc(LPVOID context) {
    ((Impl*)context)->AcceptLoop();
    return 0;
}

void HttpServer::Impl::AcceptLoop() {
    while (running_) {
        sockaddr_storage address;
        int addressLen = sizeof(address);
        SOCKET client = accept(listenSocket_, (sockaddr*)&address, &addressLen);
        if (client == INVALID_SOCKET) {
            if (!running_) break;
            Sleep(25);
            continue;
        }
        if ((int)InterlockedCompareExchange(&activeConnections_, 0, 0) >= config_.maxConnections) {
            const char* busy = "HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\n"
                               "Content-Length: 0\r\n\r\n";
            SendAll(client, busy);
            closesocket(client);
            continue;
        }
        AddActive(client);
        IncrementActive();
        ConnectionContext* context = new ConnectionContext;
        context->server = this;
        context->socket = client;
        HANDLE thread = CreateThread(NULL, 0, ConnectionThreadProc, (LPVOID)context, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        } else {
            delete context;
            RemoveActive(client);
            DecrementActive();
            closesocket(client);
        }
    }
}

DWORD WINAPI HttpServer::Impl::ConnectionThreadProc(LPVOID context) {
    ConnectionContext* connection = (ConnectionContext*)context;
    Impl* server = connection->server;
    SOCKET socket = connection->socket;
    delete connection;
    server->HandleConnection(socket);
    return 0;
}

bool HttpServer::Impl::ConfigureSocket(SOCKET socket, std::wstring& error) {
    DWORD timeout = (DWORD)config_.requestTimeoutSeconds * 1000;
    if (!SetReceiveTimeout(socket, timeout) ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) != 0) {
        error = L"Cannot configure socket timeouts: " + GetLastErrorMessage((DWORD)WSAGetLastError());
        return false;
    }
    int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
    return true;
}

bool HttpServer::Impl::SetReceiveTimeout(SOCKET socket, DWORD milliseconds) {
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      (const char*)&milliseconds, sizeof(milliseconds)) == 0;
}

bool HttpServer::Impl::ReadMore(SOCKET socket, TlsConnection* tls, std::string& buffer, std::wstring& error) {
    char data[16384];
    int count = 0;
    if (tls) {
        count = tls->Read(data, sizeof(data), error);
        if (count < 0) return false;
        if (count == 0) return true;
    } else {
        count = recv(socket, data, sizeof(data), 0);
        if (count < 0) {
            DWORD code = (DWORD)WSAGetLastError();
            if (code == WSAETIMEDOUT) error = L"request timeout";
            else error = L"Receive failed: " + GetLastErrorMessage(code);
            return false;
        }
        if (count == 0) return true;
    }
    buffer.append(data, (size_t)count);
    return true;
}

bool HttpServer::Impl::ParseHeaderBlock(const std::string& headerBlock, HttpRequest& request,
                                        std::wstring& error) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < headerBlock.size()) {
        size_t end = headerBlock.find("\r\n", start);
        if (end == std::string::npos) end = headerBlock.size();
        lines.push_back(headerBlock.substr(start, end - start));
        if (end == headerBlock.size()) break;
        start = end + 2;
    }
    if (lines.empty()) {
        error = L"Missing HTTP request line.";
        return false;
    }
    size_t firstSpace = lines[0].find(' ');
    size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : lines[0].find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        error = L"Malformed HTTP request line.";
        return false;
    }
    request.method = lines[0].substr(0, firstSpace);
    request.target = lines[0].substr(firstSpace + 1, secondSpace - firstSpace - 1);
    request.version = lines[0].substr(secondSpace + 1);
    request.headRequest = request.method == "HEAD";
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1") {
        error = L"Unsupported HTTP version.";
        return false;
    }
    std::string target = request.target;
    if (target.compare(0, 7, "http://") == 0) {
        size_t slash = target.find('/', 7);
        target = slash == std::string::npos ? "/" : target.substr(slash);
    } else if (target.compare(0, 8, "https://") == 0) {
        size_t slash = target.find('/', 8);
        target = slash == std::string::npos ? "/" : target.substr(slash);
    }
    size_t query = target.find('?');
    if (query == std::string::npos) {
        request.path = target;
        request.rawQuery.clear();
    } else {
        request.path = target.substr(0, query);
        request.rawQuery = target.substr(query + 1);
        HttpParseQuery(request.rawQuery, request.query);
    }
    if (request.path.empty() || request.path[0] != '/') {
        error = L"Invalid HTTP request target.";
        return false;
    }
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        if (lines[i][0] == ' ' || lines[i][0] == '\t') {
            error = L"Obsolete folded HTTP headers are not supported.";
            return false;
        }
        size_t colon = lines[i].find(':');
        if (colon == std::string::npos) {
            error = L"Malformed HTTP header.";
            return false;
        }
        std::string name = lines[i].substr(0, colon);
        if (!IsValidHeaderName(name)) {
            error = L"Invalid HTTP header name.";
            return false;
        }
        std::string value = TrimHeader(lines[i].substr(colon + 1));
        std::string key = ToLowerHeader(name);
        std::map<std::string, std::string>::iterator existing = request.headers.find(key);
        if (existing != request.headers.end()) {
            existing->second += ", " + value;
        } else {
            request.headers[key] = value;
        }
        if ((int)request.headers.size() > kMaxHeaders) {
            error = L"Too many HTTP headers.";
            return false;
        }
    }
    return true;
}

bool HttpServer::Impl::ReadChunkedBody(SOCKET socket, TlsConnection* tls, std::string& buffer,
                                       std::string& body, std::wstring& error) {
    body.clear();
    for (;;) {
        size_t lineEnd;
        while ((lineEnd = buffer.find("\r\n")) == std::string::npos) {
            if (buffer.size() > kMaxHeaderBytes) {
                error = L"Chunk header is too large.";
                return false;
            }
            if (!ReadMore(socket, tls, buffer, error)) return false;
            if (buffer.empty()) {
                error = L"Connection closed during chunked body.";
                return false;
            }
        }
        std::string sizeLine = buffer.substr(0, lineEnd);
        buffer.erase(0, lineEnd + 2);
        size_t chunkSize = 0;
        if (!ParseChunkSize(sizeLine, chunkSize)) {
            error = L"Invalid chunk size.";
            return false;
        }
        if (chunkSize == 0) {
            // Consume trailers.
            for (;;) {
                size_t trailerEnd;
                while ((trailerEnd = buffer.find("\r\n")) == std::string::npos) {
                    if (buffer.size() > kMaxHeaderBytes) {
                        error = L"Chunk trailer is too large.";
                        return false;
                    }
                    if (!ReadMore(socket, tls, buffer, error)) return false;
                    if (buffer.empty()) {
                        error = L"Connection closed in chunk trailer.";
                        return false;
                    }
                }
                std::string trailer = buffer.substr(0, trailerEnd);
                buffer.erase(0, trailerEnd + 2);
                if (trailer.empty()) return true;
            }
        }
        if (chunkSize > config_.maxBodyBytes ||
            body.size() > config_.maxBodyBytes - chunkSize) {
            error = L"Request body exceeds the configured limit.";
            return false;
        }
        while (buffer.size() < chunkSize + 2) {
            if (!ReadMore(socket, tls, buffer, error)) return false;
            if (buffer.empty()) {
                error = L"Connection closed during chunk body.";
                return false;
            }
        }
        body.append(buffer.data(), chunkSize);
        if (buffer[chunkSize] != '\r' || buffer[chunkSize + 1] != '\n') {
            error = L"Chunk data is not terminated by CRLF.";
            return false;
        }
        buffer.erase(0, chunkSize + 2);
    }
}

bool HttpServer::Impl::ReadRequest(SOCKET socket, TlsConnection* tls, std::string& buffer,
                                   HttpRequest& request, bool& closed, std::wstring& error) {
    closed = false;
    request = HttpRequest();
    size_t headerEnd = std::string::npos;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
        if (buffer.size() > kMaxHeaderBytes) {
            error = L"HTTP headers exceed the configured limit.";
            return false;
        }
        size_t before = buffer.size();
        if (!ReadMore(socket, tls, buffer, error)) {
            if (buffer.empty() && error == L"request timeout") {
                error.clear();
                closed = true;
                return true;
            }
            return false;
        }
        if (buffer.size() == before) {
            if (before == 0) {
                closed = true;
                return true;
            }
            error = L"Connection closed before HTTP headers completed.";
            return false;
        }
    }
    if (headerEnd > kMaxHeaderBytes) {
        error = L"HTTP headers exceed the configured limit.";
        return false;
    }
    std::string headerBlock = buffer.substr(0, headerEnd);
    buffer.erase(0, headerEnd + 4);
    if (!ParseHeaderBlock(headerBlock, request, error)) return false;
    SetReceiveTimeout(socket, (DWORD)config_.requestTimeoutSeconds * 1000);

    std::string transferEncoding = ToLowerHeader(request.Header("transfer-encoding"));
    std::string contentLengthValue = request.Header("content-length");
    if (!transferEncoding.empty() && !contentLengthValue.empty()) {
        error = L"Both Transfer-Encoding and Content-Length were supplied.";
        return false;
    }
    if (!transferEncoding.empty()) {
        if (transferEncoding != "chunked") {
            error = L"Unsupported Transfer-Encoding.";
            return false;
        }
        std::string expect = ToLowerHeader(request.Header("expect"));
        if (expect == "100-continue") {
            if (!WriteInterimContinue(socket, tls)) return false;
        }
        return ReadChunkedBody(socket, tls, buffer, request.body, error);
    }
    if (!contentLengthValue.empty()) {
        size_t contentLength = 0;
        if (!ParseContentLength(contentLengthValue, contentLength)) {
            error = L"Invalid Content-Length.";
            return false;
        }
        if (contentLength > config_.maxBodyBytes) {
            error = L"Request body exceeds the configured limit.";
            return false;
        }
        std::string expect = ToLowerHeader(request.Header("expect"));
        if (expect == "100-continue" && buffer.size() < contentLength) {
            if (!WriteInterimContinue(socket, tls)) return false;
        }
        while (buffer.size() < contentLength) {
            size_t before = buffer.size();
            if (!ReadMore(socket, tls, buffer, error)) return false;
            if (buffer.size() == before) {
                error = L"Connection closed before request body completed.";
                return false;
            }
        }
        request.body.assign(buffer.data(), contentLength);
        buffer.erase(0, contentLength);
    }
    return true;
}

bool HttpServer::Impl::WriteAll(SOCKET socket, TlsConnection* tls, const std::string& data,
                                std::wstring& error) {
    if (tls) return tls->Write(data.data(), (int)data.size(), error);
    if (!SendAll(socket, data)) {
        error = L"Send failed: " + GetLastErrorMessage((DWORD)WSAGetLastError());
        return false;
    }
    return true;
}

bool HttpServer::Impl::WriteInterimContinue(SOCKET socket, TlsConnection* tls) {
    static const std::string kContinue = "HTTP/1.1 100 Continue\r\n\r\n";
    std::wstring error;
    return WriteAll(socket, tls, kContinue, error);
}

void HttpServer::Impl::HandleConnection(SOCKET socket) {
    std::wstring error;
    if (!ConfigureSocket(socket, error)) {
        Logger::Warn(WideToUtf8(error));
        closesocket(socket);
        RemoveActive(socket);
        DecrementActive();
        return;
    }
    TlsConnection* tls = NULL;
    if (tls_) {
        tls = new TlsConnection(*tlsContext_);
        std::string handshakeBuffered;
        if (!tls->Handshake(socket, handshakeBuffered, error)) {
            Logger::Warn(WideToUtf8(error));
            closesocket(socket);
            delete tls;
            RemoveActive(socket);
            DecrementActive();
            return;
        }
    }
    std::string buffer;
    for (int requestCount = 0; requestCount < kMaxRequestsPerConnection && running_; ++requestCount) {
        DWORD receiveTimeout = buffer.empty()
            ? (DWORD)config_.keepAliveSeconds * 1000
            : (DWORD)config_.requestTimeoutSeconds * 1000;
        SetReceiveTimeout(socket, receiveTimeout);
        HttpRequest request;
        bool closed = false;
        if (!ReadRequest(socket, tls, buffer, request, closed, error)) {
            bool sent = false;
            bool close = true;
            HttpConnectionResponse responder(this, socket, tls, request, sent, close);
            int status = 400;
            if (error.find(L"exceeds") != std::wstring::npos) status = 413;
            responder.Send(ErrorHttpResponse(status, WideToUtf8(error)));
            Logger::Warn("HTTP parse error from " + GetSocketPeer(socket) + ": " + WideToUtf8(error));
            break;
        }
        if (closed) break;
        request.remoteAddress = GetSocketPeer(socket);
        request.tls = tls != NULL;
        if (request.method == "CONNECT" || request.method == "TRACE") {
            bool sent = false;
            bool close = true;
            HttpConnectionResponse responder(this, socket, tls, request, sent, close);
            responder.Send(ErrorHttpResponse(405, "Method not allowed."));
            break;
        }
        bool sent = false;
        bool close = false;
        HttpConnectionResponse responder(this, socket, tls, request, sent, close);
        try {
            handler_(request, responder);
        } catch (...) {
            if (!sent) responder.Send(ErrorHttpResponse(500, "Unhandled server exception."));
            close = true;
        }
        if (!sent) {
            responder.Send(ErrorHttpResponse(500, "Handler produced no HTTP response."));
            close = true;
        }
        if (close) break;
        bool clientClose = IEquals(ToLowerHeader(request.Header("connection")), "close");
        if (request.version == "HTTP/1.0" && !IEquals(ToLowerHeader(request.Header("connection")), "keep-alive")) {
            clientClose = true;
        }
        if (clientClose) break;
    }
    if (tls) {
        delete tls;
        tls = NULL;
    }
    shutdown(socket, SD_BOTH);
    closesocket(socket);
    RemoveActive(socket);
    DecrementActive();
}

void HttpServer::Impl::AddActive(SOCKET socket) {
    EnterCriticalSection(&socketLock_);
    activeSockets_.insert(socket);
    LeaveCriticalSection(&socketLock_);
}

void HttpServer::Impl::RemoveActive(SOCKET socket) {
    EnterCriticalSection(&socketLock_);
    activeSockets_.erase(socket);
    LeaveCriticalSection(&socketLock_);
}

void HttpServer::Impl::IncrementActive() {
    LONG count = InterlockedIncrement(&activeConnections_);
    if (count == 1 && activeEvent_) ResetEvent(activeEvent_);
}

void HttpServer::Impl::DecrementActive() {
    if (InterlockedDecrement(&activeConnections_) == 0 && activeEvent_) {
        SetEvent(activeEvent_);
    }
}

void HttpServer::Impl::CloseActiveSockets() {
    EnterCriticalSection(&socketLock_);
    for (std::set<SOCKET>::iterator it = activeSockets_.begin(); it != activeSockets_.end(); ++it) {
        shutdown(*it, SD_BOTH);
    }
    LeaveCriticalSection(&socketLock_);
}

} // namespace mcpaddin
