#pragma once

#include "common.h"
#include "com_ide.h"
#include "config.h"
#include "mcp_server.h"

#include <memory>

namespace mcpaddin {

// Microsoft Add-In Designer interface. It is declared locally so this project
// has no compile-time dependency on a VB6 or Office installation.
struct __declspec(uuid("B65AD801-ABAF-11D0-BB8B-00A0C90F2744")) IDTExtensibility2 : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE OnConnection(IDispatch* application, long connectMode,
                                                   IDispatch* addInInstance,
                                                   SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDisconnection(long disconnectMode, SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnAddInsUpdate(SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnStartupComplete(SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnBeginShutdown(SAFEARRAY** custom) = 0;
};

class MCPAddinClass : public IDTExtensibility2 {
public:
    MCPAddinClass();
    virtual ~MCPAddinClass();

    static HRESULT CreateInstance(IUnknown* outer, REFIID iid, void** result);

    STDMETHODIMP QueryInterface(REFIID iid, void** result);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    STDMETHODIMP GetTypeInfoCount(UINT* count);
    STDMETHODIMP GetTypeInfo(UINT index, LCID lcid, ITypeInfo** typeInfo);
    STDMETHODIMP GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                              LCID lcid, DISPID* dispids);
    STDMETHODIMP Invoke(DISPID dispid, REFIID iid, LCID lcid, WORD flags,
                        DISPPARAMS* params, VARIANT* result,
                        EXCEPINFO* exceptionInfo, UINT* argumentError);

    STDMETHODIMP OnConnection(IDispatch* application, long connectMode,
                              IDispatch* addInInstance, SAFEARRAY** custom);
    STDMETHODIMP OnDisconnection(long disconnectMode, SAFEARRAY** custom);
    STDMETHODIMP OnAddInsUpdate(SAFEARRAY** custom);
    STDMETHODIMP OnStartupComplete(SAFEARRAY** custom);
    STDMETHODIMP OnBeginShutdown(SAFEARRAY** custom);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    volatile LONG refCount_;
    CRITICAL_SECTION lifecycleLock_;
    bool started_;
    ServerConfig config_;
    std::unique_ptr<ComIdeBackend> backend_;
    std::unique_ptr<McpServer> server_;
    HWND dispatchWindow_;
    std::wstring startupError_;
    ComPtr<IDispatch> vbeForStartup_;

    bool StartServer();
    bool StartServerWithVbe();
    void StopServer();
    bool CreateDispatchWindow();
    void DestroyDispatchWindow();
};

void AddRefModule();
void ReleaseModule();
bool CanUnloadModule();

} // namespace mcpaddin
