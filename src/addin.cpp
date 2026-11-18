#include "addin.h"
#include "log.h"
#include "project_files.h"

#include <cstdio>

extern HMODULE g_moduleHandle;

namespace mcpaddin {

namespace {

const wchar_t* kWindowClassName = L"MCPAddin.DispatchWindow";
const CLSID kAddinClsid = {0x7B1B6F2E, 0x6F65, 0x4E5F,
                           {0xA5, 0xB2, 0x4B, 0x19, 0xE1, 0xB8, 0xC0, 0x01}};
const wchar_t* kProgId = L"MCPAddin.Connect";
volatile LONG g_moduleObjects = 0;
volatile LONG g_moduleLocks = 0;
MCPAddinClass* g_currentAddin = NULL;

bool SetRegistryString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                       const std::wstring& value) {
    HKEY key = NULL;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExW(root, subkey.c_str(), 0, NULL, 0, KEY_WRITE, NULL,
                                  &key, &disposition);
    if (status != ERROR_SUCCESS) return false;
    status = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool SetRegistryDword(HKEY root, const std::wstring& subkey, const wchar_t* name, DWORD value) {
    HKEY key = NULL;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExW(root, subkey.c_str(), 0, NULL, 0, KEY_WRITE, NULL,
                                  &key, &disposition);
    if (status != ERROR_SUCCESS) return false;
    status = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool DeleteRegistryTree(HKEY root, const std::wstring& subkey) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS) {
        return true;
    }
    for (;;) {
        wchar_t name[512] = {0};
        DWORD nameLength = _countof(name);
        FILETIME lastWrite;
        if (RegEnumKeyExW(key, 0, name, &nameLength, NULL, NULL, NULL, &lastWrite) != ERROR_SUCCESS) break;
        DeleteRegistryTree(key, name);
    }
    RegCloseKey(key);
    RegDeleteKeyW(root, subkey.c_str());
    return true;
}

std::wstring GuidToString(REFGUID guid) {
    wchar_t buffer[64] = {0};
    StringFromGUID2(guid, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));
    return buffer;
}

bool WriteStartupError(const ServerConfig& config, const std::wstring& error) {
    if (config.dataDirectory.empty()) return false;
    EnsureDirectory(config.dataDirectory);
    std::wstring path = PathJoin(config.dataDirectory, L"mcpaddin.error.txt");
    std::wstring text = Logger::TimestampW() + L"\r\n" + error + L"\r\n";
    DWORD writeError = 0;
    return WriteTextFileAtomic(path, text, L"utf8", &writeError);
}

} // namespace

void AddRefModule() {
    InterlockedIncrement(&g_moduleLocks);
}

void ReleaseModule() {
    InterlockedDecrement(&g_moduleLocks);
}

bool CanUnloadModule() {
    return InterlockedCompareExchange(&g_moduleObjects, 0, 0) == 0 &&
           InterlockedCompareExchange(&g_moduleLocks, 0, 0) == 0;
}

MCPAddinClass::MCPAddinClass()
    : refCount_(1), started_(false), dispatchWindow_(NULL) {
    InitializeCriticalSection(&lifecycleLock_);
    InterlockedIncrement(&g_moduleObjects);
}

MCPAddinClass::~MCPAddinClass() {
    StopServer();
    DestroyDispatchWindow();
    if (g_currentAddin == this) g_currentAddin = NULL;
    DeleteCriticalSection(&lifecycleLock_);
    InterlockedDecrement(&g_moduleObjects);
}

HRESULT MCPAddinClass::CreateInstance(IUnknown* outer, REFIID iid, void** result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    MCPAddinClass* instance = new MCPAddinClass();
    if (!instance) return E_OUTOFMEMORY;
    HRESULT hr = instance->QueryInterface(iid, result);
    instance->Release();
    return hr;
}

STDMETHODIMP MCPAddinClass::QueryInterface(REFIID iid, void** result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (iid == IID_IUnknown || iid == IID_IDispatch ||
        iid == __uuidof(IDTExtensibility2)) {
        *result = static_cast<IDTExtensibility2*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) MCPAddinClass::AddRef() {
    return (ULONG)InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) MCPAddinClass::Release() {
    ULONG count = (ULONG)InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return count;
}

STDMETHODIMP MCPAddinClass::GetTypeInfoCount(UINT* count) {
    if (!count) return E_POINTER;
    *count = 0;
    return S_OK;
}

STDMETHODIMP MCPAddinClass::GetTypeInfo(UINT, LCID, ITypeInfo** typeInfo) {
    if (typeInfo) *typeInfo = NULL;
    return E_NOTIMPL;
}

STDMETHODIMP MCPAddinClass::GetIDsOfNames(REFIID, LPOLESTR* names, UINT count,
                                          LCID, DISPID* dispids) {
    if (!names || !dispids || count == 0) return E_INVALIDARG;
    static const wchar_t* methodNames[] = {
        L"OnConnection", L"OnDisconnection", L"OnAddInsUpdate",
        L"OnStartupComplete", L"OnBeginShutdown"
    };
    for (UINT i = 0; i < count; ++i) {
        dispids[i] = DISPID_UNKNOWN;
        for (int m = 0; m < 5; ++m) {
            if (_wcsicmp(names[i], methodNames[m]) == 0) {
                dispids[i] = m + 1;
                break;
            }
        }
        if (dispids[i] == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;
    }
    return S_OK;
}

STDMETHODIMP MCPAddinClass::Invoke(DISPID dispid, REFIID, LCID, WORD,
                                   DISPPARAMS* params, VARIANT* result,
                                   EXCEPINFO*, UINT*) {
    if (result) VariantInit(result);
    if (dispid < 1 || dispid > 5) return DISP_E_MEMBERNOTFOUND;
    if (!params) return E_INVALIDARG;
    // Office/VB hosts use the vtable. This IDispatch path is provided for
    // diagnostics and non-vtable automation hosts.
    if (dispid == 1 && params->cArgs >= 4) {
        IDispatch* application = V_DISPATCH(&params->rgvarg[3]);
        IDispatch* addIn = V_DISPATCH(&params->rgvarg[1]);
        long mode = V_I4(&params->rgvarg[2]);
        SAFEARRAY** custom = params->rgvarg[0].vt == (VT_ARRAY | VT_VARIANT) ?
                             (SAFEARRAY**)params->rgvarg[0].parray : NULL;
        return OnConnection(application, mode, addIn, custom);
    }
    if (dispid == 2 && params->cArgs >= 2) {
        long mode = V_I4(&params->rgvarg[1]);
        return OnDisconnection(mode, NULL);
    }
    if (dispid == 3) return OnAddInsUpdate(NULL);
    if (dispid == 4) return OnStartupComplete(NULL);
    if (dispid == 5) return OnBeginShutdown(NULL);
    return E_INVALIDARG;
}

bool RegisterWindowClass() {
    WNDCLASSEXW windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = MCPAddinClass::WindowProc;
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool MCPAddinClass::CreateDispatchWindow() {
    if (dispatchWindow_) return true;
    if (!RegisterWindowClass()) {
        startupError_ = L"Cannot register the add-in dispatch window class: " + GetLastErrorMessage();
        return false;
    }
    dispatchWindow_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    if (!dispatchWindow_) {
        startupError_ = L"Cannot create the add-in dispatch window: " + GetLastErrorMessage();
        return false;
    }
    g_currentAddin = this;
    return true;
}

void MCPAddinClass::DestroyDispatchWindow() {
    if (dispatchWindow_) {
        DestroyWindow(dispatchWindow_);
        dispatchWindow_ = NULL;
    }
    if (g_currentAddin == this) g_currentAddin = NULL;
}

LRESULT CALLBACK MCPAddinClass::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)window;
    (void)wParam;
    if (message == ComIdeBackend::kToolMessage && g_currentAddin) {
        if (g_currentAddin->backend_) g_currentAddin->backend_->HandlePostedCall(lParam);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool MCPAddinClass::StartServer() {
    if (started_) return true;
    std::wstring error;
    if (!ConfigManager::Load(config_, &error)) {
        startupError_ = error;
        return false;
    }
    Logger::Initialize(config_);
    Logger::Info("VB6 MCP Add-in starting");
    if (!CreateDispatchWindow()) {
        Logger::Error(WideToUtf8(startupError_));
        return false;
    }
    backend_.reset(new ComIdeBackend(config_));
    server_.reset(new McpServer(config_, backend_.get()));
    if (!server_->Start(error)) {
        startupError_ = error;
        Logger::Error("MCP server failed to start: " + WideToUtf8(error));
        WriteStartupError(config_, error);
        backend_->Detach();
        server_.reset();
        backend_.reset();
        DestroyDispatchWindow();
        return false;
    }
    std::string message = "MCP HTTP endpoint: " + server_->HttpEndpoint() +
                          " HTTPS endpoint: " + server_->HttpsEndpoint() +
                          " config: " + WideToUtf8(config_.configPath);
    Logger::Info(message);
    started_ = true;
    return true;
}

void MCPAddinClass::StopServer() {
    if (!started_ && !server_) return;
    if (backend_) backend_->BeginShutdown();
    if (server_) {
        server_->Stop();
        server_.reset();
    }
    if (backend_) {
        backend_->Detach();
        backend_.reset();
    }
    started_ = false;
    Logger::Info("VB6 MCP Add-in stopped");
    Logger::Shutdown();
}

STDMETHODIMP MCPAddinClass::OnConnection(IDispatch* application, long, IDispatch*, SAFEARRAY**) {
    EnterCriticalSection(&lifecycleLock_);
    if (started_) {
        LeaveCriticalSection(&lifecycleLock_);
        return S_OK;
    }
    if (!application) {
        LeaveCriticalSection(&lifecycleLock_);
        return E_INVALIDARG;
    }
    // Keep the VBE pointer until the backend attaches it below.
    vbeForStartup_.Reset(application, true);
    StartServerWithVbe();
    vbeForStartup_.Reset();
    LeaveCriticalSection(&lifecycleLock_);
    return S_OK;
}

bool MCPAddinClass::StartServerWithVbe() {
    if (!StartServer()) {
        if (backend_) backend_->Attach(vbeForStartup_.get(), dispatchWindow_);
        return false;
    }
    backend_->Attach(vbeForStartup_.get(), dispatchWindow_);
    return true;
}

STDMETHODIMP MCPAddinClass::OnDisconnection(long, SAFEARRAY**) {
    EnterCriticalSection(&lifecycleLock_);
    StopServer();
    DestroyDispatchWindow();
    LeaveCriticalSection(&lifecycleLock_);
    return S_OK;
}

STDMETHODIMP MCPAddinClass::OnAddInsUpdate(SAFEARRAY**) {
    return S_OK;
}

STDMETHODIMP MCPAddinClass::OnStartupComplete(SAFEARRAY**) {
    return S_OK;
}

STDMETHODIMP MCPAddinClass::OnBeginShutdown(SAFEARRAY**) {
    EnterCriticalSection(&lifecycleLock_);
    StopServer();
    DestroyDispatchWindow();
    LeaveCriticalSection(&lifecycleLock_);
    return S_OK;
}

class AddinClassFactory : public IClassFactory {
public:
    AddinClassFactory() : refCount_(1) {
        AddRefModule();
    }
    ~AddinClassFactory() {
        ReleaseModule();
    }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) {
        if (!result) return E_POINTER;
        *result = NULL;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *result = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        return (ULONG)InterlockedIncrement(&refCount_);
    }
    STDMETHODIMP_(ULONG) Release() {
        ULONG count = (ULONG)InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** result) {
        return MCPAddinClass::CreateInstance(outer, iid, result);
    }
    STDMETHODIMP LockServer(BOOL lock) {
        if (lock) AddRefModule();
        else ReleaseModule();
        return S_OK;
    }
private:
    volatile LONG refCount_;
};

bool RegisterAddin(bool add) {
    std::wstring modulePath = GetModulePath(g_moduleHandle);
    std::wstring clsid = GuidToString(kAddinClsid);
    std::wstring clsidKey = L"CLSID\\" + clsid;
    std::wstring progIdKey = kProgId;
    if (!add) {
        DeleteRegistryTree(HKEY_CURRENT_USER, L"Software\\Classes\\" + clsidKey);
        DeleteRegistryTree(HKEY_CURRENT_USER, L"Software\\Classes\\" + progIdKey);
        DeleteRegistryTree(HKEY_CLASSES_ROOT, clsidKey);
        DeleteRegistryTree(HKEY_CLASSES_ROOT, progIdKey);
        DeleteRegistryTree(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect");
        DeleteRegistryTree(HKEY_CURRENT_USER,
                           L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect");
        DeleteRegistryTree(HKEY_CURRENT_USER,
                           L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Addins\\MCPAddin.Connect");
        return true;
    }
    bool ok = true;
    std::wstring userClsidKey = L"Software\\Classes\\" + clsidKey;
    std::wstring userProgIdKey = L"Software\\Classes\\" + progIdKey;
    ok = SetRegistryString(HKEY_CURRENT_USER, userClsidKey, NULL, modulePath) && ok;
    ok = SetRegistryString(HKEY_CURRENT_USER, userClsidKey + L"\\InprocServer32", NULL, modulePath) && ok;
    ok = SetRegistryString(HKEY_CURRENT_USER, userClsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment") && ok;
    ok = SetRegistryString(HKEY_CURRENT_USER, userProgIdKey, NULL, kProgId) && ok;
    ok = SetRegistryString(HKEY_CURRENT_USER, userProgIdKey + L"\\CLSID", NULL, clsid) && ok;

    // Also register machine-wide when the caller has permission. Per-user
    // registration above keeps installation possible without elevation.
    SetRegistryString(HKEY_CLASSES_ROOT, clsidKey, NULL, modulePath);
    SetRegistryString(HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", NULL, modulePath);
    SetRegistryString(HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    SetRegistryString(HKEY_CLASSES_ROOT, progIdKey, NULL, kProgId);
    SetRegistryString(HKEY_CLASSES_ROOT, progIdKey + L"\\CLSID", NULL, clsid);
    SetRegistryString(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                      L"FriendlyName", L"VB6 MCP Add-in (HTTP/HTTPS)");
    SetRegistryString(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                      L"Description",
                      L"Remote Model Context Protocol server for the Visual Basic 6 IDE");
    SetRegistryDword(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                     L"LoadBehavior", 3);
    SetRegistryDword(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                     L"CommandLineSafe", 1);
    // Also write the per-user form; VB6 variants differ across installations.
    ok = SetRegistryString(HKEY_CURRENT_USER,
                           L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                           L"FriendlyName", L"VB6 MCP Add-in (HTTP/HTTPS)") && ok;
    ok = SetRegistryString(HKEY_CURRENT_USER,
                           L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                           L"Description",
                           L"Remote Model Context Protocol server for the Visual Basic 6 IDE") && ok;
    ok = SetRegistryDword(HKEY_CURRENT_USER,
                          L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                          L"LoadBehavior", 3) && ok;
    ok = SetRegistryDword(HKEY_CURRENT_USER,
                          L"SOFTWARE\\Microsoft\\Visual Basic\\6.0\\Addins\\MCPAddin.Connect",
                          L"CommandLineSafe", 1) && ok;
    // Some VB6 installations surface add-ins from the VisualStudio key.
    SetRegistryString(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Addins\\MCPAddin.Connect",
                      L"FriendlyName", L"VB6 MCP Add-in (HTTP/HTTPS)");
    SetRegistryString(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Addins\\MCPAddin.Connect",
                      L"Description",
                      L"Remote Model Context Protocol server for the Visual Basic 6 IDE");
    SetRegistryDword(HKEY_CURRENT_USER,
                     L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Addins\\MCPAddin.Connect",
                     L"LoadBehavior", 3);
    SetRegistryDword(HKEY_CURRENT_USER,
                     L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Addins\\MCPAddin.Connect",
                     L"CommandLineSafe", 1);
    return ok;
}

} // namespace mcpaddin

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID FAR* result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (clsid != mcpaddin::kAddinClsid) return CLASS_E_CLASSNOTAVAILABLE;
    mcpaddin::AddinClassFactory* factory = new mcpaddin::AddinClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, result);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) {
    return mcpaddin::CanUnloadModule() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void) {
    return mcpaddin::RegisterAddin(true) ? S_OK : E_FAIL;
}

STDAPI DllUnregisterServer(void) {
    return mcpaddin::RegisterAddin(false) ? S_OK : E_FAIL;
}
