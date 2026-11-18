#include "mock_ide.h"
#include "project_files.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <set>

namespace mcpaddin {

namespace {

std::string S(const Json& args, const std::string& key, const std::string& def = "") {
    return args.Get(key).IsNull() ? def : args.Get(key).AsString(def);
}

int I(const Json& args, const std::string& key, int def = 0) {
    return args.Get(key).IsNull() ? def : args.Get(key).AsInt(def);
}

bool B(const Json& args, const std::string& key, bool def = false) {
    return args.Get(key).IsNull() ? def : args.Get(key).AsBool(def);
}

std::wstring W(const Json& args, const std::string& key, const std::wstring& def = L"") {
    return args.Get(key).IsNull() ? def : args.Get(key).AsWString(def);
}

std::string ComponentKindFromExtension(const std::wstring& path) {
    std::wstring ext = ToLower(PathExtension(path));
    if (ext == L".frm") return "form";
    if (ext == L".cls") return "class";
    if (ext == L".bas") return "module";
    if (ext == L".ctl") return "usercontrol";
    if (ext == L".pag") return "propertypage";
    return "module";
}

std::string ReplaceSimple(const std::string& source, const std::string& find,
                          const std::string& replacement, bool all, bool matchCase) {
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
        result += replacement;
        pos = hit + find.size();
        if (!all) {
            result.append(source, pos, std::string::npos);
            break;
        }
    }
    return result;
}

bool StartsWithNoCase(const std::wstring& value, const wchar_t* prefix) {
    size_t length = wcslen(prefix);
    return value.size() >= length && _wcsnicmp(value.c_str(), prefix, length) == 0;
}

std::vector<std::wstring> SplitCodeLines(const std::wstring& code) {
    std::vector<std::wstring> lines;
    if (code.empty()) return lines;
    size_t start = 0;
    size_t i = 0;
    while (i < code.size()) {
        if (code[i] == L'\r' || code[i] == L'\n') {
            lines.push_back(code.substr(start, i - start));
            if (code[i] == L'\r' && i + 1 < code.size() && code[i + 1] == L'\n') ++i;
            start = i + 1;
        }
        ++i;
    }
    if (start < code.size()) lines.push_back(code.substr(start));
    return lines;
}

std::wstring JoinCodeLines(const std::vector<std::wstring>& lines) {
    std::wstring code;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) code += L"\r\n";
        code += lines[i];
    }
    if (!code.empty()) code += L"\r\n";
    return code;
}

Json MockWindow(const std::string& caption, const std::string& kind, bool visible) {
    Json window = Json::ObjectValue();
    window.Set("caption", MakeString(caption));
    window.Set("type", MakeNumber(kind == "main" ? 12 : (kind == "designer" ? 1 : 0)));
    window.Set("typeName", MakeString(kind));
    window.Set("visible", MakeBool(visible));
    window.Set("left", MakeNumber(100));
    window.Set("top", MakeNumber(100));
    window.Set("width", MakeNumber(900));
    window.Set("height", MakeNumber(650));
    window.Set("windowState", MakeNumber(0));
    window.Set("windowStateName", MakeString("normal"));
    window.Set("hwnd", MakeNumber(0));
    return window;
}

Json MockCodePane(const std::string& component, long lineCount) {
    Json pane = Json::ObjectValue();
    pane.Set("index", MakeNumber(1));
    pane.Set("moduleName", MakeString(component));
    pane.Set("component", MakeString(component));
    pane.Set("lineCount", MakeNumber((double)lineCount));
    pane.Set("windowCaption", MakeString(component + " [Code]"));
    pane.Set("topLine", MakeNumber(1));
    pane.Set("visibleLineCount", MakeNumber(20));
    pane.Set("codePaneView", MakeNumber(1));
    pane.Set("codePaneViewName", MakeString("fullModule"));
    Json selection = Json::ObjectValue();
    selection.Set("startLine", MakeNumber(1));
    selection.Set("startColumn", MakeNumber(1));
    selection.Set("endLine", MakeNumber(1));
    selection.Set("endColumn", MakeNumber(1));
    pane.Set("selection", selection);
    return pane;
}

} // namespace

MockIdeBackend::MockIdeBackend(const ServerConfig& config) : config_(config) {}
MockIdeBackend::~MockIdeBackend() {}

bool MockIdeBackend::IsAvailable() { return true; }
std::string MockIdeBackend::BackendName() { return "Simulated VB6 IDE"; }

Json MockIdeBackend::Status() {
    Json status = Json::ObjectValue();
    status.Set("connected", MakeBool(true));
    status.Set("simulated", MakeBool(true));
    status.Set("activeProject", MakeString(activeProject_));
    status.Set("projectCount", MakeNumber((double)projects_.size()));
    status.Set("configPath", Json::StringValue(config_.configPath));
    status.Set("toolGroups", GetToolGroups());
    status.Set("recommendedWorkflow", GetRecommendedWorkflow());
    status.Set("aiGuide", MakeString("Read vb6://guide/ai-quickstart. Use project_scaffold for new apps and project_inspect/code_search for existing apps."));
    return status;
}

MockIdeBackend::Project* MockIdeBackend::FindProject(const std::string& selector) {
    if (selector.empty()) {
        std::map<std::string, Project>::iterator it = projects_.find(activeProject_);
        if (it != projects_.end()) return &it->second;
        return projects_.empty() ? NULL : &projects_.begin()->second;
    }
    if (!selector.empty() && selector[0] == '#' && selector.size() > 1) {
        int index = atoi(selector.c_str() + 1);
        if (index >= 1 && index <= (int)projects_.size()) {
            std::map<std::string, Project>::iterator it = projects_.begin();
            std::advance(it, index - 1);
            return &it->second;
        }
    }
    std::map<std::string, Project>::iterator direct = projects_.find(selector);
    if (direct != projects_.end()) return &direct->second;
    for (std::map<std::string, Project>::iterator it = projects_.begin(); it != projects_.end(); ++it) {
        if (IEquals(it->first, selector)) return &it->second;
        if (!it->second.vbpPath.empty()) {
            if (NormalizePathForCompare(it->second.vbpPath) == NormalizePathForCompare(Utf8ToWide(selector))) {
                return &it->second;
            }
            if (IEquals(WideToUtf8(PathFileName(it->second.vbpPath)), selector)) return &it->second;
        }
    }
    return NULL;
}

MockIdeBackend::Component* MockIdeBackend::FindComponent(Project* project, const std::string& selector) {
    if (!project) return NULL;
    std::map<std::string, Component>::iterator direct = project->components.find(selector);
    if (direct != project->components.end()) return &direct->second;
    for (std::map<std::string, Component>::iterator it = project->components.begin();
         it != project->components.end(); ++it) {
        if (IEquals(it->first, selector)) return &it->second;
    }
    return NULL;
}

Json MockIdeBackend::DescribeComponent(const Component& component, bool includeCode) {
    Json item = Json::ObjectValue();
    item.Set("name", MakeString(component.name));
    item.Set("typeName", MakeString(component.kind));
    item.Set("saved", MakeBool(true));
    item.Set("hasOpenDesigner", MakeBool(component.kind == "form" || component.kind == "usercontrol"));
    item.Set("codeLineCount", MakeNumber((double)std::count(component.code.begin(), component.code.end(), L'\n')));
    if (includeCode) item.Set("code", Json::StringValue(component.code));
    return item;
}

Json MockIdeBackend::DescribeProject(const Project& project, bool includeComponents, bool includeCode) {
    Json item = Json::ObjectValue();
    item.Set("name", MakeString(project.name));
    item.Set("fileName", Json::StringValue(project.vbpPath));
    item.Set("projectType", MakeString(project.type));
    item.Set("hostTypeName", MakeString(project.type));
    item.Set("saved", MakeBool(true));
    item.Set("startMode", MakeString(project.startMode.empty() ? "standalone" : project.startMode));
    item.Set("startModeName", MakeString(project.startMode.empty() ? "standalone" : project.startMode));
    if (!project.compatibleOleServer.empty()) {
        item.Set("compatibleOleServer", MakeString(project.compatibleOleServer));
    }
    item.Set("componentCount", MakeNumber((double)project.components.size()));
    item.Set("active", MakeBool(project.name == activeProject_));
    Json references = Json::ArrayValue();
    for (size_t i = 0; i < project.references.size(); ++i) {
        Json reference = Json::ObjectValue();
        reference.Set("name", MakeString(project.references[i]));
        references.push_back(reference);
    }
    item.Set("referenceCount", MakeNumber((double)project.references.size()));
    item.Set("references", references);
    if (includeComponents) {
        Json list = Json::ArrayValue();
        for (std::map<std::string, Component>::const_iterator it = project.components.begin();
             it != project.components.end(); ++it) {
            list.push_back(DescribeComponent(it->second, includeCode));
        }
        item.Set("components", list);
    }
    if (!project.vbpPath.empty() && FileExists(project.vbpPath)) {
        ProjectVbpInfo info;
        std::wstring error;
        if (ParseVbpFile(project.vbpPath, info, error)) {
            item.Set("outputPath", Json::StringValue(ResolveBuildOutput(info, project.vbpPath, L"", L"")));
        }
    }
    return item;
}

void MockIdeBackend::LoadProjectFromFiles(const std::wstring& vbpPath) {
    ProjectVbpInfo info;
    std::wstring error;
    if (!ParseVbpFile(vbpPath, info, error)) return;
    Project project;
    project.name = info.name;
    project.vbpPath = GetFullPath(vbpPath);
    project.type = info.type;
    for (size_t i = 0; i < info.componentEntries.size(); ++i) {
        std::wstring path = Utf8ToWide(info.componentEntries[i]);
        if (!IsAbsolutePath(path)) path = PathJoin(PathDirectory(project.vbpPath), path);
        std::wstring text;
        std::wstring encoding;
        if (!ReadTextFile(path, text, &encoding)) continue;
        Component component;
        component.name = WideToUtf8(PathStem(path));
        component.kind = ComponentKindFromExtension(path);
        component.fileText = text;
        component.code = text;
        size_t option = component.code.find(L"Option Explicit");
        if (option != std::wstring::npos) {
            component.code = component.code.substr(option + wcslen(L"Option Explicit"));
        }
        while (!component.code.empty() && (component.code[0] == L'\r' || component.code[0] == L'\n')) {
            component.code.erase(component.code.begin());
        }
        project.components[component.name] = component;
    }
    projects_[project.name] = project;
    activeProject_ = project.name;
}

ToolResult MockIdeBackend::Dispatch(const std::string& name, const Json& args) {
    if (name == "ide_status") {
        Json status = Status();
        status.Set("backend", MakeString(BackendName()));
        status.Set("capabilities", MakeString("simulated project/component/build operations"));
        return ToolResult::Success(status);
    }
    if (name == "project_list") return ProjectListTool();
    if (name == "project_get") return ProjectGetTool(args);
    if (name == "project_create") return ProjectCreateTool(args);
    if (name == "project_scaffold") return ProjectScaffoldTool(args);
    if (name == "project_inspect") return ProjectInspectTool(args);
    if (name == "project_validate") return ProjectValidateTool(args);
    if (name == "project_open") return ProjectOpenTool(args);
    if (name == "project_close") return ProjectCloseTool(args);
    if (name == "project_save") return ProjectSaveTool(args);
    if (name == "project_set_properties") {
        Project* project = FindProject(S(args, "project"));
        if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
        if (args.Has("name")) {
            std::string newName = S(args, "name");
            std::string oldName = project->name;
            Project copy = *project;
            copy.name = newName;
            projects_.erase(oldName);
            projects_[newName] = copy;
            project = &projects_[newName];
            activeProject_ = newName;
        }
        if (args.Has("startMode")) project->startMode = S(args, "startMode");
        if (args.Has("compatibleOleServer")) {
            project->compatibleOleServer = S(args, "compatibleOleServer");
        }
        return ToolResult::Success(DescribeProject(*project, true, false));
    }
    if (name == "project_read_property") return ProjectReadPropertyTool(args);
    if (name == "project_write_property") return ProjectWritePropertyTool(args);
    if (name == "project_add_toolbox_progid") return ProjectAddToolboxProgIdTool(args);
    if (name == "project_build") return ProjectBuildTool(args);
    if (name == "project_add_component") return AddComponentTool(args);
    if (name == "project_add_form") return AddFormTool(args);
    if (name == "project_add_file") return ImportFileTool(args);
    if (name == "project_create_file") return CreateFileTool(args);
    if (name == "project_remove_component") return RemoveComponentTool(args);
    if (name == "component_list") return ComponentListTool(args);
    if (name == "component_get") return ComponentGetTool(args);
    if (name == "component_get_code") return ComponentGetCodeTool(args);
    if (name == "component_set_code") return ComponentSetCodeTool(args);
    if (name == "component_edit_code") return ComponentEditCodeTool(args);
    if (name == "component_rename") return ComponentRenameTool(args);
    if (name == "component_export") return ComponentExportTool(args);
    if (name == "component_replace_file") return ComponentReplaceTool(args);
    if (name == "component_properties_list") return ComponentPropertiesListTool(args);
    if (name == "component_read_property") return ComponentReadPropertyTool(args);
    if (name == "component_write_property") return ComponentWritePropertyTool(args);
    if (name == "component_action") return ComponentActionTool(args);
    if (name == "code_get_lines") return CodeGetLinesTool(args);
    if (name == "code_lines_edit") return CodeLinesEditTool(args);
    if (name == "code_find") return CodeFindTool(args);
    if (name == "code_search") return CodeSearchTool(args);
    if (name == "code_create_event") return CodeCreateEventTool(args);
    if (name == "code_structure") return CodeStructureTool(args);
    if (name == "code_panes_list") return CodePanesListTool(args);
    if (name == "code_pane_set") return CodePaneSetTool(args);
    if (name == "windows_list") return WindowsListTool(args);
    if (name == "window_action") return WindowActionTool(args);
    if (name == "form_get_text") return FormGetTextTool(args);
    if (name == "form_set_text") return FormSetTextTool(args);
    if (name == "form_list_controls") return FormListControlsTool(args);
    if (name == "form_add_control") return FormAddControlTool(args);
    if (name == "form_remove_control") return FormRemoveControlTool(args);
    if (name == "form_set_control_property") return FormSetPropertyTool(args);
    if (name == "references_list") return ReferenceListTool(args);
    if (name == "reference_add") return ReferenceAddTool(args);
    if (name == "reference_remove") return ReferenceRemoveTool(args);
    if (name == "active_project_set") {
        Project* project = FindProject(S(args, "project"));
        if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
        activeProject_ = project->name;
        Json result = Json::ObjectValue();
        result.Set("active", MakeString(activeProject_));
        return ToolResult::Success(result);
    }
    if (name == "ide_execute_command") {
        Json result = Json::ObjectValue();
        result.Set("executed", MakeBool(true));
        result.Set("command", MakeString(S(args, "command")));
        result.Set("simulated", MakeBool(true));
        return ToolResult::Success(result);
    }
    if (name == "source_read") return SourceReadTool(args);
    if (name == "source_write") return SourceWriteTool(args);
    if (name == "source_edit") return SourceEditTool(args);
    if (name == "source_list") return SourceListTool(args);
    if (name == "vb6_batch") return Vb6BatchTool(args);
    return ToolResult::Failure("Unsupported mock tool: " + name, "not_implemented");
}

ToolResult MockIdeBackend::CallTool(const std::string& name, const Json& arguments) {
    return Dispatch(name, arguments);
}

ToolResult MockIdeBackend::ProjectListTool() {
    Json list = Json::ArrayValue();
    for (std::map<std::string, Project>::iterator it = projects_.begin(); it != projects_.end(); ++it) {
        list.push_back(DescribeProject(it->second, false, false));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)projects_.size()));
    result.Set("projects", list);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectGetTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    return ToolResult::Success(DescribeProject(*project, true, B(args, "includeCode", false)));
}

ToolResult MockIdeBackend::ProjectInspectTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Json result = DescribeProject(*project, true, false);
    bool includeMembers = B(args, "includeMembers", true);
    bool includeControls = B(args, "includeControls", true);
    long memberCount = 0;
    long controlCount = 0;
    Json components = result.Get("components");
    for (size_t i = 0; i < components.size(); ++i) {
        Json& component = components.at(i);
        std::string componentName = component.Get("name").AsString();
        Component* source = FindComponent(project, componentName);
        if (!source) continue;
        if (includeMembers) {
            Json members = Json::ArrayValue();
            std::vector<std::wstring> lines = SplitCodeLines(source->code);
            for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                std::wstring trimmed = Trim(lines[lineIndex]);
                bool isMember = StartsWithNoCase(trimmed, L"Public Sub ") ||
                                StartsWithNoCase(trimmed, L"Private Sub ") ||
                                StartsWithNoCase(trimmed, L"Friend Sub ") ||
                                StartsWithNoCase(trimmed, L"Public Function ") ||
                                StartsWithNoCase(trimmed, L"Private Function ") ||
                                StartsWithNoCase(trimmed, L"Public Property ") ||
                                StartsWithNoCase(trimmed, L"Private Property ");
                if (!isMember) continue;
                size_t open = trimmed.find(L'(');
                size_t space = trimmed.find_last_of(L' ', open == std::wstring::npos ? trimmed.size() : open);
                if (space == std::wstring::npos) continue;
                size_t end = open == std::wstring::npos ? trimmed.size() : open;
                Json member = Json::ObjectValue();
                member.Set("name", Json::StringValue(trimmed.substr(space + 1, end - space - 1)));
                member.Set("line", MakeNumber((double)(lineIndex + 1)));
                members.push_back(member);
                ++memberCount;
            }
            component.Set("members", members);
        }
        if (includeControls && (source->kind == "form" || source->kind == "usercontrol" ||
                                source->kind == "propertypage")) {
            std::vector<FormControlInfo> controls;
            std::wstring error;
            if (ListFormControls(source->fileText, controls, error)) {
                Json list = Json::ArrayValue();
                for (size_t c = 0; c < controls.size(); ++c) {
                    Json control = Json::ObjectValue();
                    control.Set("type", MakeString(controls[c].type));
                    control.Set("name", MakeString(controls[c].name));
                    control.Set("line", MakeNumber((double)controls[c].line));
                    if (controls[c].hasIndex) control.Set("index", MakeNumber((double)controls[c].index));
                    list.push_back(control);
                    ++controlCount;
                }
                component.Set("controls", list);
            } else {
                Json controlError = Json::ObjectValue();
                controlError.Set("error", Json::StringValue(error));
                component.Set("controlError", controlError);
            }
        }
    }
    result.Set("components", components);
    result.Set("memberCount", MakeNumber((double)memberCount));
    result.Set("controlCount", MakeNumber((double)controlCount));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectValidateTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    ProjectVbpInfo info;
    std::wstring error;
    if (!ParseVbpFile(project->vbpPath, info, error)) {
        return ToolResult::Failure(WideToUtf8(error), "vbp_parse_error");
    }
    Json diagnostics = Json::ArrayValue();
    int errors = 0;
    int warnings = 0;
    for (size_t i = 0; i < info.componentEntries.size(); ++i) {
        std::wstring path = Utf8ToWide(info.componentEntries[i]);
        if (!IsAbsolutePath(path)) path = PathJoin(PathDirectory(project->vbpPath), path);
        if (!FileExists(path)) {
            Json diag = Json::ObjectValue();
            diag.Set("severity", MakeString("error"));
            diag.Set("code", MakeString("missing_component_file"));
            diag.Set("path", Json::StringValue(path));
            diagnostics.push_back(diag);
            ++errors;
        }
    }
    bool startupResolved = info.startup.empty();
    if (!info.startup.empty()) {
        for (std::map<std::string, Component>::iterator it = project->components.begin();
             it != project->components.end(); ++it) {
            if (IEquals(it->first, info.startup) ||
                (IEquals(info.startup, "Sub Main") &&
                 it->second.code.find(L"Sub Main") != std::wstring::npos)) {
                startupResolved = true;
                break;
            }
        }
    }
    if (!startupResolved) {
        Json diag = Json::ObjectValue();
        diag.Set("severity", MakeString("error"));
        diag.Set("code", MakeString("startup_not_found"));
        diag.Set("message", MakeString("The configured startup object was not found."));
        diagnostics.push_back(diag);
        ++errors;
    }
    Json result = Json::ObjectValue();
    result.Set("valid", MakeBool(errors == 0 && (!B(args, "strict", false) || warnings == 0)));
    result.Set("projectType", MakeString(info.type));
    result.Set("vbpPath", Json::StringValue(project->vbpPath));
    result.Set("componentCount", MakeNumber((double)info.componentEntries.size()));
    result.Set("startup", MakeString(info.startup));
    result.Set("errorCount", MakeNumber((double)errors));
    result.Set("warningCount", MakeNumber((double)warnings));
    result.Set("infoCount", MakeNumber(0));
    result.Set("diagnostics", diagnostics);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectCreateTool(const Json& args) {
    std::wstring directory = Utf8ToWide(S(args, "directory"));
    std::wstring vbp = Utf8ToWide(S(args, "vbpPath"));
    std::string name = S(args, "name");
    if (directory.empty() && !vbp.empty()) directory = PathDirectory(GetFullPath(vbp));
    if (vbp.empty() && !directory.empty()) {
        if (name.empty()) name = "Project1";
        vbp = PathJoin(GetFullPath(directory), Utf8ToWide(name) + L".vbp");
    }
    if (directory.empty() || vbp.empty()) return ToolResult::Failure("directory or vbpPath is required.", "invalid_arguments");
    if (name.empty()) name = WideToUtf8(PathStem(vbp));
    ProjectKind kind = ProjectKind::StandardExe;
    if (!ProjectKindFromString(S(args, "type", "standard_exe"), kind)) {
        return ToolResult::Failure("Unknown project type.", "invalid_arguments");
    }
    ProjectTemplateSpec spec;
    spec.name = name;
    spec.title = S(args, "title", name);
    spec.directory = GetFullPath(directory);
    spec.vbpPath = GetFullPath(vbp);
    spec.kind = kind;
    spec.overwrite = B(args, "overwrite", false);
    std::vector<std::wstring> files;
    std::wstring error;
    if (!CreateProjectFromTemplate(spec, files, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_create_failed");
    }
    LoadProjectFromFiles(spec.vbpPath);
    Json result = Json::ObjectValue();
    result.Set("vbpPath", Json::StringValue(spec.vbpPath));
    result.Set("filesCreated", Json::ArrayValue());
    Json created = Json::ArrayValue();
    for (size_t i = 0; i < files.size(); ++i) created.push_back(Json::StringValue(files[i]));
    result.Set("filesCreated", created);
    Project* project = FindProject(name);
    if (project && B(args, "open", true)) result.Set("project", DescribeProject(*project, true, false));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectScaffoldTool(const Json& args) {
    std::wstring directory = Utf8ToWide(S(args, "directory"));
    std::wstring vbp = Utf8ToWide(S(args, "vbpPath"));
    std::string name = S(args, "name");
    if (directory.empty() && !vbp.empty()) directory = PathDirectory(GetFullPath(vbp));
    if (vbp.empty() && !directory.empty()) {
        if (name.empty()) name = "Project1";
        vbp = PathJoin(GetFullPath(directory), Utf8ToWide(name) + L".vbp");
    }
    if (directory.empty() || vbp.empty()) {
        return ToolResult::Failure("directory or vbpPath is required.", "invalid_arguments");
    }
    if (name.empty()) name = WideToUtf8(PathStem(vbp));
    ProjectKind kind = ProjectKind::StandardExe;
    if (!ProjectKindFromString(S(args, "type", "standard_exe"), kind)) {
        return ToolResult::Failure("Unknown project type.", "invalid_arguments");
    }

    ProjectTemplateSpec spec;
    spec.name = name;
    spec.title = S(args, "title", name);
    spec.description = S(args, "description");
    spec.company = S(args, "company");
    spec.product = S(args, "product");
    spec.directory = GetFullPath(directory);
    spec.vbpPath = GetFullPath(vbp);
    spec.startup = S(args, "startup");
    spec.kind = kind;
    spec.overwrite = B(args, "overwrite", false);
    spec.createDefaultStartup = B(args, "startupModule", true);

    const Json& specs = args.Get("components");
    std::set<std::string> names;
    std::set<std::string> files;
    Json describedComponents = Json::ArrayValue();
    if (specs.IsArray() && !specs.array().empty()) {
        for (size_t i = 0; i < specs.array().size(); ++i) {
            const Json& item = specs.at(i);
            ProjectComponentSpec component;
            component.kind = S(item, "kind");
            component.name = S(item, "name");
            component.fileName = S(item, "fileName");
            if (!IsValidVbIdentifier(component.name)) {
                return ToolResult::Failure("Invalid component name.", "invalid_arguments");
            }
            if (!names.insert(ToLowerAscii(component.name)).second) {
                return ToolResult::Failure("Duplicate component name.", "invalid_arguments");
            }
            std::wstring extension;
            std::wstring source = W(item, "source");
            if (source.empty()) {
                std::wstring error;
                if (!CreateComponentSource(component.kind, component.name, S(item, "title", component.name),
                                           L"ansi", source, extension, error)) {
                    return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
                }
            } else {
                std::wstring error;
                std::wstring ignored;
                CreateComponentSource(component.kind, component.name, component.name,
                                      L"ansi", ignored, extension, error);
            }
            if (component.fileName.empty()) component.fileName = component.name;
            if (PathExtension(Utf8ToWide(component.fileName)).empty()) {
                std::wstring fileName = Utf8ToWide(component.fileName);
                std::wstring withExtension = PathStem(fileName) + extension;
                std::wstring parent = PathDirectory(fileName);
                component.fileName = parent.empty() ? WideToUtf8(withExtension)
                                                    : WideToUtf8(PathJoin(parent, withExtension));
            }
            std::string full = ToLowerAscii(WideToUtf8(GetFullPath(
                PathJoin(spec.directory, Utf8ToWide(component.fileName)))));
            if (!files.insert(full).second) {
                return ToolResult::Failure("Duplicate component file.", "invalid_arguments");
            }
            const Json& controls = item.Get("controls");
            if (controls.IsArray() && !controls.array().empty()) {
                for (size_t c = 0; c < controls.array().size(); ++c) {
                    const Json& control = controls.at(c);
                    std::vector<std::pair<std::string, Json> > properties;
                    const Json& props = control.Get("properties");
                    if (props.IsObject()) {
                        for (std::map<std::string, Json>::const_iterator it = props.object().begin();
                             it != props.object().end(); ++it) {
                            properties.push_back(std::make_pair(it->first, it->second));
                        }
                    }
                    std::wstring error;
                    if (!AddFormControl(source, S(control, "type"), S(control, "name"),
                                        S(control, "container"), properties,
                                        control.Get("index"), error)) {
                        return ToolResult::Failure(WideToUtf8(error), "invalid_arguments");
                    }
                }
            }
            std::string code = S(item, "code");
            if (!code.empty()) {
                source = NormalizeNewlines(source);
                source += L"\r\n" + NormalizeNewlines(Utf8ToWide(code)) + L"\r\n";
            }
            component.source = source;
            component.sourceProvided = true;
            spec.components.push_back(component);
            Json described = Json::ObjectValue();
            described.Set("kind", MakeString(component.kind));
            described.Set("name", MakeString(component.name));
            described.Set("fileName", MakeString(component.fileName));
            describedComponents.push_back(described);
        }
    }

    std::vector<std::wstring> created;
    std::wstring error;
    if (!CreateProjectFromTemplate(spec, created, error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_scaffold_failed");
    }
    LoadProjectFromFiles(spec.vbpPath);
    Project* project = FindProject(name);
    if (!project) return ToolResult::Failure("Could not load scaffolded project.", "vbp_parse_error");
    project->references.clear();
    const Json& references = args.Get("references");
    if (references.IsArray()) {
        for (size_t i = 0; i < references.array().size(); ++i) {
            std::string ref = S(references.at(i), "path", S(references.at(i), "guid"));
            project->references.push_back(ref);
        }
    }

    Json result = Json::ObjectValue();
    result.Set("success", MakeBool(true));
    result.Set("vbpPath", Json::StringValue(spec.vbpPath));
    Json fileList = Json::ArrayValue();
    for (size_t i = 0; i < created.size(); ++i) fileList.push_back(Json::StringValue(created[i]));
    result.Set("filesCreated", fileList);
    result.Set("projectType", MakeString(ProjectKindToString(kind)));
    result.Set("components", describedComponents);
    result.Set("warnings", Json::ArrayValue());
    result.Set("requiresReload", MakeBool(false));
    result.Set("project", DescribeProject(*project, true, false));
    if (B(args, "build", false)) {
        Json buildArgs = Json::ObjectValue();
        buildArgs.Set("project", Json::StringValue(name));
        ToolResult build = ProjectBuildTool(buildArgs);
        result.Set("build", build.data);
        if (!build.ok) {
            ToolResult failure = ToolResult::Failure("Scaffold build failed.", "build_failed");
            failure.data = result;
            return failure;
        }
    }
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectOpenTool(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(S(args, "path")));
    if (!FileExists(path)) return ToolResult::Failure("VBP file not found.", "file_not_found");
    LoadProjectFromFiles(path);
    Project* project = FindProject(WideToUtf8(PathStem(path)));
    if (!project) return ToolResult::Failure("Could not load project.", "vbp_parse_error");
    return ToolResult::Success(DescribeProject(*project, true, false));
}

ToolResult MockIdeBackend::ProjectCloseTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string name = project->name;
    if (B(args, "save", true)) ProjectSaveTool(args);
    projects_.erase(name);
    if (activeProject_ == name && !projects_.empty()) activeProject_ = projects_.begin()->first;
    Json result = Json::ObjectValue();
    result.Set("closed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectSaveTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    if (!project->vbpPath.empty()) {
        for (std::map<std::string, Component>::iterator it = project->components.begin();
             it != project->components.end(); ++it) {
            std::wstring ext = it->second.kind == "form" ? L".frm" :
                               (it->second.kind == "class" ? L".cls" :
                                (it->second.kind == "module" ? L".bas" : L".ctl"));
            std::wstring path = PathJoin(PathDirectory(project->vbpPath), Utf8ToWide(it->first) + ext);
            if (it->second.kind == "form" || it->second.kind == "usercontrol") {
                WriteTextFileAtomic(path, it->second.fileText, L"ansi");
            } else {
                std::wstring text = it->second.fileText;
                size_t option = text.find(L"Option Explicit");
                if (option != std::wstring::npos) {
                    text = text.substr(0, option + wcslen(L"Option Explicit")) + L"\r\n" + it->second.code;
                } else {
                    text += L"\r\n" + it->second.code;
                }
                WriteTextFileAtomic(path, text, L"ansi");
            }
        }
    }
    return ToolResult::Success(DescribeProject(*project, true, false));
}

ToolResult MockIdeBackend::ProjectReadPropertyTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string section = S(args, "section");
    std::string key = S(args, "key");
    if (section.empty() || key.empty()) {
        return ToolResult::Failure("section and key are required.", "invalid_arguments");
    }
    std::string composite = section + "|" + key;
    std::map<std::string, std::string>::iterator found = project->properties.find(composite);
    Json result = Json::ObjectValue();
    result.Set("section", MakeString(section));
    result.Set("key", MakeString(key));
    result.Set("value", MakeString(found == project->properties.end() ? "" : found->second));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectWritePropertyTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string section = S(args, "section");
    std::string key = S(args, "key");
    if (section.empty() || key.empty()) {
        return ToolResult::Failure("section and key are required.", "invalid_arguments");
    }
    project->properties[section + "|" + key] = S(args, "value");
    Json result = Json::ObjectValue();
    result.Set("section", MakeString(section));
    result.Set("key", MakeString(key));
    result.Set("value", args.Get("value"));
    result.Set("written", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectAddToolboxProgIdTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string progId = S(args, "progId");
    if (progId.empty()) return ToolResult::Failure("progId is required.", "invalid_arguments");
    Json result = Json::ObjectValue();
    result.Set("progId", MakeString(progId));
    result.Set("added", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ProjectBuildTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    ProjectVbpInfo info;
    std::wstring error;
    ParseVbpFile(project->vbpPath, info, error);
    std::wstring output = ResolveBuildOutput(info, project->vbpPath, W(args, "outputPath"), L"");
    EnsureDirectory(PathDirectory(output));
    std::vector<unsigned char> payload;
    const char* text = "MCPAddin simulated VB6 build output\r\n";
    payload.assign(text, text + strlen(text));
    DWORD writeError = 0;
    if (!WriteFileBytesAtomic(output, payload, &writeError)) {
        return ToolResult::Failure("Cannot write simulated build output.", "build_failed");
    }
    Json result = Json::ObjectValue();
    result.Set("success", MakeBool(true));
    result.Set("method", MakeString("simulated"));
    result.Set("outputPath", Json::StringValue(output));
    result.Set("outputExists", MakeBool(true));
    result.Set("outputSize", MakeNumber((double)FileSize(output)));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::AddComponentTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string kind = S(args, "kind");
    std::string name = S(args, "name");
    if (name.empty()) name = "Component1";
    if (!IsValidVbIdentifier(name)) return ToolResult::Failure("Invalid component name.", "invalid_arguments");
    Component component;
    component.name = name;
    component.kind = kind;
    std::wstring ext;
    std::wstring error;
    CreateComponentSource(kind, name, name, L"ansi", component.fileText, ext, error);
    component.code = W(args, "code");
    project->components[name] = component;
    return ToolResult::Success(DescribeComponent(project->components[name], true));
}

ToolResult MockIdeBackend::AddFormTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string name = S(args, "name");
    if (!IsValidVbIdentifier(name)) return ToolResult::Failure("Invalid form name.", "invalid_arguments");

    std::wstring source = W(args, "source");
    std::wstring ext = L".frm";
    std::wstring error;
    if (source.empty()) {
        if (!CreateComponentSource("form", name, S(args, "title", name),
                                   L"ansi", source, ext, error)) {
            return ToolResult::Failure("Invalid form source.", "invalid_arguments");
        }
        std::string code = S(args, "code");
        if (!code.empty()) source += L"\r\n" + Utf8ToWide(code) + L"\r\n";
    }

    std::wstring fileName = Utf8ToWide(S(args, "fileName", name + ".frm"));
    if (!IsAbsolutePath(fileName)) fileName = PathJoin(PathDirectory(project->vbpPath), fileName);
    if (!B(args, "overwrite", false) && FileExists(fileName)) {
        return ToolResult::Failure("Form file already exists.", "file_exists");
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(fileName, source, L"ansi", &writeError)) {
        return ToolResult::Failure("Cannot write form file.", "filesystem_error");
    }
    std::wstring vbpValue = NormalizePathForCompare(PathDirectory(fileName)) ==
                            NormalizePathForCompare(PathDirectory(project->vbpPath)) ?
                            PathFileName(fileName) : fileName;
    if (!UpdateVbpComponentEntry(project->vbpPath, "form", name, vbpValue,
                                 B(args, "startup", false), error)) {
        return ToolResult::Failure(WideToUtf8(error), "project_file_update_failed");
    }

    Component component;
    component.name = name;
    component.kind = "form";
    component.fileText = source;
    component.code = W(args, "code");
    project->components[name] = component;

    Json result = Json::ObjectValue();
    result.Set("name", MakeString(name));
    result.Set("path", Json::StringValue(fileName));
    result.Set("vbpPath", Json::StringValue(project->vbpPath));
    result.Set("requiresReload", MakeBool(false));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ImportFileTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::wstring path = GetFullPath(Utf8ToWide(S(args, "path")));
    std::wstring text;
    if (!ReadTextFile(path, text)) return ToolResult::Failure("Cannot read component file.", "file_not_found");
    Component component;
    component.name = WideToUtf8(PathStem(path));
    component.kind = ComponentKindFromExtension(path);
    component.fileText = text;
    component.code = text;
    project->components[component.name] = component;
    return ToolResult::Success(DescribeComponent(project->components[component.name], true));
}

ToolResult MockIdeBackend::CreateFileTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string kind = S(args, "kind");
    std::string name = S(args, "name");
    std::wstring source = W(args, "source");
    std::wstring ext;
    std::wstring error;
    if (source.empty()) {
        if (!CreateComponentSource(kind, name, name, L"ansi", source, ext, error)) {
            return ToolResult::Failure("Invalid component kind.", "invalid_arguments");
        }
    }
    std::wstring fileName = Utf8ToWide(S(args, "fileName", name + WideToUtf8(ext)));
    if (!IsAbsolutePath(fileName)) fileName = PathJoin(PathDirectory(project->vbpPath), fileName);
    if (!B(args, "overwrite", false) && FileExists(fileName)) {
        return ToolResult::Failure("Component file already exists: " + WideToUtf8(fileName),
                                   "file_exists");
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(fileName, source, config_.sourceEncoding, &writeError)) {
        return ToolResult::Failure("Cannot write component file.", "filesystem_error");
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(fileName));
    if (B(args, "import", true)) {
        Component component;
        component.name = name;
        component.kind = kind;
        component.fileText = source;
        component.code = W(args, "code");
        project->components[name] = component;
        result.Set("component", DescribeComponent(component, false));
    }
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::RemoveComponentTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string name = S(args, "component");
    Component* component = FindComponent(project, name);
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    if (B(args, "saveFile", false)) {
        std::wstring path = Utf8ToWide(S(args, "exportPath"));
        WriteTextFileAtomic(GetFullPath(path), component->fileText, L"ansi");
    }
    project->components.erase(component->name);
    Json result = Json::ObjectValue();
    result.Set("removed", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentListTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Json list = Json::ArrayValue();
    for (std::map<std::string, Component>::iterator it = project->components.begin();
         it != project->components.end(); ++it) {
        list.push_back(DescribeComponent(it->second, false));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)project->components.size()));
    result.Set("components", list);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentGetTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    return ToolResult::Success(DescribeComponent(*component, B(args, "includeCode", true)));
}

ToolResult MockIdeBackend::ComponentGetCodeTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    Json result = Json::ObjectValue();
    result.Set("component", MakeString(component->name));
    result.Set("code", Json::StringValue(component->code));
    result.Set("lineCount", MakeNumber((double)std::count(component->code.begin(), component->code.end(), L'\n')));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentSetCodeTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    component->code = W(args, "code");
    Json result = Json::ObjectValue();
    result.Set("component", MakeString(component->name));
    result.Set("lineCount", MakeNumber((double)std::count(component->code.begin(), component->code.end(), L'\n')));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentEditCodeTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string source = WideToUtf8(component->code);
    std::string updated = ReplaceSimple(source, S(args, "find"), S(args, "replace"),
                                       B(args, "all", true), B(args, "matchCase", false));
    bool changed = updated != source;
    component->code = Utf8ToWide(updated);
    Json result = Json::ObjectValue();
    result.Set("replaced", MakeBool(changed));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentRenameTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string oldName = component->name;
    std::string newName = S(args, "newName");
    if (!IsValidVbIdentifier(newName)) return ToolResult::Failure("Invalid newName.", "invalid_arguments");
    Component copy = *component;
    copy.name = newName;
    project->components.erase(oldName);
    project->components[newName] = copy;
    return ToolResult::Success(DescribeComponent(project->components[newName], false));
}

ToolResult MockIdeBackend::ComponentExportTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::wstring path = GetFullPath(Utf8ToWide(S(args, "path")));
    if (!WriteTextFileAtomic(path, component->fileText, L"ansi")) {
        return ToolResult::Failure("Cannot export component.", "filesystem_error");
    }
    Json result = DescribeComponent(*component, false);
    result.Set("exportedPath", Json::StringValue(path));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentReplaceTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    component->fileText = W(args, "text");
    return ToolResult::Success(DescribeComponent(*component, false));
}

ToolResult MockIdeBackend::ComponentPropertiesListTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    bool includeValues = B(args, "includeValues", true);
    Json list = Json::ArrayValue();
    for (std::map<std::string, std::string>::const_iterator it = component->properties.begin();
         it != component->properties.end(); ++it) {
        Json item = Json::ObjectValue();
        item.Set("name", MakeString(it->first));
        item.Set("numIndices", MakeNumber(0));
        if (includeValues) item.Set("value", MakeString(it->second));
        list.push_back(item);
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)component->properties.size()));
    result.Set("properties", list);
    result.Set("component", DescribeComponent(*component, false));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentReadPropertyTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string key = S(args, "key");
    if (key.empty()) return ToolResult::Failure("key is required.", "invalid_arguments");
    std::map<std::string, std::string>::const_iterator found = component->properties.find(key);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("key", MakeString(key));
    result.Set("value", MakeString(found == component->properties.end() ? "" : found->second));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentWritePropertyTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string key = S(args, "key");
    if (key.empty()) return ToolResult::Failure("key is required.", "invalid_arguments");
    component->properties[key] = S(args, "value");
    Json result = DescribeComponent(*component, false);
    result.Set("key", MakeString(key));
    result.Set("value", args.Get("value"));
    result.Set("written", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ComponentActionTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string action = ToLowerAscii(S(args, "action"));
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    if (action == "activate") {
        result.Set("activated", MakeBool(true));
    } else if (action == "reload") {
        result.Set("reloaded", MakeBool(true));
    } else if (action == "saveas") {
        std::string path = S(args, "path");
        if (path.empty()) return ToolResult::Failure("path is required.", "invalid_arguments");
        result.Set("path", Json::StringValue(GetFullPath(Utf8ToWide(path))));
        result.Set("saved", MakeBool(true));
    } else if (action == "designerwindow" || action == "designer") {
        result.Set("window", MockWindow(component->name + " [Design]", "designer", true));
    } else {
        return ToolResult::Failure("Unknown component action.", "invalid_arguments");
    }
    result.Set("component", DescribeComponent(*component, false));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeGetLinesTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::vector<std::wstring> lines = SplitCodeLines(component->code);
    long total = (long)lines.size();
    long start = I(args, "startLine", 1);
    if (start < 1) start = 1;
    if (total > 0 && start > total) {
        return ToolResult::Failure("startLine is past the end of the module.", "invalid_arguments");
    }
    long count = I(args, "count", 0);
    if (count <= 0) count = total >= start ? total - start + 1 : 0;
    if (total > 0 && start + count - 1 > total) {
        return ToolResult::Failure("Requested line range exceeds the module.", "invalid_arguments");
    }
    std::wstring text;
    for (long i = 0; i < count; ++i) {
        if (i > 0) text += L"\r\n";
        text += lines[(size_t)(start - 1 + i)];
    }
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("startLine", MakeNumber((double)start));
    result.Set("count", MakeNumber((double)count));
    result.Set("endLine", MakeNumber((double)(count > 0 ? start + count - 1 : start)));
    result.Set("totalLines", MakeNumber((double)total));
    result.Set("lines", Json::StringValue(text));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeLinesEditTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::vector<std::wstring> lines = SplitCodeLines(component->code);
    std::string action = ToLowerAscii(S(args, "action"));
    long line = I(args, "line", 0);
    long count = I(args, "count", 1);
    std::vector<std::wstring> inserted = SplitCodeLines(W(args, "text"));
    if (action == "insert") {
        if (line < 1 || line > (long)lines.size() + 1) {
            return ToolResult::Failure("line is outside the insertable range.", "invalid_arguments");
        }
        if (inserted.empty()) inserted.push_back(L"");
        lines.insert(lines.begin() + (line - 1), inserted.begin(), inserted.end());
    } else if (action == "replace") {
        if (line < 1 || line > (long)lines.size()) {
            return ToolResult::Failure("line is outside the module.", "invalid_arguments");
        }
        lines[(size_t)(line - 1)] = W(args, "text");
    } else if (action == "delete") {
        if (line < 1 || count < 1 || line + count - 1 > (long)lines.size()) {
            return ToolResult::Failure("Delete range is outside the module.", "invalid_arguments");
        }
        lines.erase(lines.begin() + (line - 1), lines.begin() + (line - 1 + count));
    } else if (action == "append") {
        lines.insert(lines.end(), inserted.begin(), inserted.end());
    } else {
        return ToolResult::Failure("Unknown code edit action.", "invalid_arguments");
    }
    component->code = JoinCodeLines(lines);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("action", MakeString(action));
    result.Set("line", MakeNumber((double)line));
    result.Set("count", MakeNumber((double)count));
    result.Set("totalLines", MakeNumber((double)SplitCodeLines(component->code).size()));
    result.Set("edited", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeFindTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::wstring target = W(args, "target");
    if (target.empty()) return ToolResult::Failure("target is required.", "invalid_arguments");
    bool matchCase = B(args, "matchCase", false);
    std::vector<std::wstring> lines = SplitCodeLines(component->code);
    long startLine = I(args, "startLine", 1);
    long startColumn = I(args, "startColumn", 1);
    long foundLine = 0;
    long foundColumn = 0;
    for (long lineIndex = startLine; lineIndex <= (long)lines.size() && foundLine == 0; ++lineIndex) {
        std::wstring haystack = lines[(size_t)(lineIndex - 1)];
        std::wstring needle = target;
        if (!matchCase) {
            haystack = ToLower(haystack);
            needle = ToLower(needle);
        }
        size_t offset = lineIndex == startLine && startColumn > 1 ? (size_t)(startColumn - 1) : 0;
        size_t hit = haystack.find(needle, offset);
        if (hit != std::wstring::npos) {
            foundLine = lineIndex;
            foundColumn = (long)hit + 1;
        }
    }
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("target", Json::StringValue(target));
    result.Set("found", MakeBool(foundLine > 0));
    result.Set("startLine", MakeNumber((double)(foundLine > 0 ? foundLine : startLine)));
    result.Set("startColumn", MakeNumber((double)(foundLine > 0 ? foundColumn : startColumn)));
    result.Set("endLine", MakeNumber((double)(foundLine > 0 ? foundLine : startLine)));
    result.Set("endColumn", MakeNumber((double)(foundLine > 0 ? foundColumn + (long)target.size() : startColumn)));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeSearchTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::wstring query = W(args, "query");
    if (query.empty()) return ToolResult::Failure("query is required.", "invalid_arguments");
    bool matchCase = B(args, "matchCase", false);
    bool wholeWord = B(args, "wholeWord", false);
    int maxResults = I(args, "maxResults", 200);
    if (maxResults < 1) maxResults = 1;
    if (maxResults > 1000) maxResults = 1000;
    std::string componentFilter = S(args, "component");
    std::wstring needle = matchCase ? query : ToLower(query);
    Json matches = Json::ArrayValue();
    bool truncated = false;
    int searched = 0;
    auto isWord = [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'_';
    };
    for (std::map<std::string, Component>::iterator it = project->components.begin();
         it != project->components.end() && !truncated; ++it) {
        if (!componentFilter.empty() && !IEquals(it->first, componentFilter)) continue;
        ++searched;
        std::vector<std::wstring> lines = SplitCodeLines(it->second.code);
        for (size_t lineIndex = 0; lineIndex < lines.size() && !truncated; ++lineIndex) {
            std::wstring haystack = matchCase ? lines[lineIndex] : ToLower(lines[lineIndex]);
            size_t offset = 0;
            while (offset <= haystack.size()) {
                size_t hit = haystack.find(needle, offset);
                if (hit == std::wstring::npos) break;
                bool ok = true;
                if (wholeWord) {
                    if (hit > 0 && isWord(lines[lineIndex][hit - 1])) ok = false;
                    size_t after = hit + query.size();
                    if (after < lines[lineIndex].size() && isWord(lines[lineIndex][after])) ok = false;
                }
                if (ok) {
                    Json match = Json::ObjectValue();
                    match.Set("component", MakeString(it->first));
                    match.Set("componentType", MakeString(it->second.kind));
                    match.Set("line", MakeNumber((double)(lineIndex + 1)));
                    match.Set("column", MakeNumber((double)(hit + 1)));
                    match.Set("text", Json::StringValue(lines[lineIndex]));
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
    result.Set("count", MakeNumber((double)matches.size()));
    result.Set("searchedComponents", MakeNumber((double)searched));
    result.Set("truncated", MakeBool(truncated));
    result.Set("matches", matches);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeCreateEventTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::string eventName = S(args, "eventName");
    std::string objectName = S(args, "objectName");
    if (eventName.empty() || objectName.empty()) {
        return ToolResult::Failure("eventName and objectName are required.", "invalid_arguments");
    }
    std::vector<std::wstring> lines = SplitCodeLines(component->code);
    long line = (long)lines.size() + 1;
    if (!lines.empty() && !lines.back().empty()) lines.push_back(L"");
    lines.push_back(Utf8ToWide("Private Sub " + objectName + "_" + eventName + "()"));
    lines.push_back(L"");
    lines.push_back(L"End Sub");
    component->code = JoinCodeLines(lines);
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("eventName", MakeString(eventName));
    result.Set("objectName", MakeString(objectName));
    result.Set("line", MakeNumber((double)line));
    result.Set("created", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodeStructureTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::vector<std::wstring> lines = SplitCodeLines(component->code);
    Json members = Json::ArrayValue();
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = Trim(lines[i]);
        std::wstring lower = ToLower(line);
        std::string scope;
        std::string type;
        size_t nameStart = std::wstring::npos;
        if (lower.compare(0, 11, L"public sub ") == 0) { scope = "public"; type = "method"; nameStart = 11; }
        else if (lower.compare(0, 12, L"private sub ") == 0) { scope = "private"; type = "method"; nameStart = 12; }
        else if (lower.compare(0, 11, L"friend sub ") == 0) { scope = "friend"; type = "method"; nameStart = 11; }
        else if (lower.compare(0, 4, L"sub ") == 0) { scope = "public"; type = "method"; nameStart = 4; }
        else if (lower.compare(0, 16, L"public function ") == 0) { scope = "public"; type = "method"; nameStart = 16; }
        else if (lower.compare(0, 17, L"private function ") == 0) { scope = "private"; type = "method"; nameStart = 17; }
        else if (lower.compare(0, 9, L"function ") == 0) { scope = "public"; type = "method"; nameStart = 9; }
        else if (lower.compare(0, 19, L"public property get") == 0) { scope = "public"; type = "property"; nameStart = 20; }
        else if (lower.compare(0, 20, L"private property get") == 0) { scope = "private"; type = "property"; nameStart = 21; }
        else continue;
        std::wstring rest = line.substr(nameStart);
        size_t end = rest.find_first_of(L" (");
        std::wstring name = end == std::wstring::npos ? rest : rest.substr(0, end);
        Json member = Json::ObjectValue();
        member.Set("name", Json::StringValue(name));
        member.Set("typeName", MakeString(type));
        member.Set("scopeName", MakeString(scope));
        member.Set("codeLocation", MakeNumber((double)(i + 1)));
        member.Set("static", MakeBool(false));
        members.push_back(member);
    }
    long declarationLines = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring lower = ToLower(Trim(lines[i]));
        if (!lower.empty() && lower.compare(0, 6, L"option") != 0 &&
            lower.compare(0, 7, L"declare") != 0 &&
            lower.compare(0, 4, L"dim ") != 0 &&
            lower.compare(0, 7, L"const ") != 0) {
            break;
        }
        ++declarationLines;
    }
    Json result = Json::ObjectValue();
    result.Set("component", DescribeComponent(*component, false));
    result.Set("lineCount", MakeNumber((double)lines.size()));
    result.Set("declarationLineCount", MakeNumber((double)declarationLines));
    result.Set("membersAvailable", MakeBool(true));
    result.Set("count", MakeNumber((double)members.size()));
    result.Set("members", members);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodePanesListTool(const Json& args) {
    (void)args;
    Project* project = FindProject("");
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Json panes = Json::ArrayValue();
    if (!project->components.empty()) {
        Component& component = project->components.begin()->second;
        panes.push_back(MockCodePane(component.name, (long)SplitCodeLines(component.code).size()));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)panes.size()));
    result.Set("codePanes", panes);
    if (!panes.empty()) result.Set("activeCodePane", panes.at(0));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::CodePaneSetTool(const Json& args) {
    Project* project = FindProject("");
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    if (project->components.empty()) {
        return ToolResult::Failure("No component is available for a code pane.", "component_not_found");
    }
    Component* component = FindComponent(project, S(args, "pane"));
    if (!component) component = &project->components.begin()->second;
    Json pane = MockCodePane(component->name, (long)SplitCodeLines(component->code).size());
    std::string action = ToLowerAscii(S(args, "action"));
    if (action == "selection") {
        Json selection = Json::ObjectValue();
        selection.Set("startLine", MakeNumber((double)I(args, "startLine", 1)));
        selection.Set("startColumn", MakeNumber((double)I(args, "startColumn", 1)));
        selection.Set("endLine", MakeNumber((double)I(args, "endLine", I(args, "startLine", 1))));
        selection.Set("endColumn", MakeNumber((double)I(args, "endColumn", I(args, "startColumn", 1))));
        pane.Set("selection", selection);
    } else if (action == "topline") {
        pane.Set("topLine", MakeNumber((double)I(args, "topLine", 1)));
    } else if (action != "focus" && action != "show") {
        return ToolResult::Failure("Unknown code pane action.", "invalid_arguments");
    }
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    result.Set("codePane", pane);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::WindowsListTool(const Json& args) {
    (void)args;
    Json windows = Json::ArrayValue();
    windows.push_back(MockWindow("Microsoft Visual Basic", "main", true));
    Project* project = FindProject("");
    if (project && !project->components.empty()) {
        windows.push_back(MockWindow(project->components.begin()->second.name + " [Code]", "code", true));
    }
    Json result = Json::ObjectValue();
    result.Set("count", MakeNumber((double)windows.size()));
    result.Set("windows", windows);
    result.Set("activeWindow", windows.at(windows.size() - 1));
    result.Set("mainWindow", windows.at(0));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::WindowActionTool(const Json& args) {
    std::string selector = S(args, "window", "Microsoft Visual Basic");
    Json window = MockWindow(selector.empty() ? "Microsoft Visual Basic" : selector, "window", true);
    std::string action = ToLowerAscii(S(args, "action"));
    if (action == "hide") window.Set("visible", MakeBool(false));
    else if (action == "show" || action == "focus") window.Set("visible", MakeBool(true));
    else if (action == "move") {
        if (args.Has("left")) window.Set("left", MakeNumber((double)I(args, "left", 0)));
        if (args.Has("top")) window.Set("top", MakeNumber((double)I(args, "top", 0)));
        if (args.Has("width")) window.Set("width", MakeNumber((double)I(args, "width", 0)));
        if (args.Has("height")) window.Set("height", MakeNumber((double)I(args, "height", 0)));
    } else if (action == "state") {
        std::string state = S(args, "state", "normal");
        long stateValue = state == "maximize" ? 2 : (state == "minimize" ? 1 : 0);
        window.Set("windowState", MakeNumber((double)stateValue));
        window.Set("windowStateName", MakeString(state));
    } else if (action == "kind") {
        window.Set("type", MakeNumber((double)I(args, "kind", 0)));
        window.Set("typeName", MakeString("kind"));
    } else if (action == "close" || action == "detach") {
        Json result = Json::ObjectValue();
        result.Set("action", MakeString(action));
        result.Set("before", window);
        result.Set(action == "close" ? "closed" : "detached", MakeBool(true));
        return ToolResult::Success(result);
    } else {
        return ToolResult::Failure("Unknown window action.", "invalid_arguments");
    }
    Json result = Json::ObjectValue();
    result.Set("action", MakeString(action));
    result.Set("before", window);
    result.Set("window", window);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::FormGetTextTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    Json result = DescribeComponent(*component, false);
    result.Set("text", Json::StringValue(component->fileText));
    result.Set("encoding", MakeString("ansi"));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::FormSetTextTool(const Json& args) {
    return ComponentReplaceTool(args);
}

ToolResult MockIdeBackend::FormListControlsTool(const Json& args) {
    ToolResult text = FormGetTextTool(args);
    if (!text.ok) return text;
    std::vector<FormControlInfo> controls;
    std::wstring error;
    if (!ListFormControls(text.data.Get("text").AsWString(), controls, error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_parse_error");
    }
    Json list = Json::ArrayValue();
    for (size_t i = 0; i < controls.size(); ++i) {
        Json item = Json::ObjectValue();
        item.Set("type", MakeString(controls[i].type));
        item.Set("name", MakeString(controls[i].name));
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
    result.Set("controls", list);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::FormAddControlTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::vector<std::pair<std::string, Json> > properties;
    const Json& props = args.Get("properties");
    if (props.IsObject()) {
        for (std::map<std::string, Json>::const_iterator it = props.object().begin(); it != props.object().end(); ++it) {
            properties.push_back(std::make_pair(it->first, it->second));
        }
    }
    std::wstring error;
    if (!AddFormControl(component->fileText, S(args, "type"), S(args, "name"),
                        S(args, "container"), properties, args.Get("index"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    return ToolResult::Success(DescribeComponent(*component, false));
}

ToolResult MockIdeBackend::FormRemoveControlTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::wstring error;
    if (!RemoveFormControl(component->fileText, S(args, "name"), args.Get("index"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    return ToolResult::Success(DescribeComponent(*component, false));
}

ToolResult MockIdeBackend::FormSetPropertyTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Component* component = FindComponent(project, S(args, "component"));
    if (!component) return ToolResult::Failure("Component not found.", "component_not_found");
    std::wstring error;
    if (!SetFormControlProperty(component->fileText, S(args, "name"), args.Get("index"),
                                S(args, "property"), args.Get("value"), error)) {
        return ToolResult::Failure(WideToUtf8(error), "form_edit_failed");
    }
    return ToolResult::Success(DescribeComponent(*component, false));
}

std::vector<std::wstring> MockIdeBackend::AllowedRoots() {
    std::vector<std::wstring> roots = config_.allowedRoots;
    for (std::map<std::string, Project>::iterator it = projects_.begin(); it != projects_.end(); ++it) {
        if (!it->second.vbpPath.empty()) roots.push_back(PathDirectory(it->second.vbpPath));
    }
    return roots;
}

bool MockIdeBackend::IsAllowed(const std::wstring& path) {
    std::wstring full = GetFullPath(path);
    std::vector<std::wstring> roots = AllowedRoots();
    for (size_t i = 0; i < roots.size(); ++i) {
        if (IsPathWithin(full, roots[i])) return true;
    }
    return false;
}

ToolResult MockIdeBackend::SourceReadTool(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(S(args, "path")));
    if (!IsAllowed(path)) return ToolResult::Failure("Path not allowed.", "path_not_allowed");
    std::wstring text;
    std::wstring encoding;
    std::wstring requestedEncoding = Utf8ToWide(S(args, "encoding"));
    if (requestedEncoding.empty() || IEquals(requestedEncoding, L"auto")) {
        if (!ReadTextFile(path, text, &encoding)) return ToolResult::Failure("Cannot read file.", "file_not_found");
    } else {
        if (!ReadTextFileAs(path, requestedEncoding, text)) {
            return ToolResult::Failure("Cannot read file with requested encoding.", "file_not_found");
        }
        encoding = ToLower(requestedEncoding);
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(path));
    result.Set("encoding", Json::StringValue(encoding));
    result.Set("size", MakeNumber((double)FileSize(path)));
    result.Set("content", Json::StringValue(text));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::SourceWriteTool(const Json& args) {
    std::wstring path = GetFullPath(Utf8ToWide(S(args, "path")));
    if (!IsAllowed(path)) return ToolResult::Failure("Path not allowed.", "path_not_allowed");
    if (B(args, "createDirectories", true)) EnsureDirectory(PathDirectory(path));
    std::wstring encoding = Utf8ToWide(S(args, "encoding"));
    if (encoding.empty() || IEquals(encoding, L"auto")) encoding = config_.sourceEncoding;
    if (!WriteTextFileAtomic(path, W(args, "content"), encoding)) {
        return ToolResult::Failure("Cannot write file.", "filesystem_error");
    }
    Json result = Json::ObjectValue();
    result.Set("path", Json::StringValue(path));
    result.Set("size", MakeNumber((double)FileSize(path)));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::SourceEditTool(const Json& args) {
    ToolResult read = SourceReadTool(args);
    if (!read.ok) return read;
    std::string source = read.data.Get("content").AsString();
    std::string updated = ReplaceSimple(source, S(args, "find"), S(args, "replace"),
                                       B(args, "all", true), B(args, "matchCase", false));
    Json writeArgs = Json::ObjectValue();
    writeArgs.Set("path", args.Get("path"));
    writeArgs.Set("content", Json::StringValue(updated));
    writeArgs.Set("encoding", read.data.Get("encoding"));
    ToolResult write = SourceWriteTool(writeArgs);
    if (!write.ok) return write;
    Json result = Json::ObjectValue();
    result.Set("replaced", MakeBool(updated != source));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::SourceListTool(const Json& args) {
    std::wstring directory = GetFullPath(Utf8ToWide(S(args, "directory")));
    if (!IsAllowed(directory)) return ToolResult::Failure("Directory not allowed.", "path_not_allowed");
    std::wstring pattern = Utf8ToWide(S(args, "pattern", "*"));
    bool recursive = B(args, "recursive", false);
    std::vector<std::wstring> queue;
    queue.push_back(directory);
    Json files = Json::ArrayValue();
    int count = 0;
    for (size_t q = 0; q < queue.size() && count < 5000; ++q) {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(PathJoin(queue[q], L"*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            std::wstring path = PathJoin(queue[q], data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                    recursive && IsAllowed(path)) {
                    queue.push_back(path);
                }
                continue;
            }
            if (!WildcardMatch(pattern, data.cFileName)) continue;
            Json file = Json::ObjectValue();
            file.Set("path", Json::StringValue(path));
            file.Set("size", MakeNumber((double)(((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow)));
            files.push_back(file);
            ++count;
        } while (FindNextFileW(find, &data) && count < 5000);
        FindClose(find);
    }
    Json result = Json::ObjectValue();
    result.Set("files", files);
    result.Set("count", MakeNumber((double)count));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ReferenceListTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    Json list = Json::ArrayValue();
    for (size_t i = 0; i < project->references.size(); ++i) {
        Json item = Json::ObjectValue();
        item.Set("name", MakeString(project->references[i]));
        list.push_back(item);
    }
    Json result = Json::ObjectValue();
    result.Set("references", list);
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ReferenceAddTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string reference = S(args, "path");
    if (reference.empty()) reference = S(args, "guid");
    if (reference.empty()) return ToolResult::Failure("path or guid is required.", "invalid_arguments");
    project->references.push_back(reference);
    Json result = Json::ObjectValue();
    result.Set("added", MakeBool(true));
    return ToolResult::Success(result);
}

ToolResult MockIdeBackend::ReferenceRemoveTool(const Json& args) {
    Project* project = FindProject(S(args, "project"));
    if (!project) return ToolResult::Failure("Project not found.", "project_not_found");
    std::string wanted = S(args, "reference");
    for (std::vector<std::string>::iterator it = project->references.begin(); it != project->references.end(); ++it) {
        if (IEquals(*it, wanted)) {
            project->references.erase(it);
            Json result = Json::ObjectValue();
            result.Set("removed", MakeBool(true));
            return ToolResult::Success(result);
        }
    }
    return ToolResult::Failure("Reference not found.", "reference_not_found");
}

ToolResult MockIdeBackend::Vb6BatchTool(const Json& args) {
    const Json& calls = args.Get("calls");
    if (!calls.IsArray() || calls.array().empty()) {
        return ToolResult::Failure("calls array is required.", "invalid_arguments");
    }
    int maxCalls = I(args, "maxCalls", 50);
    if (maxCalls < 1) maxCalls = 1;
    if (maxCalls > 200) maxCalls = 200;
    bool stopOnError = B(args, "stopOnError", true);
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
        std::string toolName = S(call, "tool");
        item.Set("tool", MakeString(toolName));
        if (toolName.empty() || toolName == "vb6_batch" || !IsKnownTool(toolName)) {
            item.Set("ok", MakeBool(false));
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
        ToolResult one = Dispatch(toolName, callArgs);
        item.Set("ok", MakeBool(one.ok));
        item.Set("result", one.data);
        if (one.ok) {
            ++successCount;
        } else {
            item.Set("code", MakeString(one.errorCode));
            ++errorCount;
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
        ToolResult failure = ToolResult::Failure("Batch failed.", "batch_failed");
        failure.data = result;
        return failure;
    }
    return ToolResult::Success(result);
}

} // namespace mcpaddin
