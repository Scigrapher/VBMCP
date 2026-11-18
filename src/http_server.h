#pragma once

#include "common.h"
#include "config.h"
#include "tls.h"

#include <functional>
#include <winsock2.h>

namespace mcpaddin {

class HttpConnectionResponse;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string rawQuery;
    std::string version;
    std::map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::string> > query;
    std::string body;
    std::string remoteAddress;
    bool tls;
    bool headRequest;
    HttpRequest() : tls(false), headRequest(false) {}

    std::string Header(const std::string& name) const;
    std::string Query(const std::string& name) const;
    bool HasHeader(const std::string& name) const;
};

struct HttpResponse {
    int status;
    std::string contentType;
    std::map<std::string, std::string> headers;
    std::string body;
    bool closeConnection;
    HttpResponse() : status(200), closeConnection(false) {}
};

class HttpResponder {
public:
    virtual ~HttpResponder() {}
    virtual bool Send(const HttpResponse& response) = 0;
    virtual bool BeginStream(int status, const std::string& contentType,
                             const std::map<std::string, std::string>& headers) = 0;
    virtual bool WriteStream(const std::string& data) = 0;
    virtual void Close() = 0;
    virtual bool IsTls() const = 0;
    virtual std::string RemoteAddress() const = 0;
};

typedef std::function<void(const HttpRequest&, HttpResponder&)> HttpHandler;

class HttpServer {
public:
    HttpServer(const ServerConfig& config, bool tls, const HttpHandler& handler);
    ~HttpServer();

    bool Start(std::wstring& error);
    void Stop();
    bool IsRunning() const;
    int Port() const;
    bool IsTls() const;
    std::string ActualBindAddress() const;

private:
    friend class HttpConnectionResponse;
    class Impl;
    Impl* impl_;
};

std::string HttpStatusReason(int status);
std::string HttpUrlDecode(const std::string& value);
void HttpParseQuery(const std::string& query,
                    std::vector<std::pair<std::string, std::string> >& values);
std::string ToLowerHeader(const std::string& value);
bool ConstantTimeEquals(const std::string& a, const std::string& b);

HttpResponse JsonHttpResponse(int status, const std::string& body,
                              const std::string& contentType = "application/json; charset=utf-8");
HttpResponse TextHttpResponse(int status, const std::string& body,
                              const std::string& contentType = "text/plain; charset=utf-8");
HttpResponse ErrorHttpResponse(int status, const std::string& message);

} // namespace mcpaddin
