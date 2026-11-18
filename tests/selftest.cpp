#include "common.h"
#include "config.h"
#include "log.h"
#include "mcp_server.h"
#include "mock_ide.h"
#include "project_files.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace mcpaddin;

namespace {

const GUID kAddinClsid = {0x7B1B6F2E, 0x6F65, 0x4E5F,
                          {0xA5, 0xB2, 0x4B, 0x19, 0xE1, 0xB8, 0xC0, 0x01}};

std::string ArgValue(int argc, char** argv, const std::string& name,
                     const std::string& defaultValue = "") {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) return argv[i + 1];
    }
    return defaultValue;
}

bool HasArg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (name == argv[i]) return true;
    return false;
}

Json ComProbe(const std::wstring& dllPath) {
    Json result = Json::ObjectValue();
    if (dllPath.empty()) {
        result.Set("skipped", MakeBool(true));
        return result;
    }
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) {
        result.Set("ok", MakeBool(false));
        result.Set("error", Json::StringValue(GetLastErrorMessage()));
        return result;
    }
    typedef HRESULT (STDAPICALLTYPE *GetClassObjectFn)(REFCLSID, REFIID, LPVOID FAR*);
    typedef HRESULT (STDAPICALLTYPE *CanUnloadFn)(void);
    GetClassObjectFn getClassObject =
        (GetClassObjectFn)GetProcAddress(module, "DllGetClassObject");
    CanUnloadFn canUnload = (CanUnloadFn)GetProcAddress(module, "DllCanUnloadNow");
    if (!getClassObject || !canUnload) {
        result.Set("ok", MakeBool(false));
        result.Set("error", MakeString("Required COM exports are missing."));
        FreeLibrary(module);
        return result;
    }
    IClassFactory* factory = NULL;
    HRESULT hr = getClassObject(kAddinClsid, IID_IClassFactory, (LPVOID*)&factory);
    result.Set("getClassObjectHresult", MakeNumber((double)hr));
    if (FAILED(hr) || !factory) {
        result.Set("ok", MakeBool(false));
        result.Set("error", MakeString("DllGetClassObject failed."));
        FreeLibrary(module);
        return result;
    }
    IUnknown* object = NULL;
    hr = factory->CreateInstance(NULL, IID_IUnknown, (void**)&object);
    result.Set("createInstanceHresult", MakeNumber((double)hr));
    if (object) {
        static const IID kIdtExtensibility2 =
            {0xB65AD801, 0xABAF, 0x11D0, {0xBB, 0x8B, 0x00, 0xA0, 0xC9, 0x0F, 0x27, 0x44}};
        IUnknown* extensibility = NULL;
        HRESULT qi = object->QueryInterface(kIdtExtensibility2, (void**)&extensibility);
        result.Set("queryIdtExtensibility2Hresult", MakeNumber((double)qi));
        if (extensibility) extensibility->Release();
        result.Set("ok", MakeBool(SUCCEEDED(hr) && SUCCEEDED(qi)));
        object->Release();
    } else {
        result.Set("ok", MakeBool(false));
    }
    factory->Release();
    HRESULT unload = canUnload();
    result.Set("canUnloadHresult", MakeNumber((double)unload));
    FreeLibrary(module);
    return result;
}

Json ComRegistrationProbe(const std::wstring& dllPath, bool unregister) {
    Json result = Json::ObjectValue();
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) {
        result.Set("ok", MakeBool(false));
        result.Set("error", Json::StringValue(GetLastErrorMessage()));
        return result;
    }
    typedef HRESULT (STDAPICALLTYPE *RegisterFn)(void);
    const char* exportName = unregister ? "DllUnregisterServer" : "DllRegisterServer";
    RegisterFn function = (RegisterFn)GetProcAddress(module, exportName);
    if (!function) {
        result.Set("ok", MakeBool(false));
        result.Set("error", MakeString("registration export missing"));
        FreeLibrary(module);
        return result;
    }
    HRESULT hr = function();
    result.Set("hresult", MakeNumber((double)hr));
    result.Set("ok", MakeBool(SUCCEEDED(hr)));
    if (FAILED(hr)) result.Set("error", Json::StringValue(HResultMessage(hr)));
    FreeLibrary(module);
    return result;
}

int ParseInt(const std::string& value, int fallback) {
    if (value.empty()) return fallback;
    return atoi(value.c_str());
}

void UnitCheck(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool RunUnitTests() {
    {
        std::string error;
        Json value = Json::Parse("{\"a\":[true,null,\"x\\n\\u263a\"],\"n\":2}", &error);
        UnitCheck(error.empty(), "JSON parser rejected valid input");
        UnitCheck(value.Get("n").AsInt() == 2, "JSON number parsing failed");
        UnitCheck(value.Get("a").at(2).AsString().find('\n') != std::string::npos,
                  "JSON escape decoding failed");
        std::string dumped = value.Dump(false);
        Json roundtrip = Json::Parse(dumped, &error);
        UnitCheck(error.empty() && roundtrip == value, "JSON roundtrip failed");
        std::string invalidError;
        Json::Parse("{\"a\":", &invalidError);
        UnitCheck(!invalidError.empty(), "JSON parser accepted invalid input");
    }

    std::wstring unitRoot;
    UnitCheck(CreateTempDirectory(L"unit-", unitRoot), "cannot create unit test directory");
    {
        std::wstring source;
        std::wstring extension;
        std::wstring error;
        UnitCheck(CreateComponentSource("form", "frmUnit", "Unit Form", L"ansi",
                                        source, extension, error),
                  "form source generation failed");
        UnitCheck(extension == L".frm", "form extension is wrong");
        std::vector<std::pair<std::string, Json> > props;
        props.push_back(std::make_pair("Caption", Json::StringValue("OK")));
        props.push_back(std::make_pair("Width", Json::NumberValue(1200)));
        UnitCheck(AddFormControl(source, "commandbutton", "cmdUnit", "", props,
                                 Json::NumberValue(0), error),
                  "form control insertion failed");
        std::vector<FormControlInfo> controls;
        UnitCheck(ListFormControls(source, controls, error), "form control listing failed");
        UnitCheck(controls.size() == 1 && controls[0].name == "cmdUnit", "form control not listed");
        UnitCheck(SetFormControlProperty(source, "cmdUnit", Json::NumberValue(0),
                                         "Caption", Json::StringValue("Go"), error),
                  "form property update failed");
        UnitCheck(source.find(L"\"Go\"") != std::wstring::npos, "form property was not written");
        UnitCheck(RemoveFormControl(source, "cmdUnit", Json::NumberValue(0), error),
                  "form control removal failed");
        UnitCheck(ListFormControls(source, controls, error) && controls.empty(),
                  "form control was not removed");
    }
    {
        ProjectTemplateSpec spec;
        spec.name = "UnitProject";
        spec.title = "Unit Project";
        spec.directory = PathJoin(unitRoot, L"project");
        spec.vbpPath = PathJoin(spec.directory, L"UnitProject.vbp");
        spec.kind = ProjectKind::ActiveXDll;
        std::vector<std::wstring> created;
        std::wstring error;
        UnitCheck(CreateProjectFromTemplate(spec, created, error), "project template creation failed");
        ProjectVbpInfo info;
        UnitCheck(ParseVbpFile(spec.vbpPath, info, error), "VBP parsing failed");
        UnitCheck(info.type == "OleDll" && info.name == "UnitProject", "VBP metadata is wrong");
        UnitCheck(PathExtension(ResolveBuildOutput(info, spec.vbpPath, L"", L"")) == L".dll",
                  "DLL output resolution failed");
        UnitCheck(IsPathWithin(PathJoin(spec.directory, L"x.bas"), spec.directory),
                  "path containment failed");
        UnitCheck(!IsPathWithin(PathJoin(unitRoot, L"other"), spec.directory),
                  "path containment false positive");
    }
    {
        std::wstring configPath = PathJoin(unitRoot, L"config-test.ini");
        SetEnvironmentVariableW(L"MCPADDIN_CONFIG", configPath.c_str());
        SetEnvironmentVariableW(L"MCPADDIN_TOKEN", NULL);
        ServerConfig config;
        std::wstring error;
        UnitCheck(ConfigManager::Load(config, &error), "configuration load failed");
        UnitCheck(config.authToken.size() == 64, "configuration token was not generated");
        UnitCheck(FileExists(configPath), "configuration file was not created");
        SetEnvironmentVariableW(L"MCPADDIN_CONFIG", NULL);
    }
    RemoveDirectoryRecursiveQuiet(unitRoot);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (HasArg(argc, argv, "--unit")) {
        try {
            RunUnitTests();
            printf("UNIT TESTS PASSED\n");
            CoUninitialize();
            return 0;
        } catch (const std::exception& ex) {
            fprintf(stderr, "UNIT TEST FAILED: %s\n", ex.what());
            CoUninitialize();
            return 1;
        }
    }

    if (HasArg(argc, argv, "--register-probe") || HasArg(argc, argv, "--unregister-probe")) {
        std::wstring dll = Utf8ToWide(ArgValue(argc, argv, "--addin-dll", ""));
        Json result = ComRegistrationProbe(dll, HasArg(argc, argv, "--unregister-probe"));
        printf("%s\n", result.Dump(true).c_str());
        CoUninitialize();
        return result.Get("ok").AsBool() ? 0 : 1;
    }

    std::wstring workDir = Utf8ToWide(ArgValue(argc, argv, "--work-dir", ""));
    if (workDir.empty()) {
        workDir = PathJoin(GetTempDirectory(), L"MCPAddinSelfTest-" + Utf8ToWide(NewGuidString()));
    }
    if (!EnsureDirectory(workDir)) {
        fprintf(stderr, "Cannot create work directory.\n");
        return 2;
    }

    ServerConfig config;
    config.bindAddress = "127.0.0.1";
    config.httpPort = ParseInt(ArgValue(argc, argv, "--http-port", "0"), 0);
    config.httpsPort = ParseInt(ArgValue(argc, argv, "--https-port", "0"), 0);
    config.enableHttp = !HasArg(argc, argv, "--no-http");
    config.enableHttps = HasArg(argc, argv, "--https");
    config.authToken = ArgValue(argc, argv, "--token", "selftest-token");
    config.allowQueryToken = true;
    config.logRequests = true;
    config.corsOrigin = "*";
    config.dataDirectory = workDir;
    config.configPath = PathJoin(workDir, L"selftest.ini");
    config.logPath = PathJoin(workDir, L"selftest.log");
    config.sourceEncoding = L"ansi";
    config.allowedRoots.push_back(workDir);
    config.maxBodyBytes = 16 * 1024 * 1024;
    config.requestTimeoutSeconds = 30;
    config.ideTimeoutSeconds = 30;
    config.maxConnections = 64;
    config.keepAliveSeconds = 30;
    config.certificateFile = Utf8ToWide(ArgValue(argc, argv, "--cert-file", ""));
    config.certificatePassword = ArgValue(argc, argv, "--cert-password", "");

    Logger::Initialize(config);
    MockIdeBackend backend(config);
    McpServer server(config, &backend);
    std::wstring error;
    if (!server.Start(error)) {
        fprintf(stderr, "Server start failed: %s\n", WideToUtf8(error).c_str());
        return 3;
    }

    Json probe = ComProbe(Utf8ToWide(ArgValue(argc, argv, "--addin-dll", "")));
    Json ready = Json::ObjectValue();
    ready.Set("ready", MakeBool(true));
    ready.Set("http", MakeString(server.HttpEndpoint()));
    ready.Set("https", MakeString(server.HttpsEndpoint()));
    ready.Set("token", MakeString(config.authToken));
    ready.Set("workDir", Json::StringValue(workDir));
    ready.Set("comProbe", probe);
    ready.Set("status", server.Status());
    printf("%s\n", ready.Dump(false).c_str());

    int runSeconds = ParseInt(ArgValue(argc, argv, "--run-seconds", "120"), 120);
    for (int i = 0; i < runSeconds; ++i) {
        Sleep(1000);
    }
    server.Stop();
    Logger::Shutdown();
    CoUninitialize();
    return 0;
}
