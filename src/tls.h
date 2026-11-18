#pragma once

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif

#include "common.h"

#include <winsock2.h>
#include <wincrypt.h>
#include <sspi.h>

namespace mcpaddin {

struct ServerConfig;

class TlsServerContext {
public:
    TlsServerContext();
    ~TlsServerContext();

    bool Initialize(const ServerConfig& config, std::wstring& error);
    void Shutdown();
    bool IsReady() const;

private:
    friend class TlsConnection;
    CredHandle cred_;
    bool credValid_;
    HCERTSTORE certStore_;
    PCCERT_CONTEXT cert_;
    std::wstring error_;
};

class TlsConnection {
public:
    explicit TlsConnection(TlsServerContext& context);
    ~TlsConnection();

    bool Handshake(SOCKET socket, std::string& buffered, std::wstring& error);
    int Read(char* buffer, int length, std::wstring& error);
    bool Write(const char* buffer, int length, std::wstring& error);
    bool HasBufferedData() const;

private:
    bool EnsureRecord(std::wstring& error);
    bool DecryptPending(std::wstring& error, bool& gotData, bool& closed);
    bool ReadRaw(std::wstring& error);
    bool SendRaw(const char* data, size_t length, std::wstring& error);
    bool SendHandshakeToken(const SecBuffer& token, std::wstring& error);

    TlsServerContext& context_;
    SOCKET socket_;
    CtxtHandle ctx_;
    bool ctxValid_;
    SecPkgContext_StreamSizes sizes_;
    std::string rawInput_;
    std::string plainInput_;
    bool peerClosed_;
};

} // namespace mcpaddin
