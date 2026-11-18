#include "com_ide.h"
#include "log.h"
#include "project_files.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <set>

namespace mcpaddin {

namespace {

const long kVbextCtStdModule = 1;
const long kVbextCtClassModule = 2;
const long kVbextCtMSForm = 3;
const long kVbextCtResFile = 4;
const long kVbextCtVBForm = 5;  // VB6's native VB.Form component type.
const long kVbextCtVBMDIForm = 6;
const long kVbextCtPropPage = 7;
const long kVbextCtUserControl = 8;
const long kVbextCtDocObject = 9;
const long kVbextCtRelatedDocument = 10;
const long kVbextCtActiveXDesigner = 11;
const long kVbextCtDocument = 100;
const long kVbextMtMethod = 1;
const long kVbextMtProperty = 2;
const long kVbextMtVariable = 3;
const long kVbextMtEvent = 4;
const long kVbextMtConst = 5;
const long kVbextScopePrivate = 1;
const long kVbextScopePublic = 2;
const long kVbextScopeFriend = 3;

std::string ArgString(const Json& args, const std::string& name, const std::string& defaultValue = "") {
    const Json& value = args.Get(name);
    return value.IsNull() ? defaultValue : value.AsString(defaultValue);
}

int ArgInt(const Json& args, const std::string& name, int defaultValue = 0) {
    const Json& value = args.Get(name);
    return value.IsNull() ? defaultValue : value.AsInt(defaultValue);
}

bool ArgBool(const Json& args, const std::string& name, bool defaultValue = false) {
    const Json& value = args.Get(name);
    return value.IsNull() ? defaultValue : value.AsBool(defaultValue);
}

Json StringArrayToJson(const std::vector<std::wstring>& values) {
    Json result = Json::ArrayValue();
    for (size_t i = 0; i < values.size(); ++i) result.push_back(Json::StringValue(values[i]));
    return result;
}

ToolResult ComFailure(const std::wstring& operation, HRESULT hr) {
    std::string message = WideToUtf8(operation) + " failed: " + WideToUtf8(HResultMessage(hr)) +
                          " (" + FormatHresult(hr) + ")";
    return ToolResult::Failure(message, "vbide_error");
}

void SetVariantLong(Variant& value, long number) {
    value.get()->vt = VT_I4;
    value.get()->lVal = number;
}

void SetVariantLongByRef(Variant& value, long* number) {
    value.get()->vt = VT_BYREF | VT_I4;
    value.get()->plVal = number;
}

void MakeBstr(Variant& value, const std::wstring& text) {
    value.get()->vt = VT_BSTR;
    value.get()->bstrVal = SysAllocStringLen(text.c_str(), (UINT)text.size());
}

void MakeDispatch(Variant& value, IDispatch* object) {
    value.get()->vt = VT_DISPATCH;
    value.get()->pdispVal = object;
    if (object) object->AddRef();
}

Json VariantValueToJson(const Variant& value) {
    const VARIANT* raw = value.get();
    switch (raw->vt) {
        case VT_BOOL:
            return MakeBool(raw->boolVal != VARIANT_FALSE);
        case VT_I1:
            return MakeNumber((double)raw->cVal);
        case VT_UI1:
            return MakeNumber((double)raw->bVal);
        case VT_I2:
            return MakeNumber((double)raw->iVal);
        case VT_UI2:
            return MakeNumber((double)raw->uiVal);
        case VT_I4:
            return MakeNumber((double)raw->lVal);
        case VT_UI4:
            return MakeNumber((double)raw->ulVal);
        case VT_INT:
            return MakeNumber((double)raw->intVal);
        case VT_UINT:
            return MakeNumber((double)raw->uintVal);
        case VT_R4:
            return MakeNumber((double)raw->fltVal);
        case VT_R8:
            return MakeNumber(raw->dblVal);
        case VT_BSTR:
            return Json::StringValue(value.AsWString());
        case VT_EMPTY:
        case VT_NULL:
            return Json::NullValue();
        case VT_DISPATCH:
            return MakeString("[object]");
        default:
            return Json::StringValue(value.AsWString());
    }
}

void MakeVariantFromJson(Variant& value, const Json& source) {
    if (source.IsBool()) {
        value.get()->vt = VT_BOOL;
        value.get()->boolVal = source.AsBool() ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (source.IsNumber()) {
        value.get()->vt = VT_I4;
        value.get()->lVal = source.AsInt();
    } else if (source.IsNull()) {
        value.get()->vt = VT_EMPTY;
    } else {
        MakeBstr(value, source.AsWString());
    }
}

std::wstring ComponentTypeName(long type) {
    switch (type) {
        case kVbextCtStdModule: return L"module";
        case kVbextCtClassModule: return L"class";
        case kVbextCtMSForm: return L"form";
        case kVbextCtVBForm: return L"form";
        case kVbextCtResFile: return L"resource";
        case kVbextCtVBMDIForm: return L"mdiForm";
        case kVbextCtPropPage: return L"propertyPage";
        case kVbextCtUserControl: return L"userControl";
        case kVbextCtDocObject: return L"docObject";
        case kVbextCtRelatedDocument: return L"relatedDocument";
        case kVbextCtActiveXDesigner: return L"designer";
        case kVbextCtDocument: return L"document";
        default: return L"other";
    }
}

std::string MemberTypeName(long type) {
    switch (type) {
        case kVbextMtMethod: return "method";
        case kVbextMtProperty: return "property";
        case kVbextMtVariable: return "variable";
        case kVbextMtEvent: return "event";
        case kVbextMtConst: return "const";
        default: return "other";
    }
}

std::string ScopeName(long scope) {
    switch (scope) {
        case kVbextScopePrivate: return "private";
        case kVbextScopePublic: return "public";
        case kVbextScopeFriend: return "friend";
        default: return "other";
    }
}

std::vector<std::wstring> ComponentExportExtensions(long type) {
    std::vector<std::wstring> extensions;
    if (type == kVbextCtMSForm || type == kVbextCtVBForm) {
        extensions.push_back(L".frm");
    } else if (type == kVbextCtClassModule) {
        extensions.push_back(L".cls");
    } else if (type == kVbextCtStdModule) {
        extensions.push_back(L".bas");
    } else if (type == kVbextCtVBMDIForm) {
        extensions.push_back(L".frm");
    } else if (type == kVbextCtPropPage) {
        extensions.push_back(L".pag");
    } else if (type == kVbextCtUserControl) {
        extensions.push_back(L".ctl");
    } else if (type == kVbextCtDocObject || type == kVbextCtRelatedDocument) {
        extensions.push_back(L".dob");
    } else if (type == kVbextCtActiveXDesigner) {
        extensions.push_back(L".ctl");
        extensions.push_back(L".pag");
        extensions.push_back(L".dsr");
        extensions.push_back(L".dob");
    } else {
        extensions.push_back(L".tmp");
    }
    return extensions;
}

bool TryExportComponent(IDispatch* component, const std::wstring& tempDirectory,
                        const std::wstring& componentName, long type,
                        std::wstring& exportPath, HRESULT& failure) {
    std::vector<std::wstring> extensions = ComponentExportExtensions(type);
    for (size_t i = 0; i < extensions.size(); ++i) {
        std::wstring candidate = PathJoin(tempDirectory, componentName + extensions[i]);
        Variant pathArg;
        MakeBstr(pathArg, candidate);
        std::vector<Variant*> args;
        args.push_back(&pathArg);
        failure = InvokeMethodNoResult(component, L"Export", args);
        if (SUCCEEDED(failure) && FileExists(candidate)) {
            exportPath = candidate;
            return true;
        }
    }
    return false;
}

bool HasDispatchMember(IDispatch* object, const wchar_t* name) {
    if (!object) return false;
    LPOLESTR names[1] = {const_cast<LPOLESTR>(name)};
    DISPID dispid = DISPID_UNKNOWN;
    return SUCCEEDED(object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid));
}

std::wstring ComponentExtensionForType(long type) {
    switch (type) {
        case kVbextCtMSForm:
        case kVbextCtVBForm: return L".frm";
        case kVbextCtVBMDIForm: return L".frm";
        case kVbextCtClassModule: return L".cls";
        case kVbextCtStdModule: return L".bas";
        case kVbextCtPropPage: return L".pag";
        case kVbextCtUserControl: return L".ctl";
        case kVbextCtDocObject:
        case kVbextCtRelatedDocument: return L".dob";
        default: return std::wstring();
    }
}

std::string ComponentKindFromExtension(const std::wstring& path) {
    std::wstring ext = ToLower(PathExtension(path));
    if (ext == L".frm") return "form";
    if (ext == L".cls") return "class";
    if (ext == L".bas") return "module";
    if (ext == L".ctl") return "usercontrol";
    if (ext == L".pag") return "propertypage";
    if (ext == L".dsr") return "designer";
    return std::string();
}

std::wstring VbpPathValue(const std::wstring& vbpPath, const std::wstring& componentFile) {
    if (NormalizePathForCompare(PathDirectory(vbpPath)) ==
        NormalizePathForCompare(PathDirectory(componentFile))) {
        return PathFileName(componentFile);
    }
    return componentFile;
}

std::wstring SourceVbName(const std::wstring& source, const std::wstring& fallback) {
    size_t marker = source.find(L"Attribute VB_Name");
    if (marker != std::wstring::npos) {
        size_t firstQuote = source.find(L'"', marker);
        size_t secondQuote = firstQuote == std::wstring::npos ? std::wstring::npos :
                             source.find(L'"', firstQuote + 1);
        if (firstQuote != std::wstring::npos && secondQuote != std::wstring::npos) {
            return source.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        }
    }
    return fallback;
}

bool MakeVariantFromJson(const Json& value, Variant& result) {
    if (value.IsString()) {
        MakeBstr(result, value.AsWString());
        return true;
    }
    if (value.IsBool()) {
        result.get()->vt = VT_BOOL;
        result.get()->boolVal = value.AsBool() ? VARIANT_TRUE : VARIANT_FALSE;
        return true;
    }
    if (value.IsNumber()) {
        double number = value.AsDouble();
        if (number == floor(number) && number >= -2147483648.0 && number <= 2147483647.0) {
            result.get()->vt = VT_I4;
            result.get()->lVal = (LONG)number;
        } else {
            result.get()->vt = VT_R8;
            result.get()->dblVal = number;
        }
        return true;
    }
    return false;
}

bool SetDesignerProperty(IDispatch* object, const std::string& propertyName,
                         const Json& value, std::wstring& error) {
    ComPtr<IDispatch> properties = GetObjectProperty(object, L"Properties");
    if (!properties) {
        error = L"The designer object does not expose a Properties collection.";
        return false;
    }
    ComPtr<IDispatch> property = GetCollectionItem(properties.get(), Utf8ToWide(propertyName));
    if (!property) {
        error = L"Designer property was not found: " + Utf8ToWide(propertyName);
        return false;
    }
    Variant variant;
    if (!MakeVariantFromJson(value, variant)) {
        error = L"Unsupported designer property value type for: " + Utf8ToWide(propertyName);
        return false;
    }
    HRESULT hr = PutProperty(property.get(), L"Value", variant);
    if (FAILED(hr)) {
        error = L"Cannot set designer property " + Utf8ToWide(propertyName) + L": " +
                HResultMessage(hr);
        return false;
    }
    return true;
}

bool GetDesignerProperty(IDispatch* object, const std::string& propertyName,
                         Variant& value) {
    ComPtr<IDispatch> properties = GetObjectProperty(object, L"Properties");
    if (!properties) return false;
    ComPtr<IDispatch> property = GetCollectionItem(properties.get(), Utf8ToWide(propertyName));
    if (!property) return false;
    return SUCCEEDED(GetProperty(property.get(), L"Value", value));
}

ComPtr<IDispatch> OpenComponentDesigner(IDispatch* component) {
    if (!component) return ComPtr<IDispatch>();
    std::vector<Variant*> noArgs;
    Variant windowResult;
    if (SUCCEEDED(InvokeMethod(component, L"DesignerWindow", noArgs, windowResult)) &&
        windowResult.GetDispatch()) {
        ComPtr<IDispatch> window(windowResult.GetDispatch(), true);
        Variant visible;
        visible.get()->vt = VT_BOOL;
        visible.get()->boolVal = VARIANT_TRUE;
        PutProperty(window.get(), L"Visible", visible);
        CallVoid(window.get(), L"SetFocus", noArgs);
    }
    return GetObjectProperty(component, L"Designer");
}

bool TryAddVbControl(IDispatch* controls, const std::string& controlType,
                     const std::string& collectionName, Variant& added,
                     std::wstring& error) {
    if (!controls) {
        error = Utf8ToWide(collectionName) + L" is unavailable.";
        return false;
    }
    HRESULT lastHr = E_FAIL;
    for (int attempt = 0; attempt < 3 && !added.GetDispatch(); ++attempt) {
        Variant progIdArg;
        MakeBstr(progIdArg, Utf8ToWide(controlType));
        std::vector<Variant*> addArgs;
        addArgs.push_back(&progIdArg);
        Variant relativeArg;
        Variant beforeArg;
        if (attempt > 0) {
            if (attempt == 1) {
                relativeArg.get()->vt = VT_DISPATCH;
                relativeArg.get()->pdispVal = NULL;
                beforeArg.get()->vt = VT_BOOL;
                beforeArg.get()->boolVal = VARIANT_FALSE;
            } else {
                relativeArg.get()->vt = VT_ERROR;
                relativeArg.get()->scode = DISP_E_PARAMNOTFOUND;
                beforeArg.get()->vt = VT_ERROR;
                beforeArg.get()->scode = DISP_E_PARAMNOTFOUND;
            }
            addArgs.push_back(&relativeArg);
            addArgs.push_back(&beforeArg);
        }
        lastHr = InvokeMethod(controls, L"Add", addArgs, added);
    }
    if (added.GetDispatch()) return true;
    if (SUCCEEDED(lastHr)) {
        error = Utf8ToWide(collectionName) + L".Add returned no VBControl object (VARIANT type " +
                Utf8ToWide(std::to_string((int)added.get()->vt)) + L").";
    } else {
        error = Utf8ToWide(collectionName) + L".Add failed after optional-argument retries: " +
                HResultMessage(lastHr);
    }
    return false;
}

bool IsNumericSelector(const std::string& value) {
    if (value.empty()) return false;
    size_t start = (value[0] == '#') ? 1 : 0;
    if (start >= value.size()) return false;
    for (size_t i = start; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

std::string ReplaceTextSimple(const std::string& source, const std::string& find,
                              const std::string& replace, bool replaceAll, bool matchCase) {
    if (find.empty()) return source;
    std::string haystack = matchCase ? source : ToLowerAscii(source);
    std::string needle = matchCase ? find : ToLowerAscii(find);
    std::string result;
    size_t pos = 0;
    while (pos < source.size()) {
        size_t hit = haystack.find(needle, pos);
        if (hit == std::string::npos) {
            result.append(source, pos, std::string::npos);
            break;
        }
        result.append(source, pos, hit - pos);
        result += replace;
        pos = hit + find.size();
        if (!replaceAll) {
            result.append(source, pos, std::string::npos);
            break;
        }
    }
    return result;
}

bool ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value, std::wstring& out) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, value, NULL, &type, NULL, &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
        if (RegQueryValueExW(key, value, NULL, &type, (LPBYTE)&buffer[0], &bytes) == ERROR_SUCCESS) {
            out = &buffer[0];
            RegCloseKey(key);
            return true;
        }
    }
    RegCloseKey(key);
    return false;
}

std::wstring FindVb6Exe(const ServerConfig& config, const std::string& overridePath) {
    if (!overridePath.empty()) {
        std::wstring value = Utf8ToWide(overridePath);
        if (FileExists(value)) return GetFullPath(value);
    }
    if (!config.vb6Exe.empty() && FileExists(config.vb6Exe)) return GetFullPath(config.vb6Exe);
    std::wstring env = GetEnvVar(L"VB6EXE");
    if (!env.empty() && FileExists(env)) return GetFullPath(env);
    wchar_t search[MAX_PATH * 2] = {0};
    if (SearchPathW(NULL, L"VB6.EXE", NULL, _countof(search), search, NULL) > 0 && FileExists(search)) {
        return GetFullPath(search);
    }
    const wchar_t* keys[] = {
        L"SOFTWARE\\Microsoft\\VisualStudio\\6.0\\Setup\\Microsoft Visual Basic",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\6.0\\Setup\\Microsoft Visual Basic"
    };
    for (size_t i = 0; i < _countof(keys); ++i) {
        std::wstring productDir;
        if (ReadRegistryString(HKEY_LOCAL_MACHINE, keys[i], L"ProductDir", productDir)) {
            std::wstring exe = PathJoin(GetFullPath(productDir), L"VB6.EXE");
            if (FileExists(exe)) return exe;
        }
    }
    return std::wstring();
}

bool RunProcessHidden(const std::wstring& commandLine, const std::wstring& workingDirectory,
                      DWORD timeoutMs, DWORD& exitCode, std::wstring& error) {
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(0);
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, &mutableCommand[0], NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL,
                        workingDirectory.empty() ? NULL : workingDirectory.c_str(),
                        &startup, &process)) {
        error = L"Cannot start process: " + GetLastErrorMessage();
        return false;
    }
    CloseHandle(process.hThread);
    DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        error = L"Process timed out.";
        return false;
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    exitCode = code;
    return true;
}

} // namespace

struct ComIdeBackend::WorkCall {
    std::string name;
    Json arguments;
    ToolResult result;
    HANDLE done;
    volatile LONG refs;
    WorkCall() : done(NULL), refs(2) {}
    void AddRef() { InterlockedIncrement(&refs); }
    void Release() {
        if (InterlockedDecrement(&refs) == 0) {
            if (done) CloseHandle(done);
            delete this;
        }
    }
};

ComIdeBackend::ComIdeBackend(const ServerConfig& config)
    : config_(config), dispatchWindow_(NULL), ideThreadId_(0), connected_(0),
      shuttingDown_(0), shutdownEvent_(CreateEventW(NULL, TRUE, FALSE, NULL)) {
    InitializeCriticalSection(&stateLock_);
}

ComIdeBackend::~ComIdeBackend() {
    Detach();
    if (shutdownEvent_) CloseHandle(shutdownEvent_);
    DeleteCriticalSection(&stateLock_);
}

void ComIdeBackend::Attach(IDispatch* vbe, HWND dispatchWindow) {
    EnterCriticalSection(&stateLock_);
    vbe_.Reset(vbe, true);
    dispatchWindow_ = dispatchWindow;
    ideThreadId_ = GetCurrentThreadId();
    InterlockedExchange(&connected_, vbe ? 1 : 0);
    InterlockedExchange(&shuttingDown_, 0);
    if (shutdownEvent_) ResetEvent(shutdownEvent_);
    lastError_.clear();
    LeaveCriticalSection(&stateLock_);
}

void ComIdeBackend::BeginShutdown() {
    InterlockedExchange(&shuttingDown_, 1);
    InterlockedExchange(&connected_, 0);
    if (shutdownEvent_) SetEvent(shutdownEvent_);
}

void ComIdeBackend::Detach() {
    BeginShutdown();
    EnterCriticalSection(&stateLock_);
    vbe_.Reset();
    dispatchWindow_ = NULL;
    ideThreadId_ = 0;
    LeaveCriticalSection(&stateLock_);
}

bool ComIdeBackend::SameThread() const {
    return ideThreadId_ != 0 && GetCurrentThreadId() == ideThreadId_;
}

bool ComIdeBackend::IsAvailable() {
    return InterlockedCompareExchange(&connected_, 0, 0) != 0;
}

std::string ComIdeBackend::BackendName() {
    return "VB6 IDE (IDispatch/IDTExtensibility2)";
}

void ComIdeBackend::SetLastError(const std::string& error) {
    EnterCriticalSection(&stateLock_);
    lastError_ = error;
    LeaveCriticalSection(&stateLock_);
}

Json ComIdeBackend::Status() {
    Json status = Json::ObjectValue();
    status.Set("connected", MakeBool(IsAvailable()));
    status.Set("ideThreadId", MakeNumber((double)ideThreadId_));
    status.Set("dispatchWindow", MakeNumber((double)(uintptr_t)dispatchWindow_));
    EnterCriticalSection(&stateLock_);
    status.Set("lastError", MakeString(lastError_));
    LeaveCriticalSection(&stateLock_);
    return status;
}

ToolResult ComIdeBackend::CallTool(const std::string& name, const Json& arguments) {
    if (!IsAvailable() || InterlockedCompareExchange(&shuttingDown_, 0, 0) != 0) {
        return ToolResult::Failure("The VB6 add-in is not connected to an IDE instance.", "ide_unavailable");
    }
    if (!shutdownEvent_) {
        return ToolResult::Failure("The VB6 add-in shutdown event is unavailable.", "server_error");
    }
    if (SameThread()) return ExecuteTool(name, arguments);
    WorkCall* call = new WorkCall();
    call->name = name;
    call->arguments = arguments;
    call->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!call->done) {
        call->Release();
        call->Release();
        return ToolResult::Failure("Cannot allocate tool completion event.", "server_error");
    }
    HWND window = NULL;
    EnterCriticalSection(&stateLock_);
    window = dispatchWindow_;
    LeaveCriticalSection(&stateLock_);
    if (!window || !PostMessageW(window, ComIdeBackend::kToolMessage, 0, (LPARAM)call)) {
        call->Release();
        call->Release();
        return ToolResult::Failure("Cannot dispatch the tool call to the VB6 IDE thread.", "ide_unavailable");
    }
    DWORD timeout = (DWORD)config_.ideTimeoutSeconds * 1000;
    HANDLE handles[2] = {call->done, shutdownEvent_};
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, timeout);
    if (wait == WAIT_OBJECT_0 + 1 || InterlockedCompareExchange(&shuttingDown_, 0, 0) != 0) {
        ToolResult result = ToolResult::Failure("The VB6 IDE is shutting down.", "ide_unavailable");
        call->Release();
        return result;
    }
    if (wait != WAIT_OBJECT_0) {
        call->Release();
        return ToolResult::Failure("The VB6 IDE did not process the tool call before the timeout.",
                                   "ide_timeout");
    }
    ToolResult result = call->result;
    call->Release();
    return result;
}

void ComIdeBackend::HandlePostedCall(LPARAM parameter) {
    WorkCall* call = (WorkCall*)parameter;
    if (!call) return;
    if (InterlockedCompareExchange(&shuttingDown_, 0, 0) != 0) {
        call->result = ToolResult::Failure("The VB6 IDE is shutting down.", "ide_unavailable");
    } else {
        try {
            call->result = ExecuteTool(call->name, call->arguments);
        } catch (const std::exception& ex) {
            call->result = ToolResult::Failure(std::string("IDE tool exception: ") + ex.what(), "ide_exception");
        } catch (...) {
            call->result = ToolResult::Failure("Unknown IDE tool exception.", "ide_exception");
        }
    }
    SetEvent(call->done);
    call->Release();
}

ToolResult ComIdeBackend::ExecuteTool(const std::string& name, const Json& args) {
    if (name == "ide_status") return IdeStatus();
    if (name == "project_list") return ProjectList();
    if (name == "project_get") return ProjectGet(args);
    if (name == "project_create") return ProjectCreate(args);
    if (name == "project_scaffold") return ProjectScaffold(args);
    if (name == "project_inspect") return ProjectInspect(args);
    if (name == "project_validate") return ProjectValidate(args);
    if (name == "project_open") return ProjectOpen(args);
    if (name == "project_close") return ProjectClose(args);
    if (name == "project_save") return ProjectSave(args);
    if (name == "project_set_properties") return ProjectSetProperties(args);
    if (name == "project_read_property") return ProjectReadProperty(args);
    if (name == "project_write_property") return ProjectWriteProperty(args);
    if (name == "project_add_toolbox_progid") return ProjectAddToolboxProgId(args);
    if (name == "project_build") return ProjectBuild(args);
    if (name == "project_add_component") return ProjectAddComponent(args);
    if (name == "project_add_form") return ProjectAddForm(args);
    if (name == "project_add_file") return ProjectAddFile(args);
    if (name == "project_create_file") return ProjectCreateFile(args);
    if (name == "project_remove_component") return ProjectRemoveComponent(args);
    if (name == "component_list") return ComponentList(args);
    if (name == "component_get") return ComponentGet(args);
    if (name == "component_get_code") return ComponentGetCode(args);
    if (name == "component_set_code") return ComponentSetCode(args);
    if (name == "component_edit_code") return ComponentEditCode(args);
    if (name == "component_rename") return ComponentRename(args);
    if (name == "component_export") return ComponentExport(args);
    if (name == "component_replace_file") return ComponentReplaceFile(args);
    if (name == "component_properties_list") return ComponentPropertiesList(args);
    if (name == "component_read_property") return ComponentReadProperty(args);
    if (name == "component_write_property") return ComponentWriteProperty(args);
    if (name == "component_action") return ComponentAction(args);
    if (name == "code_get_lines") return CodeGetLines(args);
    if (name == "code_lines_edit") return CodeLinesEdit(args);
    if (name == "code_find") return CodeFind(args);
    if (name == "code_search") return CodeSearch(args);
    if (name == "code_create_event") return CodeCreateEvent(args);
    if (name == "code_structure") return CodeStructure(args);
    if (name == "code_panes_list") return CodePanesList(args);
    if (name == "code_pane_set") return CodePaneSet(args);
    if (name == "windows_list") return WindowsList(args);
    if (name == "window_action") return WindowAction(args);
    if (name == "form_get_text") return FormGetText(args);
    if (name == "form_set_text") return FormSetText(args);
    if (name == "form_list_controls") return FormListControls(args);
    if (name == "form_add_control") return FormAddControl(args);
    if (name == "form_remove_control") return FormRemoveControl(args);
    if (name == "form_set_control_property") return FormSetControlProperty(args);
    if (name == "references_list") return ReferencesList(args);
    if (name == "reference_add") return ReferenceAdd(args);
    if (name == "reference_remove") return ReferenceRemove(args);
    if (name == "active_project_set") return ActiveProjectSet(args);
    if (name == "ide_execute_command") return IdeExecuteCommand(args);
    if (name == "source_read") return SourceRead(args);
    if (name == "source_write") return SourceWrite(args);
    if (name == "source_edit") return SourceEdit(args);
    if (name == "source_list") return SourceList(args);
    if (name == "vb6_batch") return Vb6Batch(args);
    return ToolResult::Failure("Tool is not implemented by the IDE backend: " + name, "not_implemented");
}

namespace {

ComPtr<IDispatch> GetProjectsCollection(IDispatch* vbe) {
    return GetObjectProperty(vbe, L"VBProjects");
}

bool FindProject(IDispatch* vbe, const std::string& selector, ComPtr<IDispatch>& project,
                 std::wstring& error) {
    ComPtr<IDispatch> projects = GetProjectsCollection(vbe);
    if (!projects) {
        error = L"The VBE object does not expose VBProjects.";
        return false;
    }
    if (selector.empty()) {
        project = GetObjectProperty(vbe, L"ActiveVBProject");
        if (!project) project = GetCollectionItem(projects.get(), 1);
        if (!project) {
            error = L"No project is open.";
            return false;
        }
        return true;
    }
    if (IsNumericSelector(selector)) {
        long index = atol(selector[0] == '#' ? selector.c_str() + 1 : selector.c_str());
        project = GetCollectionItem(projects.get(), index);
        if (!project) {
            error = L"Project index was not found: " + Utf8ToWide(selector);
            return false;
        }
        return true;
    }
    long count = GetCollectionCount(projects.get());
    std::wstring wanted = Utf8ToWide(selector);
    std::wstring wantedPath = NormalizePathForCompare(wanted);
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> candidate = GetCollectionItem(projects.get(), i);
        if (!candidate) continue;
        std::wstring name;
        std::wstring fileName;
        GetBstrProperty(candidate.get(), L"Name", name);
        GetBstrProperty(candidate.get(), L"FileName", fileName);
        if (_wcsicmp(name.c_str(), wanted.c_str()) == 0) {
            project = candidate;
            return true;
        }
        if (!fileName.empty() &&
            (NormalizePathForCompare(fileName) == wantedPath ||
             _wcsicmp(PathFileName(fileName).c_str(), wanted.c_str()) == 0)) {
            project = candidate;
            return true;
        }
    }
    error = L"Project not found: " + wanted;
    return false;
}

bool FindComponent(ComPtr<IDispatch>& project, const std::string& selector,
                   ComPtr<IDispatch>& component, std::wstring& error) {
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    if (!components) {
        error = L"The project does not expose VBComponents.";
        return false;
    }
    if (selector.empty()) {
        if (GetCollectionCount(components.get()) == 0) {
            error = L"The project has no components.";
            return false;
        }
        component = GetCollectionItem(components.get(), 1);
        return component.get() != NULL;
    }
    if (IsNumericSelector(selector)) {
        long index = atol(selector[0] == '#' ? selector.c_str() + 1 : selector.c_str());
        component = GetCollectionItem(components.get(), index);
        if (!component) {
            error = L"Component index was not found: " + Utf8ToWide(selector);
            return false;
        }
        return true;
    }
    component = GetCollectionItem(components.get(), Utf8ToWide(selector));
    if (component) return true;
    long count = GetCollectionCount(components.get());
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> candidate = GetCollectionItem(components.get(), i);
        if (!candidate) continue;
        std::wstring name;
        GetBstrProperty(candidate.get(), L"Name", name);
        if (_wcsicmp(name.c_str(), Utf8ToWide(selector).c_str()) == 0) {
            component = candidate;
            return true;
        }
    }
    error = L"Component not found: " + Utf8ToWide(selector);
    return false;
}

bool FindProjectComponent(IDispatch* vbe, const Json& args,
                          ComPtr<IDispatch>& project, ComPtr<IDispatch>& component,
                          std::wstring& error) {
    if (!FindProject(vbe, ArgString(args, "project"), project, error)) return false;
    return FindComponent(project, ArgString(args, "component"), component, error);
}

bool FindComponentCodeModule(IDispatch* vbe, const Json& args,
                             ComPtr<IDispatch>& project, ComPtr<IDispatch>& component,
                             ComPtr<IDispatch>& codeModule, std::wstring& error) {
    if (!FindProjectComponent(vbe, args, project, component, error)) return false;
    codeModule = GetObjectProperty(component.get(), L"CodeModule");
    if (!codeModule) {
        error = L"The component does not expose a CodeModule.";
        return false;
    }
    return true;
}

const char* WindowStateName(long state) {
    switch (state) {
        case 0: return "normal";
        case 1: return "minimize";
        case 2: return "maximize";
        default: return "other";
    }
}

const char* WindowTypeName(long type) {
    switch (type) {
        case 0: return "codeWindow";
        case 1: return "designer";
        case 2: return "browser";
        case 3: return "watch";
        case 4: return "locals";
        case 5: return "immediate";
        case 6: return "projectWindow";
        case 7: return "propertyWindow";
        case 8: return "find";
        case 9: return "findReplace";
        case 10: return "toolbox";
        case 11: return "linkedWindowFrame";
        case 12: return "mainWindow";
        case 13: return "preview";
        case 14: return "colorPalette";
        case 15: return "toolWindow";
        default: return "other";
    }
}

Json DescribeWindow(ComPtr<IDispatch>& window, long index) {
    if (!window) return Json::ObjectValue();
    Json info = Json::ObjectValue();
    std::wstring caption;
    GetBstrProperty(window.get(), L"Caption", caption);
    info.Set("caption", Json::StringValue(caption));
    if (index > 0) info.Set("index", MakeNumber((double)index));
    long number = 0;
    if (GetLongProperty(window.get(), L"Type", number)) {
        info.Set("type", MakeNumber((double)number));
        info.Set("typeName", MakeString(WindowTypeName(number)));
    }
    if (GetLongProperty(window.get(), L"WindowState", number)) {
        info.Set("windowState", MakeNumber((double)number));
        info.Set("windowStateName", MakeString(WindowStateName(number)));
    }
    if (GetLongProperty(window.get(), L"Left", number)) info.Set("left", MakeNumber((double)number));
    if (GetLongProperty(window.get(), L"Top", number)) info.Set("top", MakeNumber((double)number));
    if (GetLongProperty(window.get(), L"Width", number)) info.Set("width", MakeNumber((double)number));
    if (GetLongProperty(window.get(), L"Height", number)) info.Set("height", MakeNumber((double)number));
    if (GetLongProperty(window.get(), L"HWnd", number)) info.Set("hwnd", MakeNumber((double)number));
    bool visible = false;
    if (GetBoolProperty(window.get(), L"Visible", visible)) info.Set("visible", MakeBool(visible));
    ComPtr<IDispatch> frame = GetObjectProperty(window.get(), L"LinkedWindowFrame");
    if (frame) {
        std::wstring frameCaption;
        if (GetBstrProperty(frame.get(), L"Caption", frameCaption)) {
            info.Set("linkedWindowFrame", Json::StringValue(frameCaption));
        }
    }
    return info;
}

Json DescribeCodePane(ComPtr<IDispatch>& pane, long index) {
    if (!pane) return Json::ObjectValue();
    Json info = Json::ObjectValue();
    info.Set("index", MakeNumber((double)index));
    ComPtr<IDispatch> codeModule = GetObjectProperty(pane.get(), L"CodeModule");
    if (codeModule) {
        std::wstring moduleName;
        GetBstrProperty(codeModule.get(), L"Name", moduleName);
        info.Set("moduleName", Json::StringValue(moduleName));
        ComPtr<IDispatch> parent = GetObjectProperty(codeModule.get(), L"Parent");
        if (parent) {
            std::wstring componentName;
            GetBstrProperty(parent.get(), L"Name", componentName);
            info.Set("component", Json::StringValue(componentName));
        }
        long lines = 0;
        if (GetLongProperty(codeModule.get(), L"CountOfLines", lines)) {
            info.Set("lineCount", MakeNumber((double)lines));
        }
    }
    ComPtr<IDispatch> window = GetObjectProperty(pane.get(), L"Window");
    if (window) {
        std::wstring caption;
        if (GetBstrProperty(window.get(), L"Caption", caption)) {
            info.Set("windowCaption", Json::StringValue(caption));
        }
    }
    long number = 0;
    if (GetLongProperty(pane.get(), L"TopLine", number)) info.Set("topLine", MakeNumber((double)number));
    if (GetLongProperty(pane.get(), L"CountOfVisibleLines", number)) {
        info.Set("visibleLineCount", MakeNumber((double)number));
    }
    if (GetLongProperty(pane.get(), L"CodePaneView", number)) {
        info.Set("codePaneView", MakeNumber((double)number));
        info.Set("codePaneViewName", MakeString(number == 0 ? "procedure" : "fullModule"));
    }

    long startLineValue = 0, startColumnValue = 0, endLineValue = 0, endColumnValue = 0;
    Variant startLine, startColumn, endLine, endColumn;
    SetVariantLongByRef(startLine, &startLineValue);
    SetVariantLongByRef(startColumn, &startColumnValue);
    SetVariantLongByRef(endLine, &endLineValue);
    SetVariantLongByRef(endColumn, &endColumnValue);
    std::vector<Variant*> selectionArgs;
    selectionArgs.push_back(&startLine);
    selectionArgs.push_back(&startColumn);
    selectionArgs.push_back(&endLine);
    selectionArgs.push_back(&endColumn);
    Variant selectionResult;
    if (SUCCEEDED(InvokeMethodByRef(pane.get(), L"GetSelection", selectionArgs, selectionResult))) {
        Json selection = Json::ObjectValue();
        selection.Set("startLine", MakeNumber((double)startLineValue));
        selection.Set("startColumn", MakeNumber((double)startColumnValue));
        selection.Set("endLine", MakeNumber((double)endLineValue));
        selection.Set("endColumn", MakeNumber((double)endColumnValue));
        info.Set("selection", selection);
    }
    return info;
}

Json DescribeProperties(IDispatch* properties, bool includeValues) {
    Json info = Json::ObjectValue();
    if (!properties) {
        info.Set("count", MakeNumber(0));
        info.Set("properties", Json::ArrayValue());
        return info;
    }
    long count = GetCollectionCount(properties);
    Json list = Json::ArrayValue();
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> property = GetCollectionItem(properties, i);
        if (!property) continue;
        Json item = Json::ObjectValue();
        std::wstring name;
        GetBstrProperty(property.get(), L"Name", name);
        item.Set("name", Json::StringValue(name));
        long indices = 0;
        if (GetLongProperty(property.get(), L"NumIndices", indices)) {
            item.Set("numIndices", MakeNumber((double)indices));
        }
        if (includeValues && indices == 0) {
            Variant value;
            if (SUCCEEDED(GetProperty(property.get(), L"Value", value))) {
                item.Set("value", VariantValueToJson(value));
            }
        }
        list.push_back(item);
    }
    info.Set("count", MakeNumber((double)count));
    info.Set("properties", list);
    return info;
}

bool FindWindow(IDispatch* vbe, const std::string& selector, ComPtr<IDispatch>& window,
                std::wstring& error) {
    ComPtr<IDispatch> windows = GetObjectProperty(vbe, L"Windows");
    if (!windows) {
        error = L"The VBE object does not expose Windows.";
        return false;
    }
    if (selector.empty()) {
        window = GetObjectProperty(vbe, L"ActiveWindow");
        if (!window) {
            error = L"No active IDE window.";
            return false;
        }
        return true;
    }
    if (IsNumericSelector(selector)) {
        long index = atol(selector[0] == '#' ? selector.c_str() + 1 : selector.c_str());
        window = GetCollectionItem(windows.get(), index);
        if (!window) {
            error = L"Window index was not found: " + Utf8ToWide(selector);
            return false;
        }
        return true;
    }
    long count = GetCollectionCount(windows.get());
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> candidate = GetCollectionItem(windows.get(), i);
        if (!candidate) continue;
        std::wstring caption;
        GetBstrProperty(candidate.get(), L"Caption", caption);
        if (_wcsicmp(caption.c_str(), Utf8ToWide(selector).c_str()) == 0) {
            window = candidate;
            return true;
        }
    }
    error = L"Window not found: " + Utf8ToWide(selector);
    return false;
}

bool FindCodePane(IDispatch* vbe, const std::string& selector, ComPtr<IDispatch>& pane,
                  std::wstring& error) {
    ComPtr<IDispatch> panes = GetObjectProperty(vbe, L"CodePanes");
    if (!panes) {
        error = L"The VBE object does not expose CodePanes.";
        return false;
    }
    if (selector.empty()) {
        pane = GetObjectProperty(vbe, L"ActiveCodePane");
        if (!pane) {
            error = L"No active code pane.";
            return false;
        }
        return true;
    }
    if (IsNumericSelector(selector)) {
        long index = atol(selector[0] == '#' ? selector.c_str() + 1 : selector.c_str());
        pane = GetCollectionItem(panes.get(), index);
        if (!pane) {
            error = L"Code pane index was not found: " + Utf8ToWide(selector);
            return false;
        }
        return true;
    }
    std::wstring wanted = Utf8ToWide(selector);
    long count = GetCollectionCount(panes.get());
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> candidate = GetCollectionItem(panes.get(), i);
        if (!candidate) continue;
        ComPtr<IDispatch> module = GetObjectProperty(candidate.get(), L"CodeModule");
        std::wstring moduleName;
        if (module) GetBstrProperty(module.get(), L"Name", moduleName);
        std::wstring componentName;
        ComPtr<IDispatch> parent = module ? GetObjectProperty(module.get(), L"Parent") : ComPtr<IDispatch>();
        if (parent) GetBstrProperty(parent.get(), L"Name", componentName);
        std::wstring caption;
        ComPtr<IDispatch> window = GetObjectProperty(candidate.get(), L"Window");
        if (window) GetBstrProperty(window.get(), L"Caption", caption);
        if (_wcsicmp(moduleName.c_str(), wanted.c_str()) == 0 ||
            _wcsicmp(componentName.c_str(), wanted.c_str()) == 0 ||
            _wcsicmp(caption.c_str(), wanted.c_str()) == 0) {
            pane = candidate;
            return true;
        }
    }
    error = L"Code pane not found: " + wanted;
    return false;
}

std::string DescribeProjectTypeFromFile(const std::wstring& fileName) {
    if (fileName.empty() || !FileExists(fileName)) return std::string();
    ProjectVbpInfo info;
    std::wstring error;
    if (!ParseVbpFile(fileName, info, error)) return std::string();
    return info.type;
}

Json DescribeComponent(ComPtr<IDispatch>& component, bool includeCode) {
    if (!component) return Json::ObjectValue();
    Json info = Json::ObjectValue();
    std::wstring name;
    GetBstrProperty(component.get(), L"Name", name);
    info.Set("name", Json::StringValue(name));
    std::wstring fileName;
    if (GetBstrProperty(component.get(), L"FileName", fileName)) {
        info.Set("fileName", Json::StringValue(fileName));
    }
    long type = 0;
    GetLongProperty(component.get(), L"Type", type);
    info.Set("type", MakeNumber((double)type));
    info.Set("typeName", Json::StringValue(ComponentTypeName(type)));
    bool saved = false;
    if (GetBoolProperty(component.get(), L"Saved", saved)) info.Set("saved", MakeBool(saved));
    bool designerOpen = false;
    if (GetBoolProperty(component.get(), L"HasOpenDesigner", designerOpen)) {
        info.Set("hasOpenDesigner", MakeBool(designerOpen));
    }
    std::wstring designerId;
    if (GetBstrProperty(component.get(), L"DesignerID", designerId)) {
        info.Set("designerId", Json::StringValue(designerId));
    }
    ComPtr<IDispatch> codeModule = GetObjectProperty(component.get(), L"CodeModule");
    if (codeModule) {
        long lineCount = 0;
        if (GetLongProperty(codeModule.get(), L"CountOfLines", lineCount)) {
            info.Set("codeLineCount", MakeNumber((double)lineCount));
        }
        long declarations = 0;
        if (GetLongProperty(codeModule.get(), L"CountOfDeclarationLines", declarations)) {
            info.Set("declarationLineCount", MakeNumber((double)declarations));
        }
        if (includeCode) {
            std::vector<Variant*> args;
            Variant start;
            Variant count;
            SetVariantLong(start, 1);
            SetVariantLong(count, lineCount);
            args.push_back(&start);
            args.push_back(&count);
            Variant text;
            if (lineCount > 0 && SUCCEEDED(GetProperty(codeModule.get(), L"Lines", args, text))) {
                info.Set("code", Json::StringValue(text.AsWString()));
            } else {
                info.Set("code", MakeString(""));
            }
        }
    }
    return info;
}

Json DescribeProject(ComPtr<IDispatch>& project, bool includeComponents, bool includeCode) {
    if (!project) return Json::ObjectValue();
    Json info = Json::ObjectValue();
    std::wstring name;
    std::wstring fileName;
    std::wstring buildFileName;
    std::wstring description;
    std::wstring helpFile;
    GetBstrProperty(project.get(), L"Name", name);
    GetBstrProperty(project.get(), L"FileName", fileName);
    GetBstrProperty(project.get(), L"BuildFileName", buildFileName);
    GetBstrProperty(project.get(), L"Description", description);
    GetBstrProperty(project.get(), L"HelpFile", helpFile);
    info.Set("name", Json::StringValue(name));
    info.Set("fileName", Json::StringValue(fileName));
    info.Set("buildFileName", Json::StringValue(buildFileName));
    info.Set("description", Json::StringValue(description));
    info.Set("helpFile", Json::StringValue(helpFile));
    long helpContextId = 0;
    if (GetLongProperty(project.get(), L"HelpContextID", helpContextId)) {
        info.Set("helpContextId", MakeNumber((double)helpContextId));
    }
    long mode = 0;
    if (GetLongProperty(project.get(), L"Mode", mode)) info.Set("mode", MakeNumber((double)mode));
    long hostType = 0;
    if (GetLongProperty(project.get(), L"Type", hostType)) {
        info.Set("hostType", MakeNumber((double)hostType));
        const char* typeName = "other";
        if (hostType == 0) typeName = "standard_exe";
        else if (hostType == 1) typeName = "activex_exe";
        else if (hostType == 2) typeName = "activex_dll";
        else if (hostType == 3) typeName = "activex_control";
        info.Set("hostTypeName", MakeString(typeName));
    }
    long startMode = 0;
    if (GetLongProperty(project.get(), L"StartMode", startMode)) {
        info.Set("startMode", MakeNumber((double)startMode));
        info.Set("startModeName", MakeString(startMode == 1 ? "ole_server" : "standalone"));
    }
    long iconState = 0;
    if (GetLongProperty(project.get(), L"IconState", iconState)) {
        info.Set("sourceControlStatus", MakeNumber((double)iconState));
    }
    std::wstring compatibleOleServer;
    if (GetBstrProperty(project.get(), L"CompatibleOleServer", compatibleOleServer)) {
        info.Set("compatibleOleServer", Json::StringValue(compatibleOleServer));
    }
    bool saved = false;
    if (GetBoolProperty(project.get(), L"Saved", saved)) info.Set("saved", MakeBool(saved));
    std::string vbpType = DescribeProjectTypeFromFile(fileName);
    if (!vbpType.empty()) info.Set("projectType", MakeString(vbpType));
    if (!fileName.empty()) {
        ProjectVbpInfo vbp;
        std::wstring parseError;
        if (ParseVbpFile(fileName, vbp, parseError)) {
            info.Set("outputPath", Json::StringValue(ResolveBuildOutput(vbp, fileName, L"", L"")));
        }
    }
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    if (components) {
        long count = GetCollectionCount(components.get());
        info.Set("componentCount", MakeNumber((double)count));
        if (includeComponents) {
            Json list = Json::ArrayValue();
            for (long i = 1; i <= count; ++i) {
                ComPtr<IDispatch> component = GetCollectionItem(components.get(), i);
                list.push_back(DescribeComponent(component, includeCode));
            }
            info.Set("components", list);
        }
    }
    ComPtr<IDispatch> references = GetObjectProperty(project.get(), L"References");
    if (references) {
        Json list = Json::ArrayValue();
        long count = GetCollectionCount(references.get());
        for (long i = 1; i <= count; ++i) {
            ComPtr<IDispatch> reference = GetCollectionItem(references.get(), i);
            if (!reference) continue;
            Json item = Json::ObjectValue();
            std::wstring referenceName;
            std::wstring guid;
            std::wstring fullPath;
            long major = 0;
            long minor = 0;
            bool broken = false;
            GetBstrProperty(reference.get(), L"Name", referenceName);
            GetBstrProperty(reference.get(), L"Guid", guid);
            GetBstrProperty(reference.get(), L"FullPath", fullPath);
            GetLongProperty(reference.get(), L"Major", major);
            GetLongProperty(reference.get(), L"Minor", minor);
            GetBoolProperty(reference.get(), L"IsBroken", broken);
            item.Set("name", Json::StringValue(referenceName));
            item.Set("guid", Json::StringValue(guid));
            item.Set("fullPath", Json::StringValue(fullPath));
            item.Set("major", MakeNumber((double)major));
            item.Set("minor", MakeNumber((double)minor));
            item.Set("isBroken", MakeBool(broken));
            list.push_back(item);
        }
        info.Set("referenceCount", MakeNumber((double)count));
        info.Set("references", list);
    }
    return info;
}

ToolResult ReplaceComponentText(ComPtr<IDispatch>& project, ComPtr<IDispatch>& component,
                                const std::wstring& replacement, bool writeReplacement,
                                std::wstring& backupText, std::wstring& backupEncoding) {
    long type = 0;
    GetLongProperty(component.get(), L"Type", type);
    std::wstring temp;
    if (!CreateTempDirectory(L"export-", temp)) {
        return ToolResult::Failure("Cannot create a temporary component directory.", "filesystem_error");
    }
    std::wstring name;
    GetBstrProperty(component.get(), L"Name", name);
    std::wstring exportPath;
    HRESULT exportFailure = E_FAIL;
    if (!TryExportComponent(component.get(), temp, name, type, exportPath, exportFailure)) {
        RemoveDirectoryRecursiveQuiet(temp);
        return ComFailure(L"Export component", exportFailure);
    }
    std::wstring encoding;
    DWORD ioError = 0;
    if (!ReadTextFile(exportPath, backupText, &encoding, &ioError)) {
        RemoveDirectoryRecursiveQuiet(temp);
        return ToolResult::Failure("Cannot read exported component text: " +
                                   WideToUtf8(GetLastErrorMessage(ioError)), "filesystem_error");
    }
    backupEncoding = encoding;
    if (writeReplacement) {
        if (!WriteTextFileAtomic(exportPath, replacement, encoding, &ioError)) {
            RemoveDirectoryRecursiveQuiet(temp);
            return ToolResult::Failure("Cannot write replacement component text: " +
                                       WideToUtf8(GetLastErrorMessage(ioError)), "filesystem_error");
        }
    }

    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    if (writeReplacement && !HasDispatchMember(components.get(), L"Import")) {
        std::wstring vbpPath;
        GetBstrProperty(project.get(), L"FileName", vbpPath);
        std::wstring componentName;
        GetBstrProperty(component.get(), L"Name", componentName);
        long componentType = 0;
        GetLongProperty(component.get(), L"Type", componentType);
        std::wstring componentFile;
        std::wstring mappingError;
        if (vbpPath.empty() ||
            !FindVbpComponentFile(vbpPath, WideToUtf8(componentName),
                                  ComponentExtensionForType(componentType),
                                  componentFile, mappingError)) {
            RemoveDirectoryRecursiveQuiet(temp);
            if (mappingError.empty()) mappingError = L"The project has no filename.";
            return ToolResult::Failure(
                "This VB6 IDE does not expose VBComponents.Import, and the component file could not be located: " +
                WideToUtf8(mappingError), "vb6_file_mapping_error");
        }
        if (!WriteTextFileAtomic(componentFile, replacement, encoding, &ioError)) {
            RemoveDirectoryRecursiveQuiet(temp);
            return ToolResult::Failure("Cannot write component file: " +
                                       WideToUtf8(GetLastErrorMessage(ioError)), "filesystem_error");
        }
        Json result = DescribeComponent(component, false);
        result.Set("path", Json::StringValue(componentFile));
        result.Set("requiresReload", MakeBool(true));
        result.Set("fallback", MakeString("vb6-file-backed"));
        RemoveDirectoryRecursiveQuiet(temp);
        return ToolResult::Success(result);
    }
    Variant componentArg;
    MakeDispatch(componentArg, component.get());
    std::vector<Variant*> removeArgs;
    removeArgs.push_back(&componentArg);
    HRESULT hr = InvokeMethodNoResult(components.get(), L"Remove", removeArgs);
    if (FAILED(hr)) {
        RemoveDirectoryRecursiveQuiet(temp);
        return ComFailure(L"Remove component for re-import", hr);
    }

    Variant importArg;
    MakeBstr(importArg, exportPath);
    std::vector<Variant*> importArgs;
    importArgs.push_back(&importArg);
    Variant imported;
    hr = InvokeMethod(components.get(), L"Import", importArgs, imported);
    if (FAILED(hr)) {
        // Roll back to the original export so the project does not lose the component.
        if (writeReplacement) WriteTextFileAtomic(exportPath, backupText, backupEncoding);
        Variant rollbackArg;
        MakeBstr(rollbackArg, exportPath);
        std::vector<Variant*> rollbackArgs;
        rollbackArgs.push_back(&rollbackArg);
        Variant rollbackResult;
        InvokeMethod(components.get(), L"Import", rollbackArgs, rollbackResult);
        RemoveDirectoryRecursiveQuiet(temp);
        return ComFailure(L"Import replacement component", hr);
    }
    ComPtr<IDispatch> result(imported.GetDispatch(), true);
    RemoveDirectoryRecursiveQuiet(temp);
    if (!result) return ToolResult::Failure("VBIDE Import did not return a component.", "vbide_error");
    component = result;
    return ToolResult::Success(DescribeComponent(component, false));
}

bool IsPathAllowed(const ServerConfig& config, const std::wstring& path,
                   std::vector<std::wstring> extraRoots) {
    std::wstring full = GetFullPath(path);
    for (size_t i = 0; i < config.allowedRoots.size(); ++i) {
        if (IsPathWithin(full, config.allowedRoots[i])) return true;
    }
    for (size_t i = 0; i < extraRoots.size(); ++i) {
        if (!extraRoots[i].empty() && IsPathWithin(full, extraRoots[i])) return true;
    }
    return false;
}

std::vector<std::wstring> ProjectRoots(IDispatch* vbe) {
    std::vector<std::wstring> roots;
    ComPtr<IDispatch> projects = GetProjectsCollection(vbe);
    if (!projects) return roots;
    long count = GetCollectionCount(projects.get());
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> project = GetCollectionItem(projects.get(), i);
        std::wstring fileName;
        if (project && GetBstrProperty(project.get(), L"FileName", fileName) && !fileName.empty()) {
            roots.push_back(PathDirectory(fileName));
        }
    }
    return roots;
}

bool ExecuteCommandControl(IDispatch* controls, const std::string& command, int id,
                           int depth, int& visited) {
    if (!controls || depth > 5 || visited > 5000) return false;
    long count = GetCollectionCount(controls);
    for (long i = 1; i <= count; ++i) {
        if (++visited > 5000) return false;
        ComPtr<IDispatch> control = GetCollectionItem(controls, i);
        if (!control) continue;
        bool enabled = true;
        GetBoolProperty(control.get(), L"Enabled", enabled);
        if (enabled) {
            bool match = false;
            if (!command.empty()) {
                std::wstring caption;
                if (GetBstrProperty(control.get(), L"Caption", caption)) {
                    std::wstring clean;
                    bool pendingAmp = false;
                    for (size_t c = 0; c < caption.size(); ++c) {
                        if (caption[c] == L'&') {
                            if (pendingAmp) clean.push_back(L'&');
                            pendingAmp = !pendingAmp;
                        } else {
                            clean.push_back(caption[c]);
                            pendingAmp = false;
                        }
                    }
                    if (_wcsicmp(clean.c_str(), Utf8ToWide(command).c_str()) == 0 ||
                        _wcsicmp(caption.c_str(), Utf8ToWide(command).c_str()) == 0) {
                        match = true;
                    }
                }
            }
            if (id > 0) {
                long controlId = 0;
                if (GetLongProperty(control.get(), L"Id", controlId) && controlId == id) match = true;
                if (GetLongProperty(control.get(), L"ID", controlId) && controlId == id) match = true;
            }
            if (match) {
                Variant result;
                HRESULT hr = InvokeMethod(control.get(), L"Execute", std::vector<Variant*>(), result);
                if (SUCCEEDED(hr)) return true;
            }
        }
        ComPtr<IDispatch> children = GetObjectProperty(control.get(), L"Controls");
        if (children && ExecuteCommandControl(children.get(), command, id, depth + 1, visited)) return true;
    }
    return false;
}

} // namespace

ToolResult ComIdeBackend::IdeStatus() {
    if (!vbe_) return ToolResult::Failure("VB6 IDE is not attached.", "ide_unavailable");
    Json result = Json::ObjectValue();
    result.Set("connected", MakeBool(true));
    result.Set("ideThreadId", MakeNumber((double)ideThreadId_));
    std::wstring name;
    if (GetBstrProperty(vbe_.get(), L"Name", name)) result.Set("name", Json::StringValue(name));
    std::wstring fullName;
    if (GetBstrProperty(vbe_.get(), L"FullName", fullName)) {
        result.Set("fullName", Json::StringValue(fullName));
    }
    std::wstring version;
    if (GetBstrProperty(vbe_.get(), L"Version", version)) result.Set("version", Json::StringValue(version));
    std::wstring lastUsedPath;
    if (GetBstrProperty(vbe_.get(), L"LastUsedPath", lastUsedPath)) {
        result.Set("lastUsedPath", Json::StringValue(lastUsedPath));
    }
    std::wstring templatePath;
    if (GetBstrProperty(vbe_.get(), L"TemplatePath", templatePath)) {
        result.Set("templatePath", Json::StringValue(templatePath));
    }
    long number = 0;
    if (GetLongProperty(vbe_.get(), L"ReadOnlyMode", number)) {
        result.Set("readOnlyMode", MakeNumber((double)number));
    }
    if (GetLongProperty(vbe_.get(), L"DisplayModel", number)) {
        result.Set("displayModel", MakeNumber((double)number));
        result.Set("displayModelName", MakeString(number == 1 ? "mdi" : "sdi"));
    }
    ComPtr<IDispatch> active = GetObjectProperty(vbe_.get(), L"ActiveVBProject");
    if (active) {
        std::wstring activeProjectName;
        std::wstring fileName;
        GetBstrProperty(active.get(), L"Name", activeProjectName);
        GetBstrProperty(active.get(), L"FileName", fileName);
        result.Set("activeProject", Json::StringValue(activeProjectName));
        result.Set("activeProjectFile", Json::StringValue(fileName));
    }
    ComPtr<IDispatch> selected = GetObjectProperty(vbe_.get(), L"SelectedVBComponent");
    if (selected) {
        result.Set("selectedComponent", DescribeComponent(selected, false));
    }
    ComPtr<IDispatch> activeCodePane = GetObjectProperty(vbe_.get(), L"ActiveCodePane");
    if (activeCodePane) {
        result.Set("activeCodePane", DescribeCodePane(activeCodePane, 0));
    }
    ComPtr<IDispatch> activeWindow = GetObjectProperty(vbe_.get(), L"ActiveWindow");
    if (activeWindow) {
        result.Set("activeWindow", DescribeWindow(activeWindow, 0));
    }
    ComPtr<IDispatch> mainWindow = GetObjectProperty(vbe_.get(), L"MainWindow");
    if (mainWindow) {
        std::wstring caption;
        if (GetBstrProperty(mainWindow.get(), L"Caption", caption)) result.Set("mainWindowCaption", Json::StringValue(caption));
        bool visible = false;
        if (GetBoolProperty(mainWindow.get(), L"Visible", visible)) result.Set("visible", MakeBool(visible));
    }
    ComPtr<IDispatch> projects = GetProjectsCollection(vbe_.get());
    if (projects) result.Set("projectCount", MakeNumber((double)GetCollectionCount(projects.get())));
    ComPtr<IDispatch> windows = GetObjectProperty(vbe_.get(), L"Windows");
    if (windows) result.Set("windowCount", MakeNumber((double)GetCollectionCount(windows.get())));
    ComPtr<IDispatch> panes = GetObjectProperty(vbe_.get(), L"CodePanes");
    if (panes) result.Set("codePaneCount", MakeNumber((double)GetCollectionCount(panes.get())));
    ComPtr<IDispatch> addins = GetObjectProperty(vbe_.get(), L"Addins");
    if (addins) {
        long addinCount = GetCollectionCount(addins.get());
        Json addinList = Json::ArrayValue();
        for (long i = 1; i <= addinCount; ++i) {
            ComPtr<IDispatch> addin = GetCollectionItem(addins.get(), i);
            if (!addin) continue;
            Json item = Json::ObjectValue();
            std::wstring progId, description, guid;
            GetBstrProperty(addin.get(), L"ProgId", progId);
            GetBstrProperty(addin.get(), L"Description", description);
            GetBstrProperty(addin.get(), L"Guid", guid);
            bool connect = false;
            GetBoolProperty(addin.get(), L"Connect", connect);
            item.Set("progId", Json::StringValue(progId));
            item.Set("description", Json::StringValue(description));
            item.Set("guid", Json::StringValue(guid));
            item.Set("connect", MakeBool(connect));
            addinList.push_back(item);
        }
        result.Set("addinCount", MakeNumber((double)addinCount));
        result.Set("addins", addinList);
    }
    result.Set("configPath", Json::StringValue(config_.configPath));
    result.Set("logPath", Json::StringValue(config_.logPath));
    result.Set("backend", MakeString(BackendName()));
    result.Set("capabilities", MakeString(
        "app-scaffold, project-inspection, project-validation, project-wide-code-search, "
        "batch-orchestration, projects, components, code, code-panes, windows, form-designer, "
        "build, references, commandbars, project-properties, component-properties, source-files"));
    result.Set("toolGroups", GetToolGroups());
    result.Set("recommendedWorkflow", GetRecommendedWorkflow());
    result.Set("aiGuide", MakeString(
        "Start with resources/read uri=vb6://guide/ai-quickstart. For new apps call "
        "project_scaffold; for existing apps call project_inspect, code_search, then targeted "
        "edits. Validate with project_validate and compile with project_build method=commandline."));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectList() {
    ComPtr<IDispatch> projects = GetProjectsCollection(vbe_.get());
    if (!projects) return ToolResult::Failure("VBProjects is unavailable.", "vbide_error");
    Json result = Json::ObjectValue();
    Json list = Json::ArrayValue();
    long count = GetCollectionCount(projects.get());
    ComPtr<IDispatch> active = GetObjectProperty(vbe_.get(), L"ActiveVBProject");
    std::wstring activeName;
    if (active) GetBstrProperty(active.get(), L"Name", activeName);
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> project = GetCollectionItem(projects.get(), i);
        Json item = DescribeProject(project, false, false);
        std::wstring name;
        if (project) GetBstrProperty(project.get(), L"Name", name);
        item.Set("active", MakeBool(!activeName.empty() && _wcsicmp(name.c_str(), activeName.c_str()) == 0));
        list.push_back(item);
    }
    result.Set("count", MakeNumber((double)count));
    result.Set("projects", list);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectGet(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    return ToolResult::Success(DescribeProject(project, true, ArgBool(args, "includeCode", false)));
}

ToolResult ComIdeBackend::ProjectCreate(const Json& args) {
    std::wstring directory = Utf8ToWide(ArgString(args, "directory"));
    std::wstring vbpPath = Utf8ToWide(ArgString(args, "vbpPath"));
    std::string name = ArgString(args, "name");
    if (directory.empty() && !vbpPath.empty()) directory = PathDirectory(GetFullPath(vbpPath));
    if (vbpPath.empty() && !directory.empty()) {
        if (name.empty()) name = "Project1";
        vbpPath = PathJoin(GetFullPath(directory), Utf8ToWide(name) + L".vbp");
    }
    if (directory.empty() || vbpPath.empty()) {
        return ToolResult::Failure("project_create requires directory or vbpPath.", "invalid_arguments");
    }
    directory = GetFullPath(directory);
    vbpPath = GetFullPath(vbpPath);
    if (name.empty()) name = WideToUtf8(PathStem(vbpPath));

    ProjectKind kind = ProjectKind::StandardExe;
    if (!ProjectKindFromString(ArgString(args, "type", "standard_exe"), kind)) {
        return ToolResult::Failure("Unknown project type. Use standard_exe, activex_exe, activex_dll, or activex_control.",
                                   "invalid_arguments");
    }
    ProjectTemplateSpec spec;
    spec.name = name;
    spec.title = ArgString(args, "title", name);
    spec.description = ArgString(args, "description");
    spec.company = ArgString(args, "company");
    spec.product = ArgString(args, "product");
    spec.directory = directory;
    spec.vbpPath = vbpPath;
    spec.kind = kind;
    spec.overwrite = ArgBool(args, "overwrite", false);
    spec.createDefaultStartup = ArgBool(args, "startupModule", true);
    std::vector<std::wstring> files;
    std::wstring error;
    if (!CreateProjectFromTemplate(spec, files, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_create_failed");
    }
    Json result = Json::ObjectValue();
    result.Set("vbpPath", Json::StringValue(vbpPath));
    result.Set("filesCreated", StringArrayToJson(files));
    result.Set("projectType", MakeString(ProjectKindToString(kind)));
    bool open = ArgBool(args, "open", true);
    if (open) {
        Variant pathArg;
        MakeBstr(pathArg, vbpPath);
        std::vector<Variant*> openArgs;
        openArgs.push_back(&pathArg);
        Variant opened;
        HRESULT hr = InvokeMethod(GetProjectsCollection(vbe_.get()).get(), L"Open", openArgs, opened);
        if (FAILED(hr)) {
            ToolResult failure = ToolResult::Failure(
                "Project files were created, but opening the project failed: " +
                WideToUtf8(HResultMessage(hr)),
                "project_open_failed");
            failure.data.Set("vbpPath", Json::StringValue(vbpPath));
            failure.data.Set("filesCreated", StringArrayToJson(files));
            return failure;
        }
        ComPtr<IDispatch> project(opened.GetDispatch(), true);
        result.Set("project", DescribeProject(project, true, false));
    }
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectScaffold(const Json& args) {
    std::wstring directory = Utf8ToWide(ArgString(args, "directory"));
    std::wstring vbpPath = Utf8ToWide(ArgString(args, "vbpPath"));
    std::string name = ArgString(args, "name");
    if (directory.empty() && !vbpPath.empty()) directory = PathDirectory(GetFullPath(vbpPath));
    if (vbpPath.empty() && !directory.empty()) {
        if (name.empty()) name = "Project1";
        vbpPath = PathJoin(GetFullPath(directory), Utf8ToWide(name) + L".vbp");
    }
    if (directory.empty() || vbpPath.empty()) {
        return ToolResult::Failure("project_scaffold requires directory or vbpPath.", "invalid_arguments");
    }
    directory = GetFullPath(directory);
    vbpPath = GetFullPath(vbpPath);
    if (name.empty()) name = WideToUtf8(PathStem(vbpPath));
    if (!IsValidVbIdentifier(name)) {
        return ToolResult::Failure("Project name is not a valid VB6 identifier: " + name,
                                   "invalid_arguments");
    }

    ProjectKind kind = ProjectKind::StandardExe;
    if (!ProjectKindFromString(ArgString(args, "type", "standard_exe"), kind)) {
        return ToolResult::Failure("Unknown project type. Use standard_exe, activex_exe, activex_dll, or activex_control.",
                                   "invalid_arguments");
    }

    ProjectTemplateSpec spec;
    spec.name = name;
    spec.title = ArgString(args, "title", name);
    spec.description = ArgString(args, "description");
    spec.company = ArgString(args, "company");
    spec.product = ArgString(args, "product");
    spec.directory = directory;
    spec.vbpPath = vbpPath;
    spec.startup = ArgString(args, "startup");
    spec.kind = kind;
    spec.overwrite = ArgBool(args, "overwrite", false);
    spec.createDefaultStartup = ArgBool(args, "startupModule", true);

    Json componentResults = Json::ArrayValue();
    Json warnings = Json::ArrayValue();
    std::set<std::string> componentNames;
    std::set<std::string> componentFiles;
    std::wstring error;
    const Json& componentSpecs = args.Get("components");
    if (componentSpecs.IsArray() && !componentSpecs.array().empty()) {
        for (size_t i = 0; i < componentSpecs.array().size(); ++i) {
            const Json& item = componentSpecs.at(i);
            if (!item.IsObject()) {
                return ToolResult::Failure("components[] entries must be objects.", "invalid_arguments");
            }
            ProjectComponentSpec component;
            component.kind = ToLowerAscii(item.Get("kind").AsString());
            component.name = item.Get("name").AsString();
            component.fileName = item.Get("fileName").AsString();
            if (!IEquals(component.kind, "form") && !IEquals(component.kind, "class") &&
                !IEquals(component.kind, "module") && !IEquals(component.kind, "usercontrol") &&
                !IEquals(component.kind, "propertypage")) {
                return ToolResult::Failure("Unsupported scaffold component kind: " + component.kind,
                                           "invalid_arguments");
            }
            if (!IsValidVbIdentifier(component.name)) {
                return ToolResult::Failure("Component name is not a valid VB6 identifier: " + component.name,
                                           "invalid_arguments");
            }
            std::string lowerName = ToLowerAscii(component.name);
            if (!componentNames.insert(lowerName).second) {
                return ToolResult::Failure("Duplicate component name: " + component.name,
                                           "invalid_arguments");
            }

            std::wstring extension;
            std::wstring source = item.Get("source").AsWString();
            if (source.empty()) {
                if (!CreateComponentSource(component.kind, component.name,
                                           item.Get("title").AsString(component.name),
                                           config_.sourceEncoding, source, extension, error)) {
                    return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
                }
            } else {
                std::wstring ignoredSource;
                if (!CreateComponentSource(component.kind, component.name, component.name,
                                           config_.sourceEncoding, ignoredSource, extension, error)) {
                    return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
                }
            }
            if (component.fileName.empty()) component.fileName = component.name;
            if (PathExtension(Utf8ToWide(component.fileName)).empty()) {
                std::wstring fileName = Utf8ToWide(component.fileName);
                std::wstring fileNameWithExtension = PathStem(fileName) + extension;
                std::wstring parent = PathDirectory(fileName);
                component.fileName = parent.empty()
                    ? WideToUtf8(fileNameWithExtension)
                    : WideToUtf8(PathJoin(parent, fileNameWithExtension));
            }
            std::string lowerFile = ToLowerAscii(WideToUtf8(
                GetFullPath(PathJoin(directory, Utf8ToWide(component.fileName)))));
            if (!componentFiles.insert(lowerFile).second) {
                return ToolResult::Failure("Duplicate component file path: " + component.fileName,
                                           "invalid_arguments");
            }

            const Json& controls = item.Get("controls");
            if (controls.IsArray() && !controls.array().empty()) {
                if (!IEquals(component.kind, "form")) {
                    return ToolResult::Failure("Only form components can declare controls.", "invalid_arguments");
                }
                source = NormalizeNewlines(source);
                for (size_t c = 0; c < controls.array().size(); ++c) {
                    const Json& control = controls.at(c);
                    if (!control.IsObject()) {
                        return ToolResult::Failure("controls[] entries must be objects.", "invalid_arguments");
                    }
                    std::vector<std::pair<std::string, Json> > properties;
                    const Json& props = control.Get("properties");
                    if (props.IsObject()) {
                        for (std::map<std::string, Json>::const_iterator it = props.object().begin();
                             it != props.object().end(); ++it) {
                            properties.push_back(std::make_pair(it->first, it->second));
                        }
                    }
                    if (!AddFormControl(source, control.Get("type").AsString(),
                                        control.Get("name").AsString(),
                                        control.Get("container").AsString(),
                                        properties, control.Get("index"), error)) {
                        return ToolResult::Failure("Form " + component.name + ": " + WideToUtf8(error),
                                                   "invalid_arguments");
                    }
                }
            }

            std::string code = item.Get("code").AsString();
            if (!code.empty()) {
                source = NormalizeNewlines(source);
                if (!source.empty() && source[source.size() - 1] != L'\n') source += L"\r\n";
                source += NormalizeNewlines(Utf8ToWide(code));
                if (!source.empty() && source[source.size() - 1] != L'\n') source += L"\r\n";
            }

            component.source = source;
            component.sourceProvided = true;
            spec.components.push_back(component);

            Json described = Json::ObjectValue();
            described.Set("kind", MakeString(component.kind));
            described.Set("name", MakeString(component.name));
            described.Set("fileName", MakeString(component.fileName));
            described.Set("path", Json::StringValue(PathJoin(directory, Utf8ToWide(component.fileName))));
            if (controls.IsArray()) {
                described.Set("controlCount", MakeNumber((double)controls.size()));
            }
            componentResults.push_back(described);
        }
    }

    std::vector<std::wstring> files;
    if (!CreateProjectFromTemplate(spec, files, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_scaffold_failed");
    }

    Json result = Json::ObjectValue();
    result.Set("success", MakeBool(true));
    result.Set("vbpPath", Json::StringValue(vbpPath));
    result.Set("filesCreated", StringArrayToJson(files));
    result.Set("projectType", MakeString(ProjectKindToString(kind)));
    result.Set("components", componentResults);
    result.Set("warnings", warnings);
    result.Set("requiresReload", MakeBool(false));

    bool open = ArgBool(args, "open", true);
    if (open) {
        Variant pathArg;
        MakeBstr(pathArg, vbpPath);
        std::vector<Variant*> openArgs;
        openArgs.push_back(&pathArg);
        Variant opened;
        HRESULT hr = InvokeMethod(GetProjectsCollection(vbe_.get()).get(), L"Open", openArgs, opened);
        if (FAILED(hr)) {
            result.Set("success", MakeBool(false));
            ToolResult failure = ToolResult::Failure(
                "Project files were scaffolded, but opening the project failed: " +
                WideToUtf8(HResultMessage(hr)), "project_open_failed");
            failure.data = result;
            return failure;
        }
        ComPtr<IDispatch> project(opened.GetDispatch(), true);
        result.Set("project", DescribeProject(project, true, false));

        Json referenceResults = Json::ArrayValue();
        const Json& references = args.Get("references");
        if (references.IsArray()) {
            for (size_t i = 0; i < references.array().size(); ++i) {
                const Json& reference = references.at(i);
                if (!reference.IsObject()) {
                    return ToolResult::Failure("references[] entries must be objects.", "invalid_arguments");
                }
                Json refArgs = Json::ObjectValue();
                refArgs.Set("project", Json::StringValue(name));
                if (reference.Has("path")) refArgs.Set("path", reference.Get("path"));
                if (reference.Has("guid")) refArgs.Set("guid", reference.Get("guid"));
                if (reference.Has("major")) refArgs.Set("major", reference.Get("major"));
                if (reference.Has("minor")) refArgs.Set("minor", reference.Get("minor"));
                ToolResult added = ReferenceAdd(refArgs);
                Json item = Json::ObjectValue();
                item.Set("index", MakeNumber((double)(i + 1)));
                item.Set("ok", MakeBool(added.ok));
                item.Set("result", added.data);
                referenceResults.push_back(item);
                if (!added.ok) {
                    result.Set("success", MakeBool(false));
                    result.Set("references", referenceResults);
                    ToolResult failure = ToolResult::Failure(
                        "Project was scaffolded, but one or more references could not be added.",
                        "reference_add_failed");
                    failure.data = result;
                    return failure;
                }
            }
        }
        if (!referenceResults.empty()) result.Set("references", referenceResults);

        const Json& toolbox = args.Get("toolboxProgIds");
        if (toolbox.IsArray() && !toolbox.array().empty()) {
            Json toolboxResults = Json::ArrayValue();
            for (size_t i = 0; i < toolbox.array().size(); ++i) {
                Json toolbarArgs = Json::ObjectValue();
                toolbarArgs.Set("project", Json::StringValue(name));
                toolbarArgs.Set("progId", toolbox.at(i));
                ToolResult added = ProjectAddToolboxProgId(toolbarArgs);
                Json item = Json::ObjectValue();
                item.Set("progId", toolbox.at(i));
                item.Set("ok", MakeBool(added.ok));
                item.Set("result", added.data);
                toolboxResults.push_back(item);
                if (!added.ok) warnings.push_back(MakeString("Failed to register toolbox ProgID " +
                                                             toolbox.at(i).AsString()));
            }
            result.Set("toolboxProgIds", toolboxResults);
        }

        if (ArgBool(args, "build", false)) {
            Json buildArgs = Json::ObjectValue();
            buildArgs.Set("project", Json::StringValue(name));
            buildArgs.Set("method", MakeString(ArgString(args, "buildMethod", "commandline")));
            if (!ArgString(args, "buildOutputPath").empty()) {
                buildArgs.Set("outputPath", Json::StringValue(ArgString(args, "buildOutputPath")));
            }
            buildArgs.Set("timeoutSeconds", MakeNumber((double)ArgInt(args, "buildTimeoutSeconds", 300)));
            ToolResult build = ProjectBuild(buildArgs);
            result.Set("build", build.data);
            if (!build.ok) {
                result.Set("success", MakeBool(false));
                ToolResult failure = ToolResult::Failure(
                    "Project was scaffolded and opened, but the requested build failed.",
                    "build_failed");
                failure.data = result;
                return failure;
            }
        }
    } else {
        const Json& references = args.Get("references");
        if (references.IsArray() && !references.array().empty()) {
            warnings.push_back(MakeString("references require open=true and were not applied."));
        }
    }

    result.Set("warnings", warnings);
    Json nextSteps = Json::ArrayValue();
    nextSteps.push_back(MakeString("Call project_inspect to verify components, members, and controls."));
    nextSteps.push_back(MakeString("Implement remaining code with component_set_code or vb6_batch."));
    nextSteps.push_back(MakeString("Call project_validate, then project_build with method=commandline."));
    result.Set("nextSteps", nextSteps);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectInspect(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    Json result = DescribeProject(project, true, false);
    bool includeMembers = ArgBool(args, "includeMembers", true);
    bool includeControls = ArgBool(args, "includeControls", true);
    long memberCount = 0;
    long controlCount = 0;
    Json components = result.Get("components");
    if (components.IsArray()) {
        for (size_t i = 0; i < components.array().size(); ++i) {
            Json& component = components.at(i);
            std::string componentName = component.Get("name").AsString();
            std::string typeName = component.Get("typeName").AsString();
            if (includeMembers) {
                Json memberArgs = Json::ObjectValue();
                memberArgs.Set("project", args.Get("project"));
                memberArgs.Set("component", Json::StringValue(componentName));
                ToolResult structure = CodeStructure(memberArgs);
                if (structure.ok) {
                    component.Set("members", structure.data.Get("members"));
                    memberCount += (long)structure.data.Get("members").size();
                    component.Set("lineCount", structure.data.Get("lineCount"));
                } else {
                    component.Set("memberError", structure.data);
                }
            }
            if (includeControls && (typeName == "form" || typeName == "mdiForm" ||
                                    typeName == "userControl" || typeName == "propertyPage")) {
                Json formArgs = Json::ObjectValue();
                formArgs.Set("project", args.Get("project"));
                formArgs.Set("component", Json::StringValue(componentName));
                ToolResult controls = FormListControls(formArgs);
                if (controls.ok) {
                    component.Set("controls", controls.data.Get("controls"));
                    controlCount += (long)controls.data.Get("controls").size();
                } else {
                    component.Set("controlError", controls.data);
                }
            }
        }
        result.Set("components", components);
    }
    result.Set("memberCount", MakeNumber((double)memberCount));
    result.Set("controlCount", MakeNumber((double)controlCount));
    result.Set("inspectionHint", MakeString(
        "Use code_get_lines for bodies, code_search for cross-project symbols, and form_get_text only when designer text is required."));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectValidate(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring vbpPath;
    GetBstrProperty(project.get(), L"FileName", vbpPath);
    if (vbpPath.empty()) {
        return ToolResult::Failure("The project must be saved before it can be validated.",
                                   "project_unsaved");
    }
    ProjectVbpInfo info;
    std::wstring parseError;
    if (!ParseVbpFile(vbpPath, info, parseError)) {
        return ToolResult::Failure(WideToUtf8(parseError), "vbp_parse_error");
    }

    Json diagnostics = Json::ArrayValue();
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    auto addDiagnostic = [&](const char* severity, const char* code, const std::string& message,
                             const std::string& component, const std::wstring& path, int line) {
        Json item = Json::ObjectValue();
        item.Set("severity", MakeString(severity));
        item.Set("code", MakeString(code));
        item.Set("message", MakeString(message));
        if (!component.empty()) item.Set("component", MakeString(component));
        if (!path.empty()) item.Set("path", Json::StringValue(path));
        if (line > 0) item.Set("line", MakeNumber((double)line));
        diagnostics.push_back(item);
        if (strcmp(severity, "error") == 0) ++errorCount;
        else if (strcmp(severity, "warning") == 0) ++warningCount;
        else ++infoCount;
    };

    std::set<std::string> componentNames;
    std::set<std::wstring> componentPaths;
    bool hasSubMain = false;
    bool startupResolved = false;
    for (size_t i = 0; i < info.componentEntries.size(); ++i) {
        std::wstring componentPath = Utf8ToWide(info.componentEntries[i]);
        if (!IsAbsolutePath(componentPath)) componentPath = PathJoin(PathDirectory(vbpPath), componentPath);
        componentPath = GetFullPath(componentPath);
        std::wstring normalized = NormalizePathForCompare(componentPath);
        if (!componentPaths.insert(normalized).second) {
            addDiagnostic("warning", "duplicate_component_file",
                          "The VBP lists the same component file more than once.", "", componentPath, 0);
            continue;
        }
        if (!FileExists(componentPath)) {
            addDiagnostic("error", "missing_component_file",
                          "The component file listed in the VBP does not exist.",
                          WideToUtf8(PathStem(componentPath)), componentPath, 0);
            continue;
        }
        std::wstring source;
        DWORD readError = 0;
        if (!ReadTextFile(componentPath, source, NULL, &readError)) {
            addDiagnostic("error", "component_read_failed",
                          "The component file could not be read: " + WideToUtf8(GetLastErrorMessage(readError)),
                          WideToUtf8(PathStem(componentPath)), componentPath, 0);
            continue;
        }
        std::string componentName = WideToUtf8(SourceVbName(source, PathStem(componentPath)));
        if (componentName.empty()) componentName = WideToUtf8(PathStem(componentPath));
        if (!componentNames.insert(ToLowerAscii(componentName)).second) {
            addDiagnostic("error", "duplicate_component_name",
                          "More than one component has this VB_Name.", componentName, componentPath, 0);
        }
        if (source.find(L"Attribute VB_Name") == std::wstring::npos) {
            addDiagnostic("warning", "missing_vb_name",
                          "The component has no Attribute VB_Name declaration.",
                          componentName, componentPath, 0);
        } else if (!_wcsicmp(PathStem(componentPath).c_str(), Utf8ToWide(componentName).c_str())) {
            addDiagnostic("info", "file_name_differs_from_vb_name",
                          "The file stem differs from the component's VB_Name. This is legal but can confuse navigation.",
                          componentName, componentPath, 0);
        }
        if (source.find(L"Option Explicit") == std::wstring::npos) {
            addDiagnostic("warning", "missing_option_explicit",
                          "The component does not contain Option Explicit.",
                          componentName, componentPath, 0);
        }

        std::wstring extension = ToLower(PathExtension(componentPath));
        if (extension == L".frm" || extension == L".ctl" || extension == L".pag") {
            std::vector<FormControlInfo> controls;
            std::wstring formError;
            if (!ListFormControls(source, controls, formError)) {
                addDiagnostic("error", "invalid_form_designer",
                              WideToUtf8(formError), componentName, componentPath, 0);
            } else {
                std::set<std::string> controlKeys;
                for (size_t c = 0; c < controls.size(); ++c) {
                    char keyBuffer[256] = {0};
                    _snprintf_s(keyBuffer, sizeof(keyBuffer), _TRUNCATE, "%s#%d",
                                controls[c].name.c_str(), controls[c].hasIndex ? controls[c].index : -1);
                    if (!controlKeys.insert(keyBuffer).second) {
                        addDiagnostic("error", "duplicate_form_control",
                                      "The form contains a duplicate control name/index pair.",
                                      componentName, componentPath, controls[c].line);
                    }
                }
                infoCount += 0;
            }
        }
        if (source.find(L"Sub Main") != std::wstring::npos) hasSubMain = true;
        if (!info.startup.empty() &&
            (_wcsicmp(Utf8ToWide(info.startup).c_str(), Utf8ToWide(componentName).c_str()) == 0 ||
             _wcsicmp(Utf8ToWide(info.startup).c_str(), PathStem(componentPath).c_str()) == 0 ||
             (_wcsicmp(Utf8ToWide(info.startup).c_str(), L"Sub Main") == 0 && hasSubMain))) {
            startupResolved = true;
        }
    }

    if (info.componentEntries.empty()) {
        addDiagnostic("error", "no_components",
                      "The project has no component entries.", "", vbpPath, 0);
    }
    if (!info.startup.empty() && !startupResolved) {
        addDiagnostic("error", "startup_not_found",
                      "The configured startup object was not found in the project.",
                      info.startup, vbpPath, 0);
    } else if (info.startup.empty() && IEquals(info.type, "Exe")) {
        addDiagnostic("warning", "startup_not_configured",
                      "No explicit startup object is configured; VB6 will use its default project startup behavior.",
                      "", vbpPath, 0);
    }
    if (IEquals(info.type, "Exe") && !hasSubMain && info.startup.empty()) {
        addDiagnostic("info", "no_sub_main",
                      "No Sub Main was found. Set a startup form or add a startup module if the application needs one.",
                      "", vbpPath, 0);
    }

    if (ArgBool(args, "checkReferences", true)) {
        ToolResult references = ReferencesList(args);
        if (!references.ok) {
            addDiagnostic("warning", "references_unavailable",
                          references.data.Get("error").AsString(), "", vbpPath, 0);
        } else {
            const Json& list = references.data.Get("references");
            if (list.IsArray()) {
                for (size_t i = 0; i < list.array().size(); ++i) {
                    const Json& reference = list.at(i);
                    if (reference.Get("isBroken").AsBool(false)) {
                        addDiagnostic("error", "broken_reference",
                                      "A project reference is broken: " +
                                      reference.Get("name").AsString(), "", vbpPath, 0);
                    }
                }
            }
        }
    }

    bool strict = ArgBool(args, "strict", false);
    bool valid = errorCount == 0 && (!strict || warningCount == 0);
    Json result = Json::ObjectValue();
    result.Set("valid", MakeBool(valid));
    result.Set("projectType", MakeString(info.type));
    result.Set("vbpPath", Json::StringValue(GetFullPath(vbpPath)));
    result.Set("componentCount", MakeNumber((double)info.componentEntries.size()));
    result.Set("startup", MakeString(info.startup));
    result.Set("errorCount", MakeNumber((double)errorCount));
    result.Set("warningCount", MakeNumber((double)warningCount));
    result.Set("infoCount", MakeNumber((double)infoCount));
    result.Set("diagnostics", diagnostics);
    result.Set("hint", MakeString(
        "Structural validation does not resolve types or compile code. Run project_build with method=commandline for compiler diagnostics."));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectOpen(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(ArgString(args, "path")));
    if (!FileExists(path)) return ToolResult::Failure("VBP file does not exist: " + WideToUtf8(path),
                                                      "file_not_found");
    ComPtr<IDispatch> projects = GetProjectsCollection(vbe_.get());
    Variant pathArg;
    MakeBstr(pathArg, path);
    std::vector<Variant*> openArgs;
    openArgs.push_back(&pathArg);
    Variant opened;
    HRESULT hr = InvokeMethod(projects.get(), L"Open", openArgs, opened);
    if (FAILED(hr)) return ComFailure(L"Open project", hr);
    ComPtr<IDispatch> project(opened.GetDispatch(), true);
    return ToolResult::Success(DescribeProject(project, true, false));
}

ToolResult ComIdeBackend::ProjectClose(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    if (ArgBool(args, "save", true)) {
        std::wstring fileName;
        GetBstrProperty(project.get(), L"FileName", fileName);
        if (!fileName.empty()) {
            Variant pathArg;
            MakeBstr(pathArg, fileName);
            std::vector<Variant*> saveArgs;
            saveArgs.push_back(&pathArg);
            HRESULT saveHr = InvokeMethodNoResult(project.get(), L"SaveAs", saveArgs);
            if (FAILED(saveHr)) return ComFailure(L"Save project before closing", saveHr);
        }
    }
    Variant projectArg;
    MakeDispatch(projectArg, project.get());
    std::vector<Variant*> removeArgs;
    removeArgs.push_back(&projectArg);
    HRESULT hr = InvokeMethodNoResult(GetProjectsCollection(vbe_.get()).get(), L"Remove", removeArgs);
    if (FAILED(hr)) return ComFailure(L"Close project", hr);
    Json result = Json::ObjectValue();
    result.Set("closed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectSave(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring target = Utf8ToWide(ArgString(args, "path"));
    if (target.empty()) GetBstrProperty(project.get(), L"FileName", target);
    if (target.empty()) {
        return ToolResult::Failure("The project has no filename. Supply path.", "invalid_arguments");
    }
    target = GetFullPath(target);
    Variant pathArg;
    MakeBstr(pathArg, target);
    std::vector<Variant*> saveArgs;
    saveArgs.push_back(&pathArg);
    HRESULT hr = InvokeMethodNoResult(project.get(), L"SaveAs", saveArgs);
    if (FAILED(hr)) return ComFailure(L"Save project", hr);
    return ToolResult::Success(DescribeProject(project, true, false));
}

ToolResult ComIdeBackend::ProjectSetProperties(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    Json applied = Json::ArrayValue();
    Json unsupported = Json::ArrayValue();
    struct PropertySpec {
        const char* name;
        const wchar_t* vbName;
        int kind; // 0 string, 1 long
    };
    PropertySpec specs[] = {
        {"name", L"Name", 0},
        {"description", L"Description", 0},
        {"helpFile", L"HelpFile", 0},
        {"helpContextId", L"HelpContextID", 1},
        {"buildFileName", L"BuildFileName", 0},
        {"compatibleOleServer", L"CompatibleOleServer", 0}
    };
    for (size_t i = 0; i < _countof(specs); ++i) {
        if (!args.Has(specs[i].name)) continue;
        HRESULT hr;
        if (specs[i].kind == 1) {
            Variant value;
            SetVariantLong(value, ArgInt(args, specs[i].name));
            hr = PutProperty(project.get(), specs[i].vbName, value);
        } else {
            Variant value;
            MakeBstr(value, args.Get(specs[i].name).AsWString());
            hr = PutProperty(project.get(), specs[i].vbName, value);
        }
        if (SUCCEEDED(hr)) applied.push_back(MakeString(specs[i].name));
        else return ComFailure(L"Set project property " + Utf8ToWide(specs[i].name), hr);
    }
    if (args.Has("startMode")) {
        std::string startMode = ToLowerAscii(ArgString(args, "startMode"));
        long value = 0;
        if (startMode == "standalone" || startMode == "stand_alone") value = 0;
        else if (startMode == "ole_server" || startMode == "oleserver") value = 1;
        else return ToolResult::Failure(
            "startMode must be standalone or ole_server.", "invalid_arguments");
        Variant variant;
        SetVariantLong(variant, value);
        HRESULT hr = PutProperty(project.get(), L"StartMode", variant);
        if (FAILED(hr)) return ComFailure(L"Set project StartMode", hr);
        applied.push_back(MakeString("startMode"));
    }
    if (args.Has("title")) unsupported.push_back(MakeString("title (stored in VBP; not exposed by VBIDE automation)"));
    Json result = DescribeProject(project, true, false);
    result.Set("applied", applied);
    result.Set("unsupported", unsupported);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectReadProperty(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::string section = ArgString(args, "section");
    std::string key = ArgString(args, "key");
    if (section.empty() || key.empty()) {
        return ToolResult::Failure("project_read_property requires section and key.",
                                   "invalid_arguments");
    }
    Variant sectionArg, keyArg;
    MakeBstr(sectionArg, Utf8ToWide(section));
    MakeBstr(keyArg, Utf8ToWide(key));
    std::vector<Variant*> callArgs;
    callArgs.push_back(&sectionArg);
    callArgs.push_back(&keyArg);
    Variant value;
    HRESULT hr = InvokeMethod(project.get(), L"ReadProperty", callArgs, value);
    if (FAILED(hr)) return ComFailure(L"Read project property", hr);
    Json result = Json::ObjectValue();
    result.Set("section", MakeString(section));
    result.Set("key", MakeString(key));
    result.Set("value", Json::StringValue(value.AsWString()));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectWriteProperty(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::string section = ArgString(args, "section");
    std::string key = ArgString(args, "key");
    if (section.empty() || key.empty()) {
        return ToolResult::Failure("project_write_property requires section and key.",
                                   "invalid_arguments");
    }
    Variant sectionArg, keyArg, valueArg;
    MakeBstr(sectionArg, Utf8ToWide(section));
    MakeBstr(keyArg, Utf8ToWide(key));
    MakeBstr(valueArg, args.Get("value").AsWString());
    std::vector<Variant*> callArgs;
    callArgs.push_back(&sectionArg);
    callArgs.push_back(&keyArg);
    callArgs.push_back(&valueArg);
    HRESULT hr = InvokeMethodNoResult(project.get(), L"WriteProperty", callArgs);
    if (FAILED(hr)) return ComFailure(L"Write project property", hr);
    Json result = Json::ObjectValue();
    result.Set("section", MakeString(section));
    result.Set("key", MakeString(key));
    result.Set("value", args.Get("value"));
    result.Set("written", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectAddToolboxProgId(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::string progId = ArgString(args, "progId");
    if (progId.empty()) {
        return ToolResult::Failure("project_add_toolbox_progid requires progId.",
                                   "invalid_arguments");
    }
    Variant progIdArg;
    MakeBstr(progIdArg, Utf8ToWide(progId));
    std::vector<Variant*> callArgs;
    callArgs.push_back(&progIdArg);
    Variant fileArg;
    std::string fileName = ArgString(args, "fileName");
    if (!fileName.empty()) {
        MakeBstr(fileArg, GetFullPath(Utf8ToWide(fileName)));
        callArgs.push_back(&fileArg);
    }
    HRESULT hr = InvokeMethodNoResult(project.get(), L"AddToolboxProgID", callArgs);
    if (FAILED(hr)) return ComFailure(L"Add toolbox ProgID", hr);
    Json result = Json::ObjectValue();
    result.Set("progId", MakeString(progId));
    if (!fileName.empty()) result.Set("fileName", Json::StringValue(GetFullPath(Utf8ToWide(fileName))));
    result.Set("added", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectBuild(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring vbpPath;
    GetBstrProperty(project.get(), L"FileName", vbpPath);
    if (vbpPath.empty()) {
        return ToolResult::Failure("The project must be saved before it can be built.", "project_unsaved");
    }
    std::wstring saveTarget = vbpPath;
    Variant saveArg;
    MakeBstr(saveArg, saveTarget);
    std::vector<Variant*> saveArgs;
    saveArgs.push_back(&saveArg);
    HRESULT hr = InvokeMethodNoResult(project.get(), L"SaveAs", saveArgs);
    if (FAILED(hr)) return ComFailure(L"Save project before build", hr);
    ProjectVbpInfo vbp;
    if (!ParseVbpFile(vbpPath, vbp, error)) return ToolResult::Failure(WideToUtf8(error), "vbp_parse_error");
    std::string method = ToLowerAscii(ArgString(args, "method", "ide"));
    std::wstring outputPath = Utf8ToWide(ArgString(args, "outputPath"));
    std::wstring outputDirectory = Utf8ToWide(ArgString(args, "outputDirectory"));
    std::wstring expectedOutput = ResolveBuildOutput(vbp, vbpPath, outputPath, outputDirectory);
    if (method == "commandline") {
        std::wstring vb6 = FindVb6Exe(config_, ArgString(args, "vb6Exe"));
        if (vb6.empty()) {
            return ToolResult::Failure("VB6.EXE was not found. Set [ide] vb6Exe or the vb6Exe argument.",
                                       "vb6_not_found");
        }
        std::wstring logPath = Utf8ToWide(ArgString(args, "logPath"));
        bool tempLog = false;
        if (logPath.empty()) {
            std::wstring tempDir;
            if (!CreateTempDirectory(L"build-", tempDir)) {
                return ToolResult::Failure("Cannot create a temporary build log directory.",
                                           "filesystem_error");
            }
            logPath = PathJoin(tempDir, L"compile.log");
            tempLog = true;
        } else {
            logPath = GetFullPath(logPath);
            EnsureDirectory(PathDirectory(logPath));
        }
        std::wstring command = L"\"" + vb6 + L"\" /make \"" + vbpPath + L"\" /out \"" + logPath + L"\"";
        if (!outputDirectory.empty()) {
            std::wstring dir = IsAbsolutePath(outputDirectory) ? outputDirectory :
                               PathJoin(PathDirectory(vbpPath), outputDirectory);
            EnsureDirectory(dir);
            command += L" /outdir \"" + dir + L"\"";
        }
        int timeout = ArgInt(args, "timeoutSeconds", 300);
        DWORD exitCode = 0;
        bool ran = RunProcessHidden(command, PathDirectory(vbpPath), (DWORD)timeout * 1000, exitCode, error);
        std::wstring compilerOutput;
        ReadTextFile(logPath, compilerOutput, NULL, NULL);
        bool compilerOutputTruncated = false;
        if (compilerOutput.size() > 256 * 1024) {
            compilerOutput.resize(256 * 1024);
            compilerOutputTruncated = true;
        }
        Json result = Json::ObjectValue();
        result.Set("method", MakeString("commandline"));
        result.Set("vb6Exe", Json::StringValue(vb6));
        result.Set("vbpPath", Json::StringValue(vbpPath));
        result.Set("exitCode", MakeNumber((double)exitCode));
        result.Set("compilerLogPath", Json::StringValue(tempLog ? L"" : logPath));
        result.Set("compilerOutput", Json::StringValue(compilerOutput));
        result.Set("compilerOutputTruncated", MakeBool(compilerOutputTruncated));
        result.Set("outputPath", Json::StringValue(expectedOutput));
        result.Set("outputExists", MakeBool(FileExists(expectedOutput)));
        result.Set("outputSize", MakeNumber((double)FileSize(expectedOutput)));
        if (tempLog) RemoveDirectoryRecursiveQuiet(PathDirectory(logPath));
        if (!ran) {
            result.Set("success", MakeBool(false));
            ToolResult failure = ToolResult::Failure(WideToUtf8(error), "build_failed");
            failure.data = result;
            return failure;
        }
        std::string lowerLog = ToLowerAscii(WideToUtf8(compilerOutput));
        bool logError = lowerLog.find("error") != std::string::npos && lowerLog.find("0 error") == std::string::npos;
        bool success = exitCode == 0 && FileExists(expectedOutput) && !logError;
        result.Set("success", MakeBool(success));
        if (!success) {
            ToolResult failure = ToolResult::Failure("VB6 command-line build failed. Inspect compilerOutput.",
                                                     "build_failed");
            failure.data = result;
            return failure;
        }
        return ToolResult::Success(result);
    }
    if (!outputPath.empty() || !outputDirectory.empty()) {
        EnsureDirectory(PathDirectory(expectedOutput));
        Variant pathArg;
        MakeBstr(pathArg, expectedOutput);
        PutProperty(project.get(), L"BuildFileName", pathArg);
    }
    hr = InvokeMethodNoResult(project.get(), L"MakeCompiledFile", std::vector<Variant*>());
    Json result = Json::ObjectValue();
    result.Set("method", MakeString("ide"));
    result.Set("vbpPath", Json::StringValue(vbpPath));
    result.Set("outputPath", Json::StringValue(expectedOutput));
    result.Set("outputExists", MakeBool(FileExists(expectedOutput)));
    result.Set("outputSize", MakeNumber((double)FileSize(expectedOutput)));
    result.Set("success", MakeBool(SUCCEEDED(hr) && FileExists(expectedOutput)));
    if (FAILED(hr)) {
        ToolResult failure = ComFailure(L"MakeCompiledFile", hr);
        failure.data = result;
        return failure;
    }
    if (!FileExists(expectedOutput)) {
        ToolResult failure = ToolResult::Failure("Build completed but the expected output file was not found.",
                                                 "build_output_missing");
        failure.data = result;
        return failure;
    }
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectAddComponent(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    if (!components) return ToolResult::Failure("VBComponents is unavailable.", "vbide_error");
    std::string kind = ToLowerAscii(ArgString(args, "kind"));
    Variant componentResult;
    HRESULT hr = E_FAIL;
    if (kind == "module" || kind == "class" || kind == "form") {
        long type = kind == "module" ? kVbextCtStdModule :
                    (kind == "class" ? kVbextCtClassModule : kVbextCtVBForm);
        Variant typeArg;
        SetVariantLong(typeArg, type);
        std::vector<Variant*> addArgs;
        addArgs.push_back(&typeArg);
        hr = InvokeMethod(components.get(), L"Add", addArgs, componentResult);
        if (FAILED(hr) && kind == "form") {
            Variant legacyTypeArg;
            SetVariantLong(legacyTypeArg, kVbextCtMSForm);
            std::vector<Variant*> legacyArgs;
            legacyArgs.push_back(&legacyTypeArg);
            hr = InvokeMethod(components.get(), L"Add", legacyArgs, componentResult);
        }
    } else {
        std::string progId = ArgString(args, "progId");
        if (progId.empty()) {
            if (kind == "usercontrol") progId = "VB.UserControl";
            else if (kind == "propertypage") progId = "VB.PropertyPage";
        }
        if (progId.empty()) {
            return ToolResult::Failure("AddCustom requires progId for this component kind.", "invalid_arguments");
        }
        Variant progArg;
        MakeBstr(progArg, Utf8ToWide(progId));
        std::vector<Variant*> addArgs;
        addArgs.push_back(&progArg);
        hr = InvokeMethod(components.get(), L"AddCustom", addArgs, componentResult);
    }
    if (FAILED(hr)) return ComFailure(L"Add component", hr);
    ComPtr<IDispatch> component(componentResult.GetDispatch(), true);
    std::string name = ArgString(args, "name");
    if (!name.empty()) {
        if (!IsValidVbIdentifier(name)) {
            return ToolResult::Failure("Component name is not a valid VB6 identifier.", "invalid_arguments");
        }
        Variant nameArg;
        MakeBstr(nameArg, Utf8ToWide(name));
        hr = PutProperty(component.get(), L"Name", nameArg);
        if (FAILED(hr)) return ComFailure(L"Rename new component", hr);
    }
    std::string code = ArgString(args, "code");
    if (!code.empty()) {
        ComPtr<IDispatch> codeModule = GetObjectProperty(component.get(), L"CodeModule");
        Variant codeArg;
        MakeBstr(codeArg, Utf8ToWide(code));
        std::vector<Variant*> codeArgs;
        codeArgs.push_back(&codeArg);
        hr = InvokeMethodNoResult(codeModule.get(), L"AddFromString", codeArgs);
        if (FAILED(hr)) return ComFailure(L"Add initial component code", hr);
    }
    return ToolResult::Success(DescribeComponent(component, true));
}

ToolResult ComIdeBackend::ProjectAddForm(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring vbpPath;
    GetBstrProperty(project.get(), L"FileName", vbpPath);
    if (vbpPath.empty()) {
        return ToolResult::Failure("The project must be saved before adding a form.", "project_unsaved");
    }

    std::string name = ArgString(args, "name");
    if (!IsValidVbIdentifier(name)) {
        return ToolResult::Failure("Form name is not a valid VB6 identifier.", "invalid_arguments");
    }

    std::wstring fileName = Utf8ToWide(ArgString(args, "fileName"));
    if (fileName.empty()) fileName = Utf8ToWide(name) + L".frm";
    if (!IsAbsolutePath(fileName)) fileName = PathJoin(PathDirectory(vbpPath), fileName);
    fileName = GetFullPath(fileName);

    std::wstring source = args.Get("source").AsWString();
    std::wstring extension = L".frm";
    if (source.empty()) {
        std::string title = ArgString(args, "title", name);
        if (!CreateComponentSource("form", name, title, config_.sourceEncoding,
                                   source, extension, error)) {
            return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
        }
        std::string code = ArgString(args, "code");
        if (!code.empty()) source += L"\r\n" + Utf8ToWide(code) + L"\r\n";
    }

    bool existed = FileExists(fileName);
    if (existed && !ArgBool(args, "overwrite", false)) {
        return ToolResult::Failure("Form file already exists: " + WideToUtf8(fileName),
                                   "file_exists");
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(fileName, source, config_.sourceEncoding, &writeError)) {
        return ToolResult::Failure("Cannot write form file: " +
                                   WideToUtf8(GetLastErrorMessage(writeError)),
                                   "filesystem_error");
    }

    if (!UpdateVbpComponentEntry(vbpPath, "form", name, VbpPathValue(vbpPath, fileName),
                                 ArgBool(args, "startup", false), error)) {
        if (!existed) DeleteFileQuiet(fileName);
        return ToolResult::Failure(WideToUtf8(error), "project_file_update_failed");
    }

    Json result = Json::ObjectValue();
    result.Set("name", MakeString(name));
    result.Set("path", Json::StringValue(fileName));
    result.Set("vbpPath", Json::StringValue(vbpPath));
    result.Set("startup", MakeBool(ArgBool(args, "startup", false)));
    result.Set("requiresReload", MakeBool(true));
    result.Set("fallback", MakeString("vb6-file-backed"));
    result.Set("message", MakeString(
        "The form and VBP were updated. Reopen the project in VB6 to load the designer."));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectAddFile(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring path = GetFullPath(Utf8ToWide(ArgString(args, "path")));
    if (!FileExists(path)) return ToolResult::Failure("Component file does not exist.", "file_not_found");
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    Variant pathArg;
    MakeBstr(pathArg, path);
    std::vector<Variant*> importArgs;
    importArgs.push_back(&pathArg);
    Variant imported;
    HRESULT hr = InvokeMethod(components.get(), L"Import", importArgs, imported);
    if (SUCCEEDED(hr)) {
        ComPtr<IDispatch> component(imported.GetDispatch(), true);
        return ToolResult::Success(DescribeComponent(component, true));
    }

    std::wstring vbpPath;
    GetBstrProperty(project.get(), L"FileName", vbpPath);
    std::wstring source;
    DWORD readError = 0;
    if (vbpPath.empty() || !ReadTextFile(path, source, NULL, &readError)) {
        return ComFailure(L"Import component", hr);
    }
    std::string kind = ComponentKindFromExtension(path);
    if (kind.empty()) return ComFailure(L"Import component", hr);
    std::wstring name = SourceVbName(source, PathStem(path));
    std::wstring updateError;
    if (!UpdateVbpComponentEntry(vbpPath, kind, WideToUtf8(name),
                                 VbpPathValue(vbpPath, path), false, updateError)) {
        return ToolResult::Failure(WideToUtf8(updateError), "project_file_update_failed");
    }
    Json result = Json::ObjectValue();
    result.Set("name", Json::StringValue(name));
    result.Set("path", Json::StringValue(path));
    result.Set("vbpPath", Json::StringValue(vbpPath));
    result.Set("requiresReload", MakeBool(true));
    result.Set("fallback", MakeString("vb6-file-backed"));
    result.Set("importError", MakeString(WideToUtf8(HResultMessage(hr))));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectCreateFile(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::wstring vbpPath;
    GetBstrProperty(project.get(), L"FileName", vbpPath);
    if (vbpPath.empty()) return ToolResult::Failure("The project must be saved first.", "project_unsaved");
    std::string kind = ArgString(args, "kind");
    std::string name = ArgString(args, "name");
    if (!IsValidVbIdentifier(name)) {
        return ToolResult::Failure("Component name is not a valid VB6 identifier.", "invalid_arguments");
    }
    std::wstring fileName = Utf8ToWide(ArgString(args, "fileName"));
    std::wstring ext;
    std::wstring source = args.Get("source").AsWString();
    std::wstring defaultSource;
    if (!CreateComponentSource(kind, name, name, config_.sourceEncoding, defaultSource, ext, error)) {
        return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
    }
    if (fileName.empty()) {
        fileName = Utf8ToWide(name) + ext;
    }
    if (PathDirectory(fileName) == L"") {
        fileName = PathJoin(PathDirectory(vbpPath), fileName);
    } else if (!IsAbsolutePath(fileName)) {
        fileName = PathJoin(PathDirectory(vbpPath), fileName);
    }
    if (source.empty()) {
        source = defaultSource;
        std::string code = ArgString(args, "code");
        if (!code.empty()) source += L"\r\n" + Utf8ToWide(code) + L"\r\n";
    }
    if (!ArgBool(args, "overwrite", false) && FileExists(fileName)) {
        return ToolResult::Failure("Component file already exists: " + WideToUtf8(fileName),
                                   "file_exists");
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(fileName, source, config_.sourceEncoding, &writeError)) {
        return ToolResult::Failure("Cannot write component file: " + WideToUtf8(GetLastErrorMessage(writeError)),
                                   "filesystem_error");
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(fileName));
    bool import = ArgBool(args, "import", true);
    if (import) {
        ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
        Variant pathArg;
        MakeBstr(pathArg, fileName);
        std::vector<Variant*> importArgs;
        importArgs.push_back(&pathArg);
        Variant imported;
        HRESULT hr = InvokeMethod(components.get(), L"Import", importArgs, imported);
        if (SUCCEEDED(hr)) {
            ComPtr<IDispatch> component(imported.GetDispatch(), true);
            result.Set("component", DescribeComponent(component, false));
        } else {
            std::wstring updateError;
            if (vbpPath.empty() ||
                !UpdateVbpComponentEntry(vbpPath, kind, name, VbpPathValue(vbpPath, fileName),
                                         false, updateError)) {
                return ComFailure(L"Import generated component", hr);
            }
            result.Set("requiresReload", MakeBool(true));
            result.Set("fallback", MakeString("vb6-file-backed"));
            result.Set("importError", MakeString(WideToUtf8(HResultMessage(hr))));
        }
    }
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ProjectRemoveComponent(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    if (ArgBool(args, "saveFile", false)) {
        std::wstring path = Utf8ToWide(ArgString(args, "exportPath"));
        if (path.empty()) return ToolResult::Failure("exportPath is required when saveFile is true.", "invalid_arguments");
        path = GetFullPath(path);
        EnsureDirectory(PathDirectory(path));
        Variant pathArg;
        MakeBstr(pathArg, path);
        std::vector<Variant*> exportArgs;
        exportArgs.push_back(&pathArg);
        HRESULT exportHr = InvokeMethodNoResult(component.get(), L"Export", exportArgs);
        if (FAILED(exportHr)) return ComFailure(L"Export component before removal", exportHr);
    }
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    Variant componentArg;
    MakeDispatch(componentArg, component.get());
    std::vector<Variant*> removeArgs;
    removeArgs.push_back(&componentArg);
    HRESULT hr = InvokeMethodNoResult(components.get(), L"Remove", removeArgs);
    if (FAILED(hr)) return ComFailure(L"Remove component", hr);
    Json result = Json::ObjectValue();
    result.Set("removed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentList(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    long count = GetCollectionCount(components.get());
    Json list = Json::ArrayValue();
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> component = GetCollectionItem(components.get(), i);
        list.push_back(DescribeComponent(component, false));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)count));
    result.Set("components", list);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentGet(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    return ToolResult::Success(DescribeComponent(component, ArgBool(args, "includeCode", true)));
}

ToolResult ComIdeBackend::ComponentGetCode(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    ComPtr<IDispatch> codeModule = GetObjectProperty(component.get(), L"CodeModule");
    if (!codeModule) return ToolResult::Failure("Component has no CodeModule.", "vbide_error");
    long count = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", count);
    Json result = Json::ObjectValue();
    std::wstring name;
    GetBstrProperty(component.get(), L"Name", name);
    result.Set("component", Json::StringValue(name));
    result.Set("lineCount", MakeNumber((double)count));
    if (count > 0) {
        Variant start;
        Variant lineCount;
        SetVariantLong(start, 1);
        SetVariantLong(lineCount, count);
        std::vector<Variant*> codeArgs;
        codeArgs.push_back(&start);
        codeArgs.push_back(&lineCount);
        Variant text;
        HRESULT hr = GetProperty(codeModule.get(), L"Lines", codeArgs, text);
        if (FAILED(hr)) return ComFailure(L"Read CodeModule.Lines", hr);
        result.Set("code", Json::StringValue(text.AsWString()));
    } else {
        result.Set("code", MakeString(""));
    }
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentSetCode(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    ComPtr<IDispatch> codeModule = GetObjectProperty(component.get(), L"CodeModule");
    if (!codeModule) return ToolResult::Failure("Component has no CodeModule.", "vbide_error");
    long count = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", count);
    if (count > 0) {
        Variant start;
        Variant lineCount;
        SetVariantLong(start, 1);
        SetVariantLong(lineCount, count);
        std::vector<Variant*> deleteArgs;
        deleteArgs.push_back(&start);
        deleteArgs.push_back(&lineCount);
        HRESULT hr = InvokeMethodNoResult(codeModule.get(), L"DeleteLines", deleteArgs);
        if (FAILED(hr)) return ComFailure(L"Delete existing code", hr);
    }
    std::wstring code = args.Get("code").AsWString();
    if (!code.empty()) {
        Variant codeArg;
        MakeBstr(codeArg, code);
        std::vector<Variant*> addArgs;
        addArgs.push_back(&codeArg);
        HRESULT hr = InvokeMethodNoResult(codeModule.get(), L"AddFromString", addArgs);
        if (FAILED(hr)) return ComFailure(L"Add code", hr);
    }
    Json result = Json::ObjectValue();
    result.Set("component", Json::StringValue(args.Get("component").AsWString()));
    long finalCount = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", finalCount);
    result.Set("lineCount", MakeNumber((double)finalCount));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentEditCode(const Json& args) {
    ToolResult read = ComponentGetCode(args);
    if (!read.ok) return read;
    std::string source = read.data.Get("code").AsString();
    std::string find = ArgString(args, "find");
    if (find.empty()) return ToolResult::Failure("find must not be empty.", "invalid_arguments");
    std::string replacement = ArgString(args, "replace");
    std::string updated = ReplaceTextSimple(source, find, replacement, ArgBool(args, "all", true),
                                            ArgBool(args, "matchCase", false));
    Json setArgs = Json::ObjectValue();
    setArgs.Set("project", args.Get("project"));
    setArgs.Set("component", args.Get("component"));
    setArgs.Set("code", Json::StringValue(updated));
    ToolResult set = ComponentSetCode(setArgs);
    if (!set.ok) return set;
    Json result = Json::ObjectValue();
    result.Set("component", Json::StringValue(args.Get("component").AsWString()));
    result.Set("replaced", MakeBool(updated != source));
    result.Set("changedBytes", MakeNumber((double)(source.size() > updated.size() ? source.size() - updated.size() : updated.size() - source.size())));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentRename(const Json& args) {
    std::string newName = ArgString(args, "newName");
    if (!IsValidVbIdentifier(newName)) return ToolResult::Failure("newName is not a valid VB6 identifier.",
                                                                 "invalid_arguments");
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    Variant nameArg;
    MakeBstr(nameArg, Utf8ToWide(newName));
    HRESULT hr = PutProperty(component.get(), L"Name", nameArg);
    if (FAILED(hr)) return ComFailure(L"Rename component", hr);
    return ToolResult::Success(DescribeComponent(component, false));
}

ToolResult ComIdeBackend::ComponentExport(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::wstring path = GetFullPath(Utf8ToWide(ArgString(args, "path")));
    EnsureDirectory(PathDirectory(path));
    Variant pathArg;
    MakeBstr(pathArg, path);
    std::vector<Variant*> exportArgs;
    exportArgs.push_back(&pathArg);
    HRESULT hr = InvokeMethodNoResult(component.get(), L"Export", exportArgs);
    if (FAILED(hr)) return ComFailure(L"Export component", hr);
    Json result = DescribeComponent(component, false);
    result.Set("exportedPath", Json::StringValue(path));
    result.Set("exportedSize", MakeNumber((double)FileSize(path)));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentReplaceFile(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::wstring backup;
    std::wstring encoding;
    return ReplaceComponentText(project, component, args.Get("text").AsWString(), true, backup, encoding);
}

ToolResult ComIdeBackend::ComponentPropertiesList(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    std::wstring error;
    if (!FindProjectComponent(vbe_.get(), args, project, component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    ComPtr<IDispatch> properties = GetObjectProperty(component.get(), L"Properties");
    if (!properties) {
        return ToolResult::Failure("The component does not expose its Properties collection.",
                                   "vbide_error");
    }
    Json result = DescribeProperties(properties.get(), ArgBool(args, "includeValues", true));
    result.Set("component", DescribeComponent(component, false));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentReadProperty(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    std::wstring error;
    if (!FindProjectComponent(vbe_.get(), args, project, component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::string key = ArgString(args, "key");
    if (key.empty()) {
        return ToolResult::Failure("component_read_property requires key.", "invalid_arguments");
    }
    Variant keyArg;
    MakeBstr(keyArg, Utf8ToWide(key));
    std::vector<Variant*> callArgs;
    callArgs.push_back(&keyArg);
    Variant value;
    HRESULT hr = InvokeMethod(component.get(), L"ReadProperty", callArgs, value);
    if (FAILED(hr)) return ComFailure(L"Read component property", hr);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    result.Set("key", MakeString(key));
    result.Set("value", Json::StringValue(value.AsWString()));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentWriteProperty(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    std::wstring error;
    if (!FindProjectComponent(vbe_.get(), args, project, component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::string key = ArgString(args, "key");
    if (key.empty()) {
        return ToolResult::Failure("component_write_property requires key.", "invalid_arguments");
    }
    Variant keyArg, valueArg;
    MakeBstr(keyArg, Utf8ToWide(key));
    MakeBstr(valueArg, args.Get("value").AsWString());
    std::vector<Variant*> callArgs;
    callArgs.push_back(&keyArg);
    callArgs.push_back(&valueArg);
    HRESULT hr = InvokeMethodNoResult(component.get(), L"WriteProperty", callArgs);
    if (FAILED(hr)) return ComFailure(L"Write component property", hr);
    Json result = DescribeComponent(component, false);
    result.Set("key", MakeString(key));
    result.Set("value", args.Get("value"));
    result.Set("written", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ComponentAction(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    std::wstring error;
    if (!FindProjectComponent(vbe_.get(), args, project, component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::string action = ToLowerAscii(ArgString(args, "action"));
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    if (action == "activate") {
        HRESULT hr = InvokeMethodNoResult(component.get(), L"Activate", std::vector<Variant*>());
        if (FAILED(hr)) return ComFailure(L"Activate component", hr);
        result.Set("activated", MakeBool(true));
    } else if (action == "reload") {
        HRESULT hr = InvokeMethodNoResult(component.get(), L"Reload", std::vector<Variant*>());
        if (FAILED(hr)) return ComFailure(L"Reload component", hr);
        result.Set("reloaded", MakeBool(true));
    } else if (action == "saveas") {
        std::string rawPath = ArgString(args, "path");
        if (rawPath.empty()) {
            return ToolResult::Failure("component_action saveAs requires path.", "invalid_arguments");
        }
        std::wstring path = GetFullPath(Utf8ToWide(rawPath));
        EnsureDirectory(PathDirectory(path));
        Variant pathArg;
        MakeBstr(pathArg, path);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&pathArg);
        Variant saveResult;
        HRESULT hr = InvokeMethod(component.get(), L"SaveAs", callArgs, saveResult);
        if (FAILED(hr)) return ComFailure(L"Save component as", hr);
        result.Set("path", Json::StringValue(path));
        result.Set("saved", MakeBool(saveResult.AsBool()));
    } else if (action == "designerwindow" || action == "designer") {
        Variant windowResult;
        HRESULT hr = InvokeMethod(component.get(), L"DesignerWindow",
                                  std::vector<Variant*>(), windowResult);
        if (FAILED(hr)) return ComFailure(L"Get component designer window", hr);
        ComPtr<IDispatch> window(windowResult.GetDispatch(), true);
        if (!window) {
            return ToolResult::Failure("The component has no open designer window.",
                                       "designer_not_open");
        }
        result.Set("window", DescribeWindow(window, 0));
    } else {
        return ToolResult::Failure(
            "Unknown component action. Use activate, reload, saveAs, or designerWindow.",
            "invalid_arguments");
    }
    result.Set("component", DescribeComponent(component, false));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeGetLines(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    ComPtr<IDispatch> codeModule;
    std::wstring error;
    if (!FindComponentCodeModule(vbe_.get(), args, project, component, codeModule, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    long total = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", total);
    long startLine = ArgInt(args, "startLine", 1);
    if (startLine < 1) startLine = 1;
    if (total > 0 && startLine > total) {
        return ToolResult::Failure("startLine is past the end of the code module.",
                                   "invalid_arguments");
    }
    long count = ArgInt(args, "count", 0);
    if (count <= 0) count = total >= startLine ? total - startLine + 1 : 0;
    if (count < 0 || (total > 0 && startLine + count - 1 > total)) {
        return ToolResult::Failure("Requested line range exceeds the code module.",
                                   "invalid_arguments");
    }
    std::wstring text;
    if (count > 0) {
        Variant startArg, countArg;
        SetVariantLong(startArg, startLine);
        SetVariantLong(countArg, count);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&startArg);
        callArgs.push_back(&countArg);
        Variant value;
        HRESULT hr = GetProperty(codeModule.get(), L"Lines", callArgs, value);
        if (FAILED(hr)) return ComFailure(L"Read code lines", hr);
        text = value.AsWString();
    }
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    result.Set("startLine", MakeNumber((double)startLine));
    result.Set("count", MakeNumber((double)count));
    result.Set("endLine", MakeNumber((double)(count > 0 ? startLine + count - 1 : startLine)));
    result.Set("totalLines", MakeNumber((double)total));
    result.Set("lines", Json::StringValue(text));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeLinesEdit(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    ComPtr<IDispatch> codeModule;
    std::wstring error;
    if (!FindComponentCodeModule(vbe_.get(), args, project, component, codeModule, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::string action = ToLowerAscii(ArgString(args, "action"));
    long total = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", total);
    long line = ArgInt(args, "line", 0);
    long count = ArgInt(args, "count", 1);
    std::wstring text = args.Get("text").AsWString();
    HRESULT hr = E_FAIL;
    if (action == "insert") {
        if (line < 1 || line > total + 1) {
            return ToolResult::Failure("line is outside the insertable line range.",
                                       "invalid_arguments");
        }
        Variant lineArg, textArg;
        SetVariantLong(lineArg, line);
        MakeBstr(textArg, text);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&lineArg);
        callArgs.push_back(&textArg);
        hr = InvokeMethodNoResult(codeModule.get(), L"InsertLines", callArgs);
    } else if (action == "replace") {
        if (line < 1 || line > total) {
            return ToolResult::Failure("line is outside the code module.", "invalid_arguments");
        }
        Variant lineArg, textArg;
        SetVariantLong(lineArg, line);
        MakeBstr(textArg, text);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&lineArg);
        callArgs.push_back(&textArg);
        hr = InvokeMethodNoResult(codeModule.get(), L"ReplaceLine", callArgs);
    } else if (action == "delete") {
        if (line < 1 || line > total || count < 1 || line + count - 1 > total) {
            return ToolResult::Failure("Delete range is outside the code module.",
                                       "invalid_arguments");
        }
        Variant lineArg, countArg;
        SetVariantLong(lineArg, line);
        SetVariantLong(countArg, count);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&lineArg);
        callArgs.push_back(&countArg);
        hr = InvokeMethodNoResult(codeModule.get(), L"DeleteLines", callArgs);
    } else if (action == "append") {
        Variant textArg;
        MakeBstr(textArg, text);
        std::vector<Variant*> callArgs;
        callArgs.push_back(&textArg);
        hr = InvokeMethodNoResult(codeModule.get(), L"AddFromString", callArgs);
    } else {
        return ToolResult::Failure(
            "Unknown code edit action. Use insert, replace, delete, or append.",
            "invalid_arguments");
    }
    if (FAILED(hr)) return ComFailure(L"Edit code lines", hr);
    long finalTotal = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", finalTotal);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    result.Set("action", MakeString(action));
    result.Set("line", MakeNumber((double)line));
    result.Set("count", MakeNumber((double)count));
    result.Set("totalLines", MakeNumber((double)finalTotal));
    result.Set("edited", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeFind(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    ComPtr<IDispatch> codeModule;
    std::wstring error;
    if (!FindComponentCodeModule(vbe_.get(), args, project, component, codeModule, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::wstring target = Utf8ToWide(ArgString(args, "target"));
    if (target.empty()) return ToolResult::Failure("target must not be empty.", "invalid_arguments");
    long startLineValue = ArgInt(args, "startLine", 1);
    long startColumnValue = ArgInt(args, "startColumn", 1);
    long endLineValue = 0;
    long endColumnValue = 0;
    Variant targetArg, startLineArg, startColumnArg, endLineArg, endColumnArg;
    Variant wholeWordArg, matchCaseArg, patternSearchArg;
    MakeBstr(targetArg, target);
    SetVariantLongByRef(startLineArg, &startLineValue);
    SetVariantLongByRef(startColumnArg, &startColumnValue);
    SetVariantLongByRef(endLineArg, &endLineValue);
    SetVariantLongByRef(endColumnArg, &endColumnValue);
    wholeWordArg.get()->vt = VT_BOOL;
    wholeWordArg.get()->boolVal = ArgBool(args, "wholeWord", false) ? VARIANT_TRUE : VARIANT_FALSE;
    matchCaseArg.get()->vt = VT_BOOL;
    matchCaseArg.get()->boolVal = ArgBool(args, "matchCase", false) ? VARIANT_TRUE : VARIANT_FALSE;
    patternSearchArg.get()->vt = VT_BOOL;
    patternSearchArg.get()->boolVal = ArgBool(args, "patternSearch", false) ? VARIANT_TRUE : VARIANT_FALSE;
    std::vector<Variant*> callArgs;
    callArgs.push_back(&targetArg);
    callArgs.push_back(&startLineArg);
    callArgs.push_back(&startColumnArg);
    callArgs.push_back(&endLineArg);
    callArgs.push_back(&endColumnArg);
    callArgs.push_back(&wholeWordArg);
    callArgs.push_back(&matchCaseArg);
    callArgs.push_back(&patternSearchArg);
    Variant foundValue;
    HRESULT hr = InvokeMethodByRef(codeModule.get(), L"Find", callArgs, foundValue);
    if (FAILED(hr)) return ComFailure(L"Find code", hr);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    result.Set("target", Json::StringValue(target));
    result.Set("found", MakeBool(foundValue.AsBool()));
    result.Set("startLine", MakeNumber((double)startLineValue));
    result.Set("startColumn", MakeNumber((double)startColumnValue));
    result.Set("endLine", MakeNumber((double)endLineValue));
    result.Set("endColumn", MakeNumber((double)endColumnValue));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeSearch(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    std::string queryText = ArgString(args, "query");
    if (queryText.empty()) {
        return ToolResult::Failure("code_search requires query.", "invalid_arguments");
    }
    std::wstring query = Utf8ToWide(queryText);
    std::wstring lowerQuery = ToLower(query);
    bool matchCase = ArgBool(args, "matchCase", false);
    bool wholeWord = ArgBool(args, "wholeWord", false);
    int maxResults = ArgInt(args, "maxResults", 200);
    if (maxResults < 1) maxResults = 1;
    if (maxResults > 1000) maxResults = 1000;
    std::string componentFilter = ArgString(args, "component");

    Json matches = Json::ArrayValue();
    int searchedComponents = 0;
    bool truncated = false;
    ComPtr<IDispatch> components = GetObjectProperty(project.get(), L"VBComponents");
    if (!components) return ToolResult::Failure("VBComponents is unavailable.", "vbide_error");
    long componentCount = GetCollectionCount(components.get());
    for (long i = 1; i <= componentCount && !truncated; ++i) {
        ComPtr<IDispatch> component = GetCollectionItem(components.get(), i);
        if (!component) continue;
        std::wstring componentName;
        GetBstrProperty(component.get(), L"Name", componentName);
        if (!componentFilter.empty() &&
            !IEquals(WideToUtf8(componentName), componentFilter)) {
            continue;
        }
        ComPtr<IDispatch> codeModule = GetObjectProperty(component.get(), L"CodeModule");
        if (!codeModule) continue;
        ++searchedComponents;
        long lineCount = 0;
        GetLongProperty(codeModule.get(), L"CountOfLines", lineCount);
        if (lineCount <= 0) continue;
        Variant startArg, countArg;
        SetVariantLong(startArg, 1);
        SetVariantLong(countArg, lineCount);
        std::vector<Variant*> lineArgs;
        lineArgs.push_back(&startArg);
        lineArgs.push_back(&countArg);
        Variant textValue;
        HRESULT hr = GetProperty(codeModule.get(), L"Lines", lineArgs, textValue);
        if (FAILED(hr)) continue;
        std::wstring text = NormalizeNewlines(textValue.AsWString());
        std::vector<std::wstring> lines;
        size_t lineStart = 0;
        for (size_t p = 0; p <= text.size(); ++p) {
            if (p == text.size() || text[p] == L'\n') {
                size_t end = p;
                if (end > lineStart && text[end - 1] == L'\r') --end;
                lines.push_back(text.substr(lineStart, end - lineStart));
                lineStart = p + 1;
            }
        }
        long type = 0;
        GetLongProperty(component.get(), L"Type", type);
        for (size_t lineIndex = 0; lineIndex < lines.size() && !truncated; ++lineIndex) {
            const std::wstring& line = lines[lineIndex];
            std::wstring haystack = matchCase ? line : ToLower(line);
            size_t offset = 0;
            while (offset <= haystack.size() && !truncated) {
                size_t hit = haystack.find(matchCase ? query : lowerQuery, offset);
                if (hit == std::wstring::npos) break;
                bool boundaryOk = true;
                if (wholeWord) {
                    auto isWord = [](wchar_t ch) {
                        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
                               (ch >= L'0' && ch <= L'9') || ch == L'_';
                    };
                    if (hit > 0 && isWord(line[hit - 1])) boundaryOk = false;
                    size_t after = hit + query.size();
                    if (after < line.size() && isWord(line[after])) boundaryOk = false;
                }
                if (boundaryOk) {
                    Json match = Json::ObjectValue();
                    match.Set("component", Json::StringValue(componentName));
                    match.Set("componentType", Json::StringValue(ComponentTypeName(type)));
                    match.Set("line", MakeNumber((double)(lineIndex + 1)));
                    match.Set("column", MakeNumber((double)(hit + 1)));
                    match.Set("text", Json::StringValue(line));
                    matches.push_back(match);
                    if ((int)matches.size() >= maxResults) {
                        truncated = true;
                        break;
                    }
                }
                offset = hit + (query.empty() ? 1 : query.size());
            }
        }
    }

    Json result = Json::ObjectValue();
    result.Set("query", MakeString(queryText));
    result.Set("matchCase", MakeBool(matchCase));
    result.Set("wholeWord", MakeBool(wholeWord));
    result.Set("count", MakeNumber((double)matches.size()));
    result.Set("searchedComponents", MakeNumber((double)searchedComponents));
    result.Set("truncated", MakeBool(truncated));
    result.Set("matches", matches);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeCreateEvent(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    ComPtr<IDispatch> codeModule;
    std::wstring error;
    if (!FindComponentCodeModule(vbe_.get(), args, project, component, codeModule, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::string eventName = ArgString(args, "eventName");
    std::string objectName = ArgString(args, "objectName");
    if (eventName.empty() || objectName.empty()) {
        return ToolResult::Failure("code_create_event requires eventName and objectName.",
                                   "invalid_arguments");
    }
    Variant eventArg, objectArg;
    MakeBstr(eventArg, Utf8ToWide(eventName));
    MakeBstr(objectArg, Utf8ToWide(objectName));
    std::vector<Variant*> callArgs;
    callArgs.push_back(&eventArg);
    callArgs.push_back(&objectArg);
    Variant lineResult;
    HRESULT hr = InvokeMethod(codeModule.get(), L"CreateEventProc", callArgs, lineResult);
    if (FAILED(hr)) return ComFailure(L"Create event procedure", hr);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    result.Set("eventName", MakeString(eventName));
    result.Set("objectName", MakeString(objectName));
    result.Set("line", MakeNumber((double)lineResult.AsLong()));
    result.Set("created", MakeBool(lineResult.AsLong() > 0));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodeStructure(const Json& args) {
    ComPtr<IDispatch> project;
    ComPtr<IDispatch> component;
    ComPtr<IDispatch> codeModule;
    std::wstring error;
    if (!FindComponentCodeModule(vbe_.get(), args, project, component, codeModule, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(component, false));
    long lineCount = 0;
    GetLongProperty(codeModule.get(), L"CountOfLines", lineCount);
    long declarationLines = 0;
    GetLongProperty(codeModule.get(), L"CountOfDeclarationLines", declarationLines);
    result.Set("lineCount", MakeNumber((double)lineCount));
    result.Set("declarationLineCount", MakeNumber((double)declarationLines));
    Json list = Json::ArrayValue();
    ComPtr<IDispatch> members = GetObjectProperty(codeModule.get(), L"Members");
    if (members) {
        long count = GetCollectionCount(members.get());
        for (long i = 1; i <= count; ++i) {
            ComPtr<IDispatch> member = GetCollectionItem(members.get(), i);
            if (!member) continue;
            Json item = Json::ObjectValue();
            std::wstring name, description;
            GetBstrProperty(member.get(), L"Name", name);
            GetBstrProperty(member.get(), L"Description", description);
            item.Set("name", Json::StringValue(name));
            item.Set("description", Json::StringValue(description));
            long number = 0;
            if (GetLongProperty(member.get(), L"Type", number)) {
                item.Set("type", MakeNumber((double)number));
                item.Set("typeName", MakeString(MemberTypeName(number)));
            }
            if (GetLongProperty(member.get(), L"Scope", number)) {
                item.Set("scope", MakeNumber((double)number));
                item.Set("scopeName", MakeString(ScopeName(number)));
            }
            if (GetLongProperty(member.get(), L"CodeLocation", number)) {
                item.Set("codeLocation", MakeNumber((double)number));
            }
            bool staticMember = false;
            if (GetBoolProperty(member.get(), L"Static", staticMember)) {
                item.Set("static", MakeBool(staticMember));
            }
            list.push_back(item);
        }
        result.Set("membersAvailable", MakeBool(true));
    } else {
        result.Set("membersAvailable", MakeBool(false));
    }
    result.Set("count", MakeNumber((double)list.size()));
    result.Set("members", list);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodePanesList(const Json& args) {
    (void)args;
    ComPtr<IDispatch> panes = GetObjectProperty(vbe_.get(), L"CodePanes");
    if (!panes) return ToolResult::Failure("CodePanes is unavailable.", "vbide_error");
    long count = GetCollectionCount(panes.get());
    Json list = Json::ArrayValue();
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> pane = GetCollectionItem(panes.get(), i);
        list.push_back(DescribeCodePane(pane, i));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)count));
    result.Set("codePanes", list);
    ComPtr<IDispatch> active = GetObjectProperty(vbe_.get(), L"ActiveCodePane");
    if (active) result.Set("activeCodePane", DescribeCodePane(active, 0));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::CodePaneSet(const Json& args) {
    ComPtr<IDispatch> pane;
    std::wstring error;
    if (!FindCodePane(vbe_.get(), ArgString(args, "pane"), pane, error)) {
        return ToolResult::Failure(WideToUtf8(error), "code_pane_not_found");
    }
    std::string action = ToLowerAscii(ArgString(args, "action"));
    HRESULT hr = E_FAIL;
    if (action == "focus" || action == "show") {
        hr = InvokeMethodNoResult(pane.get(), L"Show", std::vector<Variant*>());
        if (SUCCEEDED(hr)) {
            Variant paneArg;
            MakeDispatch(paneArg, pane.get());
            hr = PutProperty(vbe_.get(), L"ActiveCodePane", paneArg);
        }
    } else if (action == "selection") {
        Variant startLineArg, startColumnArg, endLineArg, endColumnArg;
        SetVariantLong(startLineArg, ArgInt(args, "startLine", 1));
        SetVariantLong(startColumnArg, ArgInt(args, "startColumn", 1));
        SetVariantLong(endLineArg, ArgInt(args, "endLine", ArgInt(args, "startLine", 1)));
        SetVariantLong(endColumnArg, ArgInt(args, "endColumn", ArgInt(args, "startColumn", 1)));
        std::vector<Variant*> callArgs;
        callArgs.push_back(&startLineArg);
        callArgs.push_back(&startColumnArg);
        callArgs.push_back(&endLineArg);
        callArgs.push_back(&endColumnArg);
        hr = InvokeMethodNoResult(pane.get(), L"SetSelection", callArgs);
    } else if (action == "topline") {
        Variant topLineArg;
        SetVariantLong(topLineArg, ArgInt(args, "topLine", 1));
        hr = PutProperty(pane.get(), L"TopLine", topLineArg);
    } else {
        return ToolResult::Failure(
            "Unknown code pane action. Use focus, show, selection, or topLine.",
            "invalid_arguments");
    }
    if (FAILED(hr)) return ComFailure(L"Set code pane", hr);
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    result.Set("codePane", DescribeCodePane(pane, 0));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::WindowsList(const Json& args) {
    (void)args;
    ComPtr<IDispatch> windows = GetObjectProperty(vbe_.get(), L"Windows");
    if (!windows) return ToolResult::Failure("Windows is unavailable.", "vbide_error");
    long count = GetCollectionCount(windows.get());
    Json list = Json::ArrayValue();
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> window = GetCollectionItem(windows.get(), i);
        list.push_back(DescribeWindow(window, i));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)count));
    result.Set("windows", list);
    ComPtr<IDispatch> active = GetObjectProperty(vbe_.get(), L"ActiveWindow");
    if (active) result.Set("activeWindow", DescribeWindow(active, 0));
    ComPtr<IDispatch> main = GetObjectProperty(vbe_.get(), L"MainWindow");
    if (main) result.Set("mainWindow", DescribeWindow(main, 0));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::WindowAction(const Json& args) {
    ComPtr<IDispatch> window;
    std::wstring error;
    if (!FindWindow(vbe_.get(), ArgString(args, "window"), window, error)) {
        return ToolResult::Failure(WideToUtf8(error), "window_not_found");
    }
    std::string action = ToLowerAscii(ArgString(args, "action"));
    Json before = DescribeWindow(window, 0);
    HRESULT hr = E_FAIL;
    if (action == "focus") {
        hr = InvokeMethodNoResult(window.get(), L"SetFocus", std::vector<Variant*>());
    } else if (action == "show" || action == "hide") {
        Variant visibleArg;
        visibleArg.get()->vt = VT_BOOL;
        visibleArg.get()->boolVal = action == "show" ? VARIANT_TRUE : VARIANT_FALSE;
        hr = PutProperty(window.get(), L"Visible", visibleArg);
    } else if (action == "close") {
        hr = InvokeMethodNoResult(window.get(), L"Close", std::vector<Variant*>());
    } else if (action == "move") {
        const char* names[] = {"left", "top", "width", "height"};
        const wchar_t* vbNames[] = {L"Left", L"Top", L"Width", L"Height"};
        bool changed = false;
        for (size_t i = 0; i < _countof(names); ++i) {
            if (!args.Has(names[i])) continue;
            changed = true;
            Variant value;
            SetVariantLong(value, ArgInt(args, names[i]));
            hr = PutProperty(window.get(), vbNames[i], value);
            if (FAILED(hr)) return ComFailure(L"Move window", hr);
        }
        if (!changed) {
            return ToolResult::Failure(
                "window_action move requires at least one of left, top, width, or height.",
                "invalid_arguments");
        }
        hr = S_OK;
    } else if (action == "state") {
        long state = 0;
        if (args.Get("state").IsString()) {
            std::string name = ToLowerAscii(ArgString(args, "state"));
            if (name == "normal") state = 0;
            else if (name == "minimize" || name == "minimized") state = 1;
            else if (name == "maximize" || name == "maximized") state = 2;
            else return ToolResult::Failure(
                "state must be normal, minimize, or maximize.", "invalid_arguments");
        } else {
            state = ArgInt(args, "state", 0);
        }
        Variant stateArg;
        SetVariantLong(stateArg, state);
        hr = PutProperty(window.get(), L"WindowState", stateArg);
    } else if (action == "kind") {
        Variant kindArg;
        SetVariantLong(kindArg, ArgInt(args, "kind", 0));
        std::vector<Variant*> callArgs;
        callArgs.push_back(&kindArg);
        hr = InvokeMethodNoResult(window.get(), L"SetKind", callArgs);
    } else if (action == "detach") {
        hr = InvokeMethodNoResult(window.get(), L"Detach", std::vector<Variant*>());
    } else {
        return ToolResult::Failure(
            "Unknown window action. Use focus, show, hide, close, move, state, kind, or detach.",
            "invalid_arguments");
    }
    if (FAILED(hr)) return ComFailure(L"Window action", hr);
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    result.Set("before", before);
    if (action != "close") result.Set("window", DescribeWindow(window, 0));
    else result.Set("closed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::FormGetText(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    long type = 0;
    GetLongProperty(component.get(), L"Type", type);
    std::wstring temp;
    if (!CreateTempDirectory(L"form-read-", temp)) {
        return ToolResult::Failure("Cannot create a temporary form export directory.", "filesystem_error");
    }
    std::wstring name;
    GetBstrProperty(component.get(), L"Name", name);
    std::wstring path;
    HRESULT exportFailure = E_FAIL;
    if (!TryExportComponent(component.get(), temp, name, type, path, exportFailure)) {
        std::wstring vbpPath;
        GetBstrProperty(project.get(), L"FileName", vbpPath);
        std::wstring mappingError;
        if (vbpPath.empty() ||
            !FindVbpComponentFile(vbpPath, WideToUtf8(name),
                                  ComponentExtensionForType(type), path, mappingError)) {
            RemoveDirectoryRecursiveQuiet(temp);
            return ComFailure(L"Export form", exportFailure);
        }
    }
    std::wstring text;
    std::wstring encoding;
    DWORD readError = 0;
    if (!ReadTextFile(path, text, &encoding, &readError)) {
        RemoveDirectoryRecursiveQuiet(temp);
        return ToolResult::Failure("Cannot read exported form.", "filesystem_error");
    }
    Json result = DescribeComponent(component, false);
    result.Set("text", Json::StringValue(text));
    result.Set("encoding", Json::StringValue(encoding));
    RemoveDirectoryRecursiveQuiet(temp);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::FormSetText(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> component;
    if (!FindComponent(project, ArgString(args, "component"), component, error)) {
        return ToolResult::Failure(WideToUtf8(error), "component_not_found");
    }
    std::wstring backup;
    std::wstring encoding;
    return ReplaceComponentText(project, component, args.Get("text").AsWString(), true, backup, encoding);
}

ToolResult ComIdeBackend::FormListControls(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring projectError;
    if (FindProject(vbe_.get(), ArgString(args, "project"), project, projectError)) {
        ComPtr<IDispatch> component;
        if (FindComponent(project, ArgString(args, "component"), component, projectError)) {
            ComPtr<IDispatch> designer = OpenComponentDesigner(component.get());
            ComPtr<IDispatch> controls = designer ?
                GetObjectProperty(designer.get(), L"VBControls") : ComPtr<IDispatch>();
            if (controls) {
                Json list = Json::ArrayValue();
                long count = GetCollectionCount(controls.get());
                for (long i = 1; i <= count; ++i) {
                    ComPtr<IDispatch> control = GetCollectionItem(controls.get(), i);
                    if (!control) continue;
                    Json item = Json::ObjectValue();
                    std::wstring className;
                    if (GetBstrProperty(control.get(), L"ClassName", className)) {
                        item.Set("type", Json::StringValue(className));
                    } else {
                        item.Set("type", MakeString("VBControl"));
                    }
                    Json props = Json::ObjectValue();
                    Variant nameValue;
                    if (GetDesignerProperty(control.get(), "Name", nameValue)) {
                        item.Set("name", Json::StringValue(nameValue.AsWString()));
                        props.Set("Name", Json::StringValue(nameValue.AsWString()));
                    }
                    Variant indexValue;
                    if (GetDesignerProperty(control.get(), "Index", indexValue)) {
                        item.Set("index", MakeNumber((double)indexValue.AsLong()));
                        props.Set("Index", MakeNumber((double)indexValue.AsLong()));
                    }
                    Variant captionValue;
                    if (GetDesignerProperty(control.get(), "Caption", captionValue)) {
                        props.Set("Caption", Json::StringValue(captionValue.AsWString()));
                    }
                    Variant textValue;
                    if (GetDesignerProperty(control.get(), "Text", textValue)) {
                        props.Set("Text", Json::StringValue(textValue.AsWString()));
                    }
                    item.Set("properties", props);
                    list.push_back(item);
                }
                Json result = Json::ObjectValue();
                result.Set("component", Json::StringValue(args.Get("component").AsWString()));
                result.Set("controls", list);
                result.Set("liveDesigner", MakeBool(true));
                return ToolResult::Success(result);
            }
        }
    }

    ToolResult textResult = FormGetText(args);
    if (!textResult.ok) return textResult;
    std::vector<FormControlInfo> controls;
    std::wstring error;
    if (!ListFormControls(textResult.data.Get("text").AsWString(), controls, error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_parse_error");
    }
    Json list = Json::ArrayValue();
    for (size_t i = 0; i < controls.size(); ++i) {
        Json item = Json::ObjectValue();
        item.Set("type", MakeString(controls[i].type));
        item.Set("name", MakeString(controls[i].name));
        item.Set("line", MakeNumber((double)controls[i].line));
        if (controls[i].hasIndex) item.Set("index", MakeNumber((double)controls[i].index));
        Json props = Json::ObjectValue();
        for (std::map<std::string, std::string>::const_iterator it = controls[i].rawProperties.begin();
             it != controls[i].rawProperties.end(); ++it) {
            props.Set(it->first, MakeString(it->second));
        }
        item.Set("properties", props);
        list.push_back(item);
    }
    Json result = Json::ObjectValue();
    result.Set("component", Json::StringValue(args.Get("component").AsWString()));
    result.Set("controls", list);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::FormAddControl(const Json& args) {
    std::string container = ArgString(args, "container");
    std::wstring liveError;
    if (container.empty()) {
        ComPtr<IDispatch> project;
        std::wstring projectError;
        if (FindProject(vbe_.get(), ArgString(args, "project"), project, projectError)) {
            ComPtr<IDispatch> component;
            if (FindComponent(project, ArgString(args, "component"), component, projectError)) {
                ComPtr<IDispatch> designer = OpenComponentDesigner(component.get());
                if (!designer) {
                    liveError = L"VBComponent.Designer is unavailable.";
                }
                Variant controlsResult;
                HRESULT controlsHr = designer ?
                    GetProperty(designer.get(), L"VBControls", controlsResult) : E_POINTER;
                ComPtr<IDispatch> controls(controlsResult.GetDispatch(), true);
                if (designer && FAILED(controlsHr)) {
                    liveError = L"VBForm.VBControls is unavailable: " + HResultMessage(controlsHr);
                }
                std::string controlType;
                std::string controlName;
                Variant added;
                bool addedOk = false;
                if (controls) {
                    controlType = NormalizeControlProgId(ArgString(args, "type"));
                    controlName = ArgString(args, "name");
                    addedOk = TryAddVbControl(controls.get(), controlType, "VBForm.VBControls",
                                              added, liveError);
                }
                if (!addedOk && designer) {
                    Variant containedResult;
                    HRESULT containedHr = GetProperty(designer.get(), L"ContainedVBControls",
                                                      containedResult);
                    ComPtr<IDispatch> contained(containedResult.GetDispatch(), true);
                    if (contained) {
                        std::wstring containedError;
                        addedOk = TryAddVbControl(contained.get(), controlType,
                                                  "VBForm.ContainedVBControls",
                                                  added, containedError);
                        if (!containedError.empty()) {
                            if (!liveError.empty()) liveError += L" ";
                            liveError += containedError;
                        }
                    } else if (FAILED(containedHr)) {
                        if (!liveError.empty()) liveError += L" ";
                        liveError += L"VBForm.ContainedVBControls is unavailable: " +
                                     HResultMessage(containedHr);
                    }
                }
                if (addedOk) {
                        ComPtr<IDispatch> control(added.GetDispatch(), true);
                        std::wstring propertyError;
                        if (!SetDesignerProperty(control.get(), "Name",
                                                 Json::StringValue(controlName), propertyError)) {
                            return ToolResult::Failure(WideToUtf8(propertyError),
                                                       "designer_property_failed");
                        }
                        const Json& properties = args.Get("properties");
                        if (properties.IsObject()) {
                            for (std::map<std::string, Json>::const_iterator it =
                                     properties.object().begin();
                                 it != properties.object().end(); ++it) {
                                if (IEquals(it->first, "Name")) continue;
                                if (!SetDesignerProperty(control.get(), it->first, it->second,
                                                         propertyError)) {
                                    return ToolResult::Failure(WideToUtf8(propertyError),
                                                               "designer_property_failed");
                                }
                            }
                        }
                        const Json& index = args.Get("index");
                        if (!index.IsNull() &&
                            !SetDesignerProperty(control.get(), "Index", index, propertyError)) {
                            return ToolResult::Failure(WideToUtf8(propertyError),
                                                       "designer_property_failed");
                        }
                        Json result = Json::ObjectValue();
                        result.Set("component", Json::StringValue(args.Get("component").AsWString()));
                        result.Set("name", MakeString(controlName));
                        result.Set("type", MakeString(controlType));
                        result.Set("liveDesigner", MakeBool(true));
                        result.Set("requiresReload", MakeBool(false));
                        return ToolResult::Success(result);
                }
            }
            else {
                liveError = projectError;
            }
        }
        else {
            liveError = projectError;
        }
    }

    ToolResult textResult = FormGetText(args);
    if (!textResult.ok) {
        if (!liveError.empty()) {
            std::string combined = "Live designer path failed: " + WideToUtf8(liveError) +
                                   ". File fallback also failed: " +
                                   textResult.data.Get("error").AsString();
            return ToolResult::Failure(combined, "form_edit_failed");
        }
        return textResult;
    }
    std::wstring text = textResult.data.Get("text").AsWString();
    std::vector<std::pair<std::string, Json> > properties;
    const Json& props = args.Get("properties");
    if (props.IsObject()) {
        for (std::map<std::string, Json>::const_iterator it = props.object().begin();
             it != props.object().end(); ++it) {
            properties.push_back(std::make_pair(it->first, it->second));
        }
    }
    std::wstring error;
    if (!AddFormControl(text, ArgString(args, "type"), ArgString(args, "name"),
                        ArgString(args, "container"), properties, args.Get("index"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    Json setArgs = Json::ObjectValue();
    setArgs.Set("project", args.Get("project"));
    setArgs.Set("component", args.Get("component"));
    setArgs.Set("text", Json::StringValue(text));
    return FormSetText(setArgs);
}

ToolResult ComIdeBackend::FormRemoveControl(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring projectError;
    if (FindProject(vbe_.get(), ArgString(args, "project"), project, projectError)) {
        ComPtr<IDispatch> component;
        if (FindComponent(project, ArgString(args, "component"), component, projectError)) {
            ComPtr<IDispatch> designer = OpenComponentDesigner(component.get());
            ComPtr<IDispatch> controls = designer ?
                GetObjectProperty(designer.get(), L"VBControls") : ComPtr<IDispatch>();
            if (controls) {
                std::wstring wantedName = Utf8ToWide(ArgString(args, "name"));
                const Json& wantedIndex = args.Get("index");
                long count = GetCollectionCount(controls.get());
                for (long i = 1; i <= count; ++i) {
                    ComPtr<IDispatch> control = GetCollectionItem(controls.get(), i);
                    if (!control) continue;
                    Variant nameValue;
                    if (!GetDesignerProperty(control.get(), "Name", nameValue) ||
                        _wcsicmp(nameValue.AsWString().c_str(), wantedName.c_str()) != 0) {
                        continue;
                    }
                    if (!wantedIndex.IsNull()) {
                        Variant indexValue;
                        if (!GetDesignerProperty(control.get(), "Index", indexValue) ||
                            indexValue.AsLong() != wantedIndex.AsInt()) {
                            continue;
                        }
                    }
                    Variant controlArg;
                    MakeDispatch(controlArg, control.get());
                    std::vector<Variant*> removeArgs;
                    removeArgs.push_back(&controlArg);
                    HRESULT removeHr = InvokeMethodNoResult(controls.get(), L"Remove", removeArgs);
                    if (FAILED(removeHr)) {
                        return ComFailure(L"Remove designer control", removeHr);
                    }
                    Json result = Json::ObjectValue();
                    result.Set("removed", MakeBool(true));
                    result.Set("liveDesigner", MakeBool(true));
                    result.Set("requiresReload", MakeBool(false));
                    return ToolResult::Success(result);
                }
            }
        }
    }

    ToolResult textResult = FormGetText(args);
    if (!textResult.ok) return textResult;
    std::wstring text = textResult.data.Get("text").AsWString();
    std::wstring error;
    if (!RemoveFormControl(text, ArgString(args, "name"), args.Get("index"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    Json setArgs = Json::ObjectValue();
    setArgs.Set("project", args.Get("project"));
    setArgs.Set("component", args.Get("component"));
    setArgs.Set("text", Json::StringValue(text));
    return FormSetText(setArgs);
}

ToolResult ComIdeBackend::FormSetControlProperty(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring projectError;
    if (FindProject(vbe_.get(), ArgString(args, "project"), project, projectError)) {
        ComPtr<IDispatch> component;
        if (FindComponent(project, ArgString(args, "component"), component, projectError)) {
            ComPtr<IDispatch> designer = OpenComponentDesigner(component.get());
            ComPtr<IDispatch> controls = designer ?
                GetObjectProperty(designer.get(), L"VBControls") : ComPtr<IDispatch>();
            if (controls) {
                std::wstring wantedName = Utf8ToWide(ArgString(args, "name"));
                const Json& wantedIndex = args.Get("index");
                long count = GetCollectionCount(controls.get());
                for (long i = 1; i <= count; ++i) {
                    ComPtr<IDispatch> control = GetCollectionItem(controls.get(), i);
                    if (!control) continue;
                    Variant nameValue;
                    if (!GetDesignerProperty(control.get(), "Name", nameValue) ||
                        _wcsicmp(nameValue.AsWString().c_str(), wantedName.c_str()) != 0) {
                        continue;
                    }
                    if (!wantedIndex.IsNull()) {
                        Variant indexValue;
                        if (!GetDesignerProperty(control.get(), "Index", indexValue) ||
                            indexValue.AsLong() != wantedIndex.AsInt()) {
                            continue;
                        }
                    }
                    std::wstring propertyError;
                    if (!SetDesignerProperty(control.get(), ArgString(args, "property"),
                                             args.Get("value"), propertyError)) {
                        return ToolResult::Failure(WideToUtf8(propertyError),
                                                   "designer_property_failed");
                    }
                    Json result = Json::ObjectValue();
                    result.Set("component", Json::StringValue(args.Get("component").AsWString()));
                    result.Set("name", Json::StringValue(wantedName));
                    result.Set("liveDesigner", MakeBool(true));
                    result.Set("requiresReload", MakeBool(false));
                    return ToolResult::Success(result);
                }
            }
        }
    }

    ToolResult textResult = FormGetText(args);
    if (!textResult.ok) return textResult;
    std::wstring text = textResult.data.Get("text").AsWString();
    std::wstring error;
    if (!SetFormControlProperty(text, ArgString(args, "name"), args.Get("index"),
                                ArgString(args, "property"), args.Get("value"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    Json setArgs = Json::ObjectValue();
    setArgs.Set("project", args.Get("project"));
    setArgs.Set("component", args.Get("component"));
    setArgs.Set("text", Json::StringValue(text));
    return FormSetText(setArgs);
}

ToolResult ComIdeBackend::ReferencesList(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> references = GetObjectProperty(project.get(), L"References");
    if (!references) return ToolResult::Failure("References is unavailable.", "vbide_error");
    long count = GetCollectionCount(references.get());
    Json list = Json::ArrayValue();
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> reference = GetCollectionItem(references.get(), i);
        if (!reference) continue;
        Json item = Json::ObjectValue();
        std::wstring name, guid, fullPath, description;
        GetBstrProperty(reference.get(), L"Name", name);
        GetBstrProperty(reference.get(), L"Guid", guid);
        GetBstrProperty(reference.get(), L"FullPath", fullPath);
        GetBstrProperty(reference.get(), L"Description", description);
        long major = 0, minor = 0;
        GetLongProperty(reference.get(), L"Major", major);
        GetLongProperty(reference.get(), L"Minor", minor);
        bool builtIn = false, broken = false;
        GetBoolProperty(reference.get(), L"BuiltIn", builtIn);
        GetBoolProperty(reference.get(), L"IsBroken", broken);
        item.Set("name", Json::StringValue(name));
        item.Set("guid", Json::StringValue(guid));
        item.Set("fullPath", Json::StringValue(fullPath));
        item.Set("description", Json::StringValue(description));
        item.Set("major", MakeNumber((double)major));
        item.Set("minor", MakeNumber((double)minor));
        item.Set("builtIn", MakeBool(builtIn));
        item.Set("isBroken", MakeBool(broken));
        list.push_back(item);
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)count));
    result.Set("references", list);
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ReferenceAdd(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> references = GetObjectProperty(project.get(), L"References");
    std::string path = ArgString(args, "path");
    std::string guid = ArgString(args, "guid");
    Variant added;
    HRESULT hr;
    if (!path.empty()) {
        Variant pathArg;
        MakeBstr(pathArg, GetFullPath(Utf8ToWide(path)));
        std::vector<Variant*> addArgs;
        addArgs.push_back(&pathArg);
        hr = InvokeMethod(references.get(), L"AddFromFile", addArgs, added);
    } else if (!guid.empty()) {
        Variant guidArg, majorArg, minorArg;
        MakeBstr(guidArg, Utf8ToWide(guid));
        SetVariantLong(majorArg, ArgInt(args, "major", 1));
        SetVariantLong(minorArg, ArgInt(args, "minor", 0));
        std::vector<Variant*> addArgs;
        addArgs.push_back(&guidArg);
        addArgs.push_back(&majorArg);
        addArgs.push_back(&minorArg);
        hr = InvokeMethod(references.get(), L"AddFromGuid", addArgs, added);
    } else {
        return ToolResult::Failure("reference_add requires path or guid.", "invalid_arguments");
    }
    if (FAILED(hr)) return ComFailure(L"Add reference", hr);
    Json result = Json::ObjectValue();
    result.Set("added", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ReferenceRemove(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    ComPtr<IDispatch> references = GetObjectProperty(project.get(), L"References");
    long count = GetCollectionCount(references.get());
    std::wstring wanted = Utf8ToWide(ArgString(args, "reference"));
    ComPtr<IDispatch> found;
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> reference = GetCollectionItem(references.get(), i);
        std::wstring name, path;
        GetBstrProperty(reference.get(), L"Name", name);
        GetBstrProperty(reference.get(), L"FullPath", path);
        if (_wcsicmp(name.c_str(), wanted.c_str()) == 0 ||
            NormalizePathForCompare(path) == NormalizePathForCompare(wanted)) {
            found = reference;
            break;
        }
    }
    if (!found) return ToolResult::Failure("Reference not found.", "reference_not_found");
    Variant refArg;
    MakeDispatch(refArg, found.get());
    std::vector<Variant*> removeArgs;
    removeArgs.push_back(&refArg);
    HRESULT hr = InvokeMethodNoResult(references.get(), L"Remove", removeArgs);
    if (FAILED(hr)) return ComFailure(L"Remove reference", hr);
    Json result = Json::ObjectValue();
    result.Set("removed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::ActiveProjectSet(const Json& args) {
    ComPtr<IDispatch> project;
    std::wstring error;
    if (!FindProject(vbe_.get(), ArgString(args, "project"), project, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_not_found");
    }
    Variant projectArg;
    MakeDispatch(projectArg, project.get());
    HRESULT hr = PutProperty(vbe_.get(), L"ActiveVBProject", projectArg);
    if (FAILED(hr)) return ComFailure(L"Set ActiveVBProject", hr);
    Json result = Json::ObjectValue();
    result.Set("active", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::IdeExecuteCommand(const Json& args) {
    ComPtr<IDispatch> commandBars = GetObjectProperty(vbe_.get(), L"CommandBars");
    if (!commandBars) return ToolResult::Failure("VBE.CommandBars is unavailable.", "vbide_error");
    std::string command = ArgString(args, "command");
    int id = ArgInt(args, "id", 0);
    if (command.empty() && id <= 0) return ToolResult::Failure("Specify command or id.", "invalid_arguments");
    int visited = 0;
    long count = GetCollectionCount(commandBars.get());
    for (long i = 1; i <= count; ++i) {
        ComPtr<IDispatch> bar = GetCollectionItem(commandBars.get(), i);
        ComPtr<IDispatch> controls = GetObjectProperty(bar.get(), L"Controls");
        if (controls && ExecuteCommandControl(controls.get(), command, id, 1, visited)) {
            Json result = Json::ObjectValue();
            result.Set("executed", MakeBool(true));
            result.Set("command", MakeString(command));
            result.Set("id", MakeNumber((double)id));
            return ToolResult::Success(result);
        }
    }
    return ToolResult::Failure("Command was not found or could not be executed.", "command_not_found");
}

ToolResult ComIdeBackend::SourceRead(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(ArgString(args, "path")));
    std::vector<std::wstring> roots = ProjectRoots(vbe_.get());
    if (!IsPathAllowed(config_, path, roots)) {
        return ToolResult::Failure("Path is outside configured allowedRoots and open project directories.",
                                   "path_not_allowed");
    }
    std::wstring text;
    std::wstring encoding;
    std::wstring requestedEncoding = Utf8ToWide(ArgString(args, "encoding"));
    DWORD readError = 0;
    if (requestedEncoding.empty() || IEquals(requestedEncoding, L"auto")) {
        if (!ReadTextFile(path, text, &encoding, &readError)) {
            return ToolResult::Failure("Cannot read file: " + WideToUtf8(GetLastErrorMessage(readError)),
                                       "filesystem_error");
        }
    } else {
        if (!ReadTextFileAs(path, requestedEncoding, text, &readError)) {
            return ToolResult::Failure("Cannot read file with requested encoding: " +
                                       WideToUtf8(GetLastErrorMessage(readError)),
                                       "filesystem_error");
        }
        encoding = ToLower(requestedEncoding);
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(path));
    result.Set("encoding", Json::StringValue(encoding));
    result.Set("size", MakeNumber((double)FileSize(path)));
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        result.Set("modifiedUnix", MakeNumber((double)UnixTimeFromFileTime(data.ftLastWriteTime)));
    }
    result.Set("content", Json::StringValue(text));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::SourceWrite(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(ArgString(args, "path")));
    std::vector<std::wstring> roots = ProjectRoots(vbe_.get());
    if (!IsPathAllowed(config_, path, roots)) {
        return ToolResult::Failure("Path is outside configured allowedRoots and open project directories.",
                                   "path_not_allowed");
    }
    if (ArgBool(args, "createDirectories", true) && !EnsureDirectory(PathDirectory(path))) {
        return ToolResult::Failure("Cannot create parent directory.", "filesystem_error");
    }
    std::wstring encoding = Utf8ToWide(ArgString(args, "encoding"));
    if (encoding.empty() || IEquals(encoding, L"auto")) encoding = config_.sourceEncoding;
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(path, args.Get("content").AsWString(), encoding, &writeError)) {
        return ToolResult::Failure("Cannot write file: " + WideToUtf8(GetLastErrorMessage(writeError)),
                                   "filesystem_error");
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(path));
    result.Set("size", MakeNumber((double)FileSize(path)));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::SourceEdit(const Json& args) {
    Json readArgs = Json::ObjectValue();
    readArgs.Set("path", args.Get("path"));
    ToolResult read = SourceRead(readArgs);
    if (!read.ok) return read;
    std::string source = read.data.Get("content").AsString();
    std::string find = ArgString(args, "find");
    if (find.empty()) return ToolResult::Failure("find must not be empty.", "invalid_arguments");
    std::string updated = ReplaceTextSimple(source, find, ArgString(args, "replace"),
                                            ArgBool(args, "all", true), ArgBool(args, "matchCase", false));
    Json writeArgs = Json::ObjectValue();
    writeArgs.Set("path", args.Get("path"));
    writeArgs.Set("content", Json::StringValue(updated));
    writeArgs.Set("encoding", read.data.Get("encoding"));
    ToolResult write = SourceWrite(writeArgs);
    if (!write.ok) return write;
    Json result = Json::ObjectValue();
    result.Set("path", args.Get("path"));
    result.Set("replaced", MakeBool(updated != source));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::SourceList(const Json& args) {
    std::wstring directory = GetFullPath(Utf8ToWide(ArgString(args, "directory")));
    std::vector<std::wstring> roots = ProjectRoots(vbe_.get());
    if (!IsPathAllowed(config_, directory, roots)) {
        return ToolResult::Failure("Directory is outside configured allowedRoots and open project directories.",
                                   "path_not_allowed");
    }
    std::wstring pattern = Utf8ToWide(ArgString(args, "pattern", "*"));
    bool recursive = ArgBool(args, "recursive", false);
    std::vector<std::wstring> queue;
    queue.push_back(directory);
    Json files = Json::ArrayValue();
    int count = 0;
    for (size_t q = 0; q < queue.size() && count < 5000; ++q) {
        std::wstring search = PathJoin(queue[q], L"*");
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(search.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            std::wstring path = PathJoin(queue[q], data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                    recursive && IsPathAllowed(config_, path, roots)) {
                    queue.push_back(path);
                }
                continue;
            }
            if (!WildcardMatch(pattern, data.cFileName)) continue;
            Json file = Json::ObjectValue();
            file.Set("path", Json::StringValue(path));
            file.Set("name", Json::StringValue(data.cFileName));
            file.Set("size", MakeNumber((double)(((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow)));
            file.Set("modifiedUnix", MakeNumber((double)UnixTimeFromFileTime(data.ftLastWriteTime)));
            files.push_back(file);
            ++count;
        } while (FindNextFileW(find, &data) && count < 5000);
        FindClose(find);
    }
    Json result = Json::ObjectValue();
    result.Set("directory", Json::StringValue(directory));
    result.Set("count", MakeNumber((double)count));
    result.Set("files", files);
    result.Set("truncated", MakeBool(count >= 5000));
    return ToolResult::Success(result);
}

ToolResult ComIdeBackend::Vb6Batch(const Json& args) {
    const Json& calls = args.Get("calls");
    if (!calls.IsArray() || calls.array().empty()) {
        return ToolResult::Failure("vb6_batch requires a non-empty calls array.", "invalid_arguments");
    }
    int maxCalls = ArgInt(args, "maxCalls", 50);
    if (maxCalls < 1) maxCalls = 1;
    if (maxCalls > 200) maxCalls = 200;
    bool stopOnError = ArgBool(args, "stopOnError", true);
    Json results = Json::ArrayValue();
    int successCount = 0;
    int errorCount = 0;
    bool stoppedEarly = false;
    for (size_t i = 0; i < calls.array().size(); ++i) {
        if ((int)i >= maxCalls) {
            stoppedEarly = true;
            break;
        }
        const Json& call = calls.at(i);
        Json item = Json::ObjectValue();
        item.Set("index", MakeNumber((double)(i + 1)));
        if (!call.IsObject()) {
            item.Set("ok", MakeBool(false));
            item.Set("error", MakeString("Batch item is not an object."));
            item.Set("code", MakeString("invalid_arguments"));
            results.push_back(item);
            ++errorCount;
            if (stopOnError) {
                stoppedEarly = true;
                break;
            }
            continue;
        }
        std::string toolName = call.Get("tool").AsString();
        item.Set("tool", MakeString(toolName));
        if (toolName.empty() || toolName == "vb6_batch" || !IsKnownTool(toolName)) {
            item.Set("ok", MakeBool(false));
            item.Set("error", MakeString(toolName == "vb6_batch"
                ? "vb6_batch cannot call itself."
                : "Unknown or missing batch tool name."));
            item.Set("code", MakeString("invalid_tool"));
            results.push_back(item);
            ++errorCount;
            if (stopOnError) {
                stoppedEarly = true;
                break;
            }
            continue;
        }
        Json callArgs = call.Get("arguments");
        if (!callArgs.IsObject()) callArgs = Json::ObjectValue();
        ToolResult one = ExecuteTool(toolName, callArgs);
        item.Set("ok", MakeBool(one.ok));
        item.Set("result", one.data);
        if (!one.ok) {
            item.Set("code", MakeString(one.errorCode));
            ++errorCount;
        } else {
            ++successCount;
        }
        results.push_back(item);
        if (!one.ok && stopOnError) {
            stoppedEarly = true;
            break;
        }
    }

    Json result = Json::ObjectValue();
    result.Set("success", MakeBool(errorCount == 0));
    result.Set("requestedCalls", MakeNumber((double)calls.size()));
    result.Set("executedCalls", MakeNumber((double)results.size()));
    result.Set("successCount", MakeNumber((double)successCount));
    result.Set("errorCount", MakeNumber((double)errorCount));
    result.Set("stoppedEarly", MakeBool(stoppedEarly));
    result.Set("results", results);
    if (errorCount > 0) {
        ToolResult failure = ToolResult::Failure(
            "One or more batched tool calls failed. Inspect results[] for partial effects.",
            "batch_failed");
        failure.data = result;
        return failure;
    }
    return ToolResult::Success(result);
}

} // namespace mcpaddin
