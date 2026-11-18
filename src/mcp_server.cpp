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
const char* kServerVersion = "1.1.0";
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
    Json resources = Json::ObjectValue();
    resources.Set("subscribe", MakeBool(false));
    resources.Set("listChanged", MakeBool(false));
    capabilities.Set("resources", resources);
    Json prompts = Json::ObjectValue();
    prompts.Set("listChanged", MakeBool(false));
    capabilities.Set("prompts", prompts);
    result.Set("capabilities", capabilities);
    Json serverInfo = Json::ObjectValue();
    serverInfo.Set("name", MakeString(kServerName));
    serverInfo.Set("version", MakeString(kServerVersion));
    result.Set("serverInfo", serverInfo);
    result.Set("instructions",
        MakeString("Controls a running Visual Basic 6 IDE. Start with ide_status, then read "
                   "vb6://guide/ai-quickstart when the task is larger than one edit. Preferred flow: "
                   "project_inspect/code_search for existing code; project_scaffold for a new app; "
                   "targeted component_*/code_*/form_* edits; project_validate; project_build with "
                   "method=commandline. Use vb6_batch for many independent operations in one request. "
                   "Project/component selectors accept names, VBP paths, or 1-based indexes."));
    return result;
}

std::string AiQuickStartGuide() {
    return
R"MD(# VB6 MCP: AI quick start

This server controls a live Visual Basic 6 IDE. Treat every write as a real
change to the developer's currently open workspace.

## First calls

1. `ide_status` - confirm the backend, active project, endpoints, and tool groups.
2. `project_list` and `project_inspect` - understand existing projects before editing.
3. `code_search` - find symbols, procedures, event handlers, and constants across the project.
4. `component_get_code`, `code_get_lines`, or `code_structure` - read only the code you need.

## Golden paths

### New application

1. `project_scaffold` with the project metadata and a `components` manifest.
2. Put initial modules, classes, forms, controls, and code in that one manifest.
3. `project_inspect` to verify the generated structure.
4. Implement remaining behavior with `component_set_code`, `code_lines_edit`,
   `code_create_event`, and `form_add_control`.
5. `project_validate`.
6. `project_build` with `method="commandline"` and inspect `compilerOutput`.

### Existing application

1. `project_inspect` for the component/member/control map.
2. `code_search` for the symbol or text.
3. `code_get_lines` for the relevant implementation.
4. Make focused edits.
5. Re-run `project_validate` and `project_build`.

### Many independent operations

Use `vb6_batch` with `calls: [{tool, arguments}, ...]`. Calls run sequentially
and have no rollback. Keep `stopOnError=true` unless you deliberately want to
continue after failures.

## VB6 rules that matter

- `CodeModule` tools edit code, not the form designer header. Use `form_*` or
  `component_replace_file` for complete `.frm`/`.ctl`/`.pag` text.
- Retail VB6 cannot import components through `VBComponents.Import`. File-backed
  operations update the `.vbp`; results with `requiresReload=true` require a
  project reload before the IDE reflects them.
- Preserve `.frx` companions when a form contains binary properties.
- Do not close the main IDE window during unattended work.
- Use `Option Explicit` and valid VB6 identifiers (`[A-Za-z][A-Za-z0-9_]*`).
- Use CRLF in persisted VB6 source when practical.
- `project_validate` is structural only. The compiler is the authority for type,
  syntax, and missing-reference errors.

## Selectors

Project and component selectors accept a VBIDE name, a `.vbp` path, or a 1-based
index (`1` or `#1`). Omit `project` to target the active project.

Reference resource: `vb6://guide/build-complete-app`
Tool groups: `vb6://reference/tool-groups`
)MD";
}

std::string CompleteAppGuide() {
    return
R"MD(# Building a complete VB6 application

`project_scaffold` is the one-call bootstrap. It writes all component files and
the `.vbp`, adds inline form controls, opens the project, applies references and
toolbox ProgIDs, and can build the result.

## Minimal complete app

```json
{
  "directory": "C:\\Projects\\AddressBook",
  "name": "AddressBook",
  "title": "Address Book",
  "type": "standard_exe",
  "startup": "Sub Main",
  "components": [
    {
      "kind": "module",
      "name": "AppMain",
      "code": "Public Sub Main()\r\n    frmMain.Show\r\nEnd Sub"
    },
    {
      "kind": "form",
      "name": "frmMain",
      "title": "Address Book",
      "controls": [
        {"type": "label", "name": "lblName", "properties": {"Caption": "Name", "Left": 240, "Top": 240}},
        {"type": "textbox", "name": "txtName", "properties": {"Left": 240, "Top": 480, "Width": 3600}},
        {"type": "commandbutton", "name": "cmdSave", "properties": {"Caption": "Save", "Left": 240, "Top": 960}}
      ],
      "code": "Private Sub cmdSave_Click()\r\n    MsgBox \"Saved \" & txtName.Text\r\nEnd Sub"
    },
    {
      "kind": "class",
      "name": "Contact",
      "code": "Public Name As String\r\nPublic Phone As String"
    }
  ]
}
```

## Component kinds

- `module`: `.bas`; use for `Sub Main`, shared procedures, and declarations.
- `class`: `.cls`; use for domain objects and services.
- `form`: `.frm`; supports inline `controls`.
- `usercontrol`: `.ctl`; for reusable ActiveX controls.
- `propertypage`: `.pag`; for ActiveX property pages.

## Control properties

`controls[].properties` is an object of VB6 designer property names to values.
Strings are quoted automatically; numbers and booleans are emitted unquoted.
Use `container` for nested controls and `index` for control arrays.

## References

Pass `references: [{path: "C:\\path\\library.dll"}]` or
`references: [{guid: "{...}", "major": 1, "minor": 0}]`. References are applied
after the project opens, so `open` must be true.

## Build

Set `"build": true` and optionally `"buildMethod": "commandline"`,
`"buildOutputPath"`, and `"buildTimeoutSeconds"`. For unattended work, prefer the
command-line method because compiler diagnostics are returned instead of a
modal IDE dialog.

## After scaffolding

Use `project_inspect` to confirm members and controls. Use `vb6_batch` for many
subsequent edits. Finish with `project_validate` and `project_build`.
)MD";
}

std::string DesignerGuide() {
    return
R"MD(# VB6 designer and file compatibility

## Two kinds of component text

1. Code text: the code module exposed by `CodeModule`. Tools: `component_get_code`,
   `component_set_code`, `code_get_lines`, `code_lines_edit`, `code_search`.
2. Complete component text: the exported `.frm`, `.ctl`, `.pag`, `.cls`, or
   `.bas` file. Tools: `form_get_text`, `form_set_text`,
   `component_replace_file`, `component_export`.

`CodeModule` never edits the designer header (`VERSION`, `Begin ... End`,
attributes). Do not try to add controls by editing code lines.

## Live designer vs file fallback

- If a form designer is open, control operations may use
  `VBComponent.Designer` -> `VBForm.VBControls`.
- Retail VB6 does not reliably expose `VBComponents.Import`.
- When native import is unavailable, the add-in writes the component file and
  updates the `.vbp`.
- A result with `requiresReload=true` means the file is correct on disk but the
  IDE must reload the project before it sees the change.

## Forms and FRX files

Binary designer properties live in the companion `.frx` file. Move/export/import
`.frm` and `.frx` together. Do not delete an `.frx` merely because a `.frm`
looks self-contained.

## Safe control editing

1. `form_list_controls` to inspect names, types, indexes, and current properties.
2. `form_add_control`, `form_remove_control`, or
   `form_set_control_property`.
3. Re-list controls after several changes.
4. For large structural changes, export, modify the full text, and use
   `component_replace_file` or `form_set_text`.

## VB6 identifiers and values

- Component and control names must be valid VB6 identifiers.
- Control arrays share a name and use unique `Index` values.
- Strings containing quotes must be JSON-escaped and then VB6-escaped.
- Designer coordinates use VB6 twips.
)MD";
}

std::string ToolGroupsResource() {
    std::string text = "# VB6 MCP tool groups\n\n";
    text += "Groups returned by `ide_status.toolGroups`:\n\n";
    text += GetToolGroups().Dump(true);
    text += "\n\nRecommended workflow:\n\n";
    text += GetRecommendedWorkflow().Dump(true);
    text += "\n";
    return text;
}

std::string ReadStaticResource(const std::string& uri) {
    if (uri == "vb6://guide/ai-quickstart") return AiQuickStartGuide();
    if (uri == "vb6://guide/build-complete-app") return CompleteAppGuide();
    if (uri == "vb6://guide/designer-compatibility") return DesignerGuide();
    if (uri == "vb6://reference/tool-groups") return ToolGroupsResource();
    return std::string();
}

Json ResourceListResult() {
    struct ResourceDef {
        const char* uri;
        const char* name;
        const char* title;
        const char* description;
    };
    static const ResourceDef definitions[] = {
        {"vb6://guide/ai-quickstart", "ai-quickstart", "VB6 MCP AI quick start",
         "Read-first workflow, golden paths, selectors, and VB6 compatibility rules."},
        {"vb6://guide/build-complete-app", "build-complete-app", "Build a complete VB6 application",
         "Declarative project_scaffold manifest, control properties, references, and build flow."},
        {"vb6://guide/designer-compatibility", "designer-compatibility", "VB6 designer compatibility",
         "CodeModule vs complete designer text, live designer vs file fallback, and FRX handling."},
        {"vb6://reference/tool-groups", "tool-groups", "VB6 MCP tool groups and workflow",
         "Tool families and the recommended discover/inspect/edit/validate/build workflow."}
    };
    Json resources = Json::ArrayValue();
    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        Json item = Json::ObjectValue();
        item.Set("uri", MakeString(definitions[i].uri));
        item.Set("name", MakeString(definitions[i].name));
        item.Set("title", MakeString(definitions[i].title));
        item.Set("description", MakeString(definitions[i].description));
        item.Set("mimeType", MakeString("text/markdown"));
        resources.push_back(item);
    }
    return resources;
}

Json PromptListResult() {
    struct PromptDef {
        const char* name;
        const char* title;
        const char* description;
        const char* argName;
        const char* argDescription;
        bool argRequired;
    };
    static const PromptDef definitions[] = {
        {"create-vb6-app", "Create a complete VB6 app", "Scaffold, implement, validate, and build a new VB6 application.",
         "requirements", "Application requirements, screens, data model, and behavior.", false},
        {"inspect-and-extend-vb6-app", "Inspect and extend a VB6 app", "Map an existing project, find the relevant code, edit it, and rebuild.",
         "task", "The requested feature, bug fix, or refactor.", false},
        {"fix-vb6-build", "Diagnose a VB6 build failure", "Validate structure, compile with diagnostics, fix errors, and rebuild.",
         "project", "Project name or VBP path. Omit for the active project.", false},
        {"create-vb6-form", "Create a VB6 form", "Create a form with controls and event handlers using designer-safe operations.",
         "formName", "Desired form name and purpose.", false}
    };
    Json prompts = Json::ArrayValue();
    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        Json item = Json::ObjectValue();
        item.Set("name", MakeString(definitions[i].name));
        item.Set("title", MakeString(definitions[i].title));
        item.Set("description", MakeString(definitions[i].description));
        Json args = Json::ArrayValue();
        Json arg = Json::ObjectValue();
        arg.Set("name", MakeString(definitions[i].argName));
        arg.Set("description", MakeString(definitions[i].argDescription));
        arg.Set("required", MakeBool(definitions[i].argRequired));
        args.push_back(arg);
        item.Set("arguments", args);
        prompts.push_back(item);
    }
    return prompts;
}

Json PromptGetResult(const std::string& name, const Json& arguments) {
    std::string text;
    std::string description;
    std::string requirements = arguments.Get("requirements").AsString();
    std::string task = arguments.Get("task").AsString();
    std::string project = arguments.Get("project").AsString();
    std::string formName = arguments.Get("formName").AsString();
    if (name == "create-vb6-app") {
        description = "Create, validate, and build a complete VB6 application.";
        text = "Build a complete VB6 application through the VB6 MCP server.\n\n"
               "Requirements:\n" + (requirements.empty() ? "None supplied; ask for missing product details before scaffolding.\n" : requirements + "\n") +
               "\nWorkflow:\n"
               "1. Call ide_status.\n"
               "2. Read resource vb6://guide/build-complete-app.\n"
               "3. Design a component manifest: startup module, forms with controls, classes, and shared modules.\n"
               "4. Call project_scaffold once with all initial components and code.\n"
               "5. Call project_inspect and verify names, members, and controls.\n"
               "6. Implement remaining behavior in focused edits; use vb6_batch for repeated operations.\n"
               "7. Call project_validate, then project_build with method=commandline.\n"
               "8. Report outputPath, compiler diagnostics, and any remaining limitations.\n";
    } else if (name == "inspect-and-extend-vb6-app") {
        description = "Inspect an existing VB6 project and implement a requested change.";
        text = "Inspect and extend an existing VB6 application through the VB6 MCP server.\n\n"
               "Task:\n" + (task.empty() ? "Infer the requested change from the conversation.\n" : task + "\n") +
               "\nWorkflow:\n"
               "1. Call ide_status and project_list.\n"
               "2. Call project_inspect for the target project.\n"
               "3. Use code_search and targeted code_get_lines calls; avoid dumping the whole project.\n"
               "4. Make focused code/designer edits and preserve public behavior unless asked to change it.\n"
               "5. Call project_validate and project_build with method=commandline.\n"
               "6. Summarize changed components and verification evidence.\n";
    } else if (name == "fix-vb6-build") {
        description = "Diagnose and fix VB6 build failures.";
        text = "Diagnose a VB6 project build failure.\n\n"
               "Project: " + (project.empty() ? "(active project)" : project) + "\n\n"
               "Workflow:\n"
               "1. Call ide_status.\n"
               "2. Call project_validate for missing files, malformed forms, startup, and broken references.\n"
               "3. Call project_build with method=commandline and capture compilerOutput.\n"
               "4. Locate each compiler error with code_search/code_get_lines.\n"
               "5. Fix the smallest responsible code or project settings.\n"
               "6. Re-run validation and build until success is true.\n"
               "7. Report the exact diagnostics resolved and any remaining warnings.\n";
    } else if (name == "create-vb6-form") {
        description = "Create a VB6 form with designer-safe controls and event handlers.";
        text = "Create a VB6 form using the VB6 MCP server.\n\n"
               "Form request: " + (formName.empty() ? "(derive a valid VB6 name from the requested purpose)" : formName) + "\n\n"
               "Workflow:\n"
               "1. Read vb6://guide/designer-compatibility.\n"
               "2. Use project_scaffold if the project is new; otherwise project_add_form.\n"
               "3. Add controls with form_add_control or one vb6_batch call.\n"
               "4. Add event procedures with code_create_event, then implement them with code_lines_edit or component_set_code.\n"
               "5. Call form_list_controls and project_inspect to verify.\n"
               "6. Validate and build with method=commandline.\n";
    } else {
        return Json::NullValue();
    }
    Json result = Json::ObjectValue();
    result.Set("description", MakeString(description));
    Json messages = Json::ArrayValue();
    Json message = Json::ObjectValue();
    message.Set("role", MakeString("user"));
    Json content = Json::ObjectValue();
    content.Set("type", MakeString("text"));
    content.Set("text", MakeString(text));
    message.Set("content", content);
    messages.push_back(message);
    result.Set("messages", messages);
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
                toolResult.data.Set("serverVersion", MakeString(kServerVersion));
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
    if (method == "resources/list") {
        Json result = Json::ObjectValue();
        result.Set("resources", ResourceListResult());
        return RpcResult(id, result);
    }
    if (method == "resources/templates/list") {
        Json result = Json::ObjectValue();
        result.Set("resourceTemplates", Json::ArrayValue());
        return RpcResult(id, result);
    }
    if (method == "resources/read") {
        if (!params.IsObject() || !params.Get("uri").IsString()) {
            return RpcError(id, -32602, "resources/read requires params.uri.");
        }
        std::string uri = params.Get("uri").AsString();
        std::string text = ReadStaticResource(uri);
        if (text.empty()) {
            return RpcError(id, -32602, "Unknown resource URI: " + uri);
        }
        Json result = Json::ObjectValue();
        Json contents = Json::ArrayValue();
        Json content = Json::ObjectValue();
        content.Set("uri", MakeString(uri));
        content.Set("mimeType", MakeString("text/markdown"));
        content.Set("text", MakeString(text));
        contents.push_back(content);
        result.Set("contents", contents);
        return RpcResult(id, result);
    }
    if (method == "prompts/list") {
        Json result = Json::ObjectValue();
        result.Set("prompts", PromptListResult());
        return RpcResult(id, result);
    }
    if (method == "prompts/get") {
        if (!params.IsObject() || !params.Get("name").IsString()) {
            return RpcError(id, -32602, "prompts/get requires params.name.");
        }
        Json promptArgs = params.Get("arguments");
        if (!promptArgs.IsObject()) promptArgs = Json::ObjectValue();
        Json result = PromptGetResult(params.Get("name").AsString(), promptArgs);
        if (result.IsNull()) {
            return RpcError(id, -32602, "Unknown prompt: " + params.Get("name").AsString());
        }
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
