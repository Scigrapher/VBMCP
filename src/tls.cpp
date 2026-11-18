#define SECURITY_WIN32
#include "tls.h"
#include "config.h"
#include "log.h"

#include <schannel.h>
#include <security.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdio>

#ifndef PKCS12_NO_PERSIST_KEY
#define PKCS12_NO_PERSIST_KEY 0x00008000
#endif

namespace mcpaddin {

static std::wstring CertNameFromThumbprint(const std::wstring& thumbprint) {
    std::wstring value;
    for (size_t i = 0; i < thumbprint.size(); ++i) {
        wchar_t ch = thumbprint[i];
        if (ch == L' ' || ch == L':' || ch == L'\t' || ch == L'\r' || ch == L'\n') continue;
        value.push_back(ch);
    }
    return value;
}

static PCCERT_CONTEXT FindCertificateByThumbprint(HCERTSTORE store, const std::wstring& thumbprint) {
    std::wstring clean = CertNameFromThumbprint(thumbprint);
    if (clean.empty()) return NULL;
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i + 1 < clean.size(); i += 2) {
        wchar_t pair[3] = {clean[i], clean[i + 1], 0};
        wchar_t* end = NULL;
        unsigned long value = wcstoul(pair, &end, 16);
        if (end == pair || *end != 0) return NULL;
        bytes.push_back((unsigned char)value);
    }
    CRYPT_HASH_BLOB hash;
    hash.cbData = (DWORD)bytes.size();
    hash.pbData = bytes.empty() ? NULL : &bytes[0];
    return CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                      CERT_FIND_HASH, &hash, NULL);
}

static PCCERT_CONTEXT FindFirstCertificate(HCERTSTORE store) {
    PCCERT_CONTEXT cert = CertEnumCertificatesInStore(store, NULL);
    if (!cert) return NULL;
    PCCERT_CONTEXT copy = CertDuplicateCertificateContext(cert);
    CertFreeCertificateContext(cert);
    return copy;
}

TlsServerContext::TlsServerContext()
    : credValid_(false), certStore_(NULL), cert_(NULL) {
    memset(&cred_, 0, sizeof(cred_));
}

TlsServerContext::~TlsServerContext() {
    Shutdown();
}

bool TlsServerContext::Initialize(const ServerConfig& config, std::wstring& error) {
    Shutdown();
    if (!config.certificateFile.empty()) {
        std::vector<unsigned char> pfx;
        DWORD readError = 0;
        if (!ReadFileBytes(config.certificateFile, pfx, &readError)) {
            error = L"Cannot read certificate file " + config.certificateFile + L": " +
                    GetLastErrorMessage(readError);
            return false;
        }
        CRYPT_DATA_BLOB blob;
        blob.cbData = (DWORD)pfx.size();
        blob.pbData = pfx.empty() ? NULL : &pfx[0];
        std::wstring password = Utf8ToWide(config.certificatePassword);
        DWORD flags = 0;
        certStore_ = PFXImportCertStore(&blob, password.c_str(), flags);
        if (!certStore_) {
            error = L"Cannot import PKCS#12 certificate: " + GetLastErrorMessage();
            return false;
        }
        if (!config.certificateThumbprint.empty()) {
            cert_ = FindCertificateByThumbprint(certStore_, config.certificateThumbprint);
        } else {
            cert_ = FindFirstCertificate(certStore_);
        }
    } else if (!config.certificateThumbprint.empty()) {
        std::wstring storeName = config.certificateStore.empty() ? L"MY" : config.certificateStore;
        certStore_ = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, NULL,
                                   CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG,
                                   storeName.c_str());
        if (!certStore_) {
            certStore_ = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, NULL,
                                       CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                                       storeName.c_str());
        }
        if (!certStore_) {
            error = L"Cannot open certificate store " + storeName + L": " + GetLastErrorMessage();
            return false;
        }
        cert_ = FindCertificateByThumbprint(certStore_, config.certificateThumbprint);
    }
    if (!cert_) {
        error = L"No usable server certificate was found.";
        if (!config.certificateFile.empty()) error += L" Certificate file: " + config.certificateFile + L".";
        if (!config.certificateThumbprint.empty()) error += L" Thumbprint: " + config.certificateThumbprint + L".";
        DWORD last = GetLastError();
        if (last != ERROR_SUCCESS) error += L" " + GetLastErrorMessage(last);
        Shutdown();
        return false;
    }
    if (!(cert_->dwCertEncodingType & X509_ASN_ENCODING)) {
        error = L"The configured certificate is not an X.509 certificate.";
        Shutdown();
        return false;
    }

    SCHANNEL_CRED schannel;
    memset(&schannel, 0, sizeof(schannel));
    schannel.dwVersion = SCHANNEL_CRED_VERSION;
    schannel.cCreds = 1;
    schannel.paCred = &cert_;
    schannel.hRootStore = NULL;
    schannel.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;
    schannel.grbitEnabledProtocols = 0;
    TimeStamp expiry;
    SECURITY_STATUS status = AcquireCredentialsHandleW(NULL, (SEC_WCHAR*)UNISP_NAME_W,
                                                       SECPKG_CRED_INBOUND, NULL, &schannel,
                                                       NULL, NULL, &cred_, &expiry);
    if (status != SEC_E_OK) {
        error = L"AcquireCredentialsHandle failed: " + HResultMessage((HRESULT)status);
        Shutdown();
        return false;
    }
    credValid_ = true;
    return true;
}

void TlsServerContext::Shutdown() {
    if (credValid_) {
        FreeCredentialsHandle(&cred_);
        credValid_ = false;
    }
    if (cert_) {
        CertFreeCertificateContext(cert_);
        cert_ = NULL;
    }
    if (certStore_) {
        CertCloseStore(certStore_, 0);
        certStore_ = NULL;
    }
}

bool TlsServerContext::IsReady() const {
    return credValid_ && cert_ != NULL;
}

TlsConnection::TlsConnection(TlsServerContext& context)
    : context_(context), socket_(INVALID_SOCKET), ctxValid_(false), peerClosed_(false) {
    memset(&ctx_, 0, sizeof(ctx_));
    memset(&sizes_, 0, sizeof(sizes_));
}

TlsConnection::~TlsConnection() {
    if (ctxValid_) DeleteSecurityContext(&ctx_);
}

bool TlsConnection::ReadRaw(std::wstring& error) {
    char buffer[16384];
    int received = recv(socket_, buffer, sizeof(buffer), 0);
    if (received > 0) {
        rawInput_.append(buffer, (size_t)received);
        if (rawInput_.size() > 1024 * 1024) {
            error = L"TLS handshake exceeded the 1 MiB safety limit.";
            return false;
        }
        return true;
    }
    if (received == 0) {
        peerClosed_ = true;
        error.clear();
        return false;
    } else {
        DWORD code = (DWORD)WSAGetLastError();
        if (code == WSAETIMEDOUT) error = L"request timeout";
        else error = L"TLS receive failed: " + GetLastErrorMessage(code);
    }
    return false;
}

bool TlsConnection::SendRaw(const char* data, size_t length, std::wstring& error) {
    size_t offset = 0;
    while (offset < length) {
        int chunk = (int)std::min<size_t>(length - offset, 16384);
        int sent = send(socket_, data + offset, chunk, 0);
        if (sent <= 0) {
            error = L"TLS send failed: " + GetLastErrorMessage((DWORD)WSAGetLastError());
            return false;
        }
        offset += (size_t)sent;
    }
    return true;
}

bool TlsConnection::SendHandshakeToken(const SecBuffer& token, std::wstring& error) {
    if (!token.pvBuffer || token.cbBuffer == 0) return true;
    return SendRaw((const char*)token.pvBuffer, token.cbBuffer, error);
}

bool TlsConnection::Handshake(SOCKET socket, std::string& buffered, std::wstring& error) {
    socket_ = socket;
    if (!context_.IsReady()) {
        error = L"TLS context is not initialized.";
        return false;
    }
    bool first = true;
    for (int iteration = 0; iteration < 64; ++iteration) {
        if (rawInput_.empty()) {
            if (!ReadRaw(error)) return false;
        }
        std::vector<SecBuffer> inBuffers(2);
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].cbBuffer = (unsigned long)rawInput_.size();
        inBuffers[0].pvBuffer = &rawInput_[0];
        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        inBuffers[1].cbBuffer = 0;
        inBuffers[1].pvBuffer = NULL;
        SecBufferDesc inDesc = {SECBUFFER_VERSION, 2, &inBuffers[0]};
        std::vector<SecBuffer> outBuffers(1);
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[0].cbBuffer = 0;
        outBuffers[0].pvBuffer = NULL;
        SecBufferDesc outDesc = {SECBUFFER_VERSION, 1, &outBuffers[0]};
        ULONG attributes = 0;
        TimeStamp expiry;
        SECURITY_STATUS status = AcceptSecurityContext(
            &context_.cred_, first ? NULL : &ctx_, &inDesc,
            ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                ASC_REQ_EXTENDED_ERROR | ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
            SECURITY_NATIVE_DREP, first ? &ctx_ : NULL, &outDesc, &attributes, &expiry);
        if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK) ctxValid_ = true;
        if (outBuffers[0].cbBuffer && outBuffers[0].pvBuffer) {
            if (!SendHandshakeToken(outBuffers[0], error)) {
                FreeContextBuffer(outBuffers[0].pvBuffer);
                return false;
            }
            FreeContextBuffer(outBuffers[0].pvBuffer);
        }

        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            if (!ReadRaw(error)) return false;
            continue;
        }
        if (status == SEC_I_CONTINUE_NEEDED) {
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                rawInput_.erase(0, rawInput_.size() - inBuffers[1].cbBuffer);
            } else {
                rawInput_.clear();
            }
            first = false;
            continue;
        }
        if (status != SEC_E_OK) {
            error = L"TLS handshake failed: " + HResultMessage((HRESULT)status);
            return false;
        }
        if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
            rawInput_.erase(0, rawInput_.size() - inBuffers[1].cbBuffer);
        } else {
            rawInput_.clear();
        }
        if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK) {
            error = L"TLS stream sizes are unavailable.";
            return false;
        }
        // Any bytes following the handshake are TLS records or application data.
        // Keep them in rawInput_ so the next Read() consumes them.
        buffered.clear();
        return true;
    }
    error = L"TLS handshake exceeded the iteration limit.";
    return false;
}

bool TlsConnection::EnsureRecord(std::wstring& error) {
    for (;;) {
        if (rawInput_.size() >= 5) {
            size_t recordLength = ((unsigned char)rawInput_[3] << 8) | (unsigned char)rawInput_[4];
            size_t total = 5 + recordLength;
            if (recordLength == 0 || total > 65536 + 5) {
                error = L"Invalid TLS record length.";
                return false;
            }
            if (rawInput_.size() >= total) return true;
        }
        if (!ReadRaw(error)) return false;
    }
}

bool TlsConnection::DecryptPending(std::wstring& error, bool& gotData, bool& closed) {
    gotData = false;
    closed = false;
    for (;;) {
        if (!EnsureRecord(error)) return false;
        size_t recordLength = ((unsigned char)rawInput_[3] << 8) | (unsigned char)rawInput_[4];
        size_t total = 5 + recordLength;
        std::vector<char> record(rawInput_.begin(), rawInput_.begin() + total);
        rawInput_.erase(0, total);
        std::vector<SecBuffer> buffers(4);
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].cbBuffer = (unsigned long)record.size();
        buffers[0].pvBuffer = &record[0];
        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[2].BufferType = SECBUFFER_EMPTY;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc = {SECBUFFER_VERSION, 4, &buffers[0]};
        SECURITY_STATUS status = DecryptMessage(&ctx_, &desc, 0, NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // Put the partial record back and read more.
            rawInput_.insert(0, record.data(), record.size());
            if (!ReadRaw(error)) return false;
            continue;
        }
        if (status == SEC_I_CONTEXT_EXPIRED) {
            closed = true;
            return true;
        }
        if (status == SEC_I_RENEGOTIATE) {
            error = L"TLS renegotiation is not supported while serving HTTP.";
            return false;
        }
        if (status != SEC_E_OK) {
            error = L"TLS decrypt failed: " + HResultMessage((HRESULT)status);
            return false;
        }
        for (size_t i = 0; i < buffers.size(); ++i) {
            if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
                plainInput_.append((const char*)buffers[i].pvBuffer, buffers[i].cbBuffer);
                gotData = true;
            } else if (buffers[i].BufferType == SECBUFFER_EXTRA && buffers[i].cbBuffer > 0) {
                rawInput_.insert(0, (const char*)buffers[i].pvBuffer, buffers[i].cbBuffer);
            }
        }
        return true;
    }
}

int TlsConnection::Read(char* buffer, int length, std::wstring& error) {
    while (plainInput_.empty()) {
        if (peerClosed_) return 0;
        bool gotData = false;
        bool closed = false;
        if (!DecryptPending(error, gotData, closed)) return peerClosed_ ? 0 : -1;
        if (closed) return 0;
        if (!gotData) continue;
    }
    int count = (int)std::min<int>(length, (int)plainInput_.size());
    memcpy(buffer, plainInput_.data(), (size_t)count);
    plainInput_.erase(0, (size_t)count);
    return count;
}

bool TlsConnection::Write(const char* buffer, int length, std::wstring& error) {
    size_t offset = 0;
    while (offset < (size_t)length) {
        size_t chunk = std::min<size_t>((size_t)length - offset, sizes_.cbMaximumMessage);
        size_t total = sizes_.cbHeader + chunk + sizes_.cbTrailer;
        std::vector<unsigned char> record(total);
        memcpy(&record[sizes_.cbHeader], buffer + offset, chunk);
        std::vector<SecBuffer> buffers(4);
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].cbBuffer = sizes_.cbHeader;
        buffers[0].pvBuffer = &record[0];
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].cbBuffer = (unsigned long)chunk;
        buffers[1].pvBuffer = &record[sizes_.cbHeader];
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].cbBuffer = sizes_.cbTrailer;
        buffers[2].pvBuffer = &record[sizes_.cbHeader + chunk];
        buffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc = {SECBUFFER_VERSION, 4, &buffers[0]};
        SECURITY_STATUS status = EncryptMessage(&ctx_, 0, &desc, 0);
        if (status != SEC_E_OK) {
            error = L"TLS encrypt failed: " + HResultMessage((HRESULT)status);
            return false;
        }
        if (!SendRaw((const char*)&record[0], total, error)) return false;
        offset += chunk;
    }
    return true;
}

bool TlsConnection::HasBufferedData() const {
    return !plainInput_.empty() || !rawInput_.empty();
}

} // namespace mcpaddin
