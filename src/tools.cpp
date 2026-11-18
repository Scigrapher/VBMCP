#include "tools.h"

#include <set>

namespace mcpaddin {

Json MakeObject() {
    return Json::ObjectValue();
}

Json MakeArray() {
    return Json::ArrayValue();
}

Json MakeString(const char* value) {
    return Json::StringValue(value ? value : "");
}

Json MakeString(const std::string& value) {
    return Json::StringValue(value);
}

Json MakeNumber(double value) {
    return Json::NumberValue(value);
}

Json MakeBool(bool value) {
    return Json::BoolValue(value);
}

Json SchemaString(const std::string& description, const std::vector<std::string>& enumValues) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("string"));
    schema.Set("description", MakeString(description));
    if (!enumValues.empty()) {
        Json values = Json::ArrayValue();
        for (size_t i = 0; i < enumValues.size(); ++i) values.push_back(MakeString(enumValues[i]));
        schema.Set("enum", values);
    }
    return schema;
}

Json SchemaBoolean(const std::string& description) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("boolean"));
    schema.Set("description", MakeString(description));
    return schema;
}

Json SchemaInteger(const std::string& description, const Json& minimum, const Json& maximum) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("integer"));
    schema.Set("description", MakeString(description));
    if (!minimum.IsNull()) schema.Set("minimum", minimum);
    if (!maximum.IsNull()) schema.Set("maximum", maximum);
    return schema;
}

Json SchemaObjectMap(const std::string& description) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("object"));
    schema.Set("description", MakeString(description));
    schema.Set("additionalProperties", MakeBool(true));
    return schema;
}

Json SchemaArrayOfStrings(const std::string& description) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("array"));
    schema.Set("description", MakeString(description));
    schema.Set("items", SchemaString("Value."));
    return schema;
}

Json SchemaArrayOfObjects(const std::string& description, const Json& itemSchema) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("array"));
    schema.Set("description", MakeString(description));
    schema.Set("items", itemSchema);
    return schema;
}

Json SchemaObject(const std::map<std::string, Json>& properties,
                  const std::vector<std::string>& required) {
    Json schema = Json::ObjectValue();
    schema.Set("type", MakeString("object"));
    Json props = Json::ObjectValue();
    for (std::map<std::string, Json>::const_iterator it = properties.begin(); it != properties.end(); ++it) {
        props.Set(it->first, it->second);
    }
    schema.Set("properties", props);
    schema.Set("additionalProperties", MakeBool(false));
    if (!required.empty()) {
        Json req = Json::ArrayValue();
        for (size_t i = 0; i < required.size(); ++i) req.push_back(MakeString(required[i]));
        schema.Set("required", req);
    }
    return schema;
}

static Json Tool(const std::string& name, const std::string& title,
                 const std::string& description, const Json& schema,
                 const std::vector<std::string>& tags = std::vector<std::string>()) {
    Json tool = Json::ObjectValue();
    tool.Set("name", MakeString(name));
    tool.Set("title", MakeString(title));
    tool.Set("description", MakeString(description));
    tool.Set("inputSchema", schema);
    if (!tags.empty()) {
        Json annotations = Json::ObjectValue();
        bool readOnly = false;
        for (size_t i = 0; i < tags.size(); ++i) {
            if (tags[i] == "read") readOnly = true;
        }
        annotations.Set("readOnlyHint", MakeBool(readOnly));
        annotations.Set("destructiveHint", MakeBool(!readOnly));
        annotations.Set("idempotentHint", MakeBool(readOnly));
        annotations.Set("openWorldHint", MakeBool(false));
        tool.Set("annotations", annotations);
    }
    return tool;
}

static Json EmptySchema() {
    std::map<std::string, Json> props;
    return SchemaObject(props, std::vector<std::string>());
}

static std::map<std::string, Json> P() {
    return std::map<std::string, Json>();
}

static void Add(std::map<std::string, Json>& props, const std::string& name, const Json& schema) {
    props[name] = schema;
}

static std::vector<std::string> R(const char* a = NULL, const char* b = NULL,
                                  const char* c = NULL, const char* d = NULL,
                                  const char* e = NULL) {
    std::vector<std::string> result;
    if (a) result.push_back(a);
    if (b) result.push_back(b);
    if (c) result.push_back(c);
    if (d) result.push_back(d);
    if (e) result.push_back(e);
    return result;
}

static Json ControlSpecSchema() {
    std::map<std::string, Json> p = P();
    Add(p, "type", SchemaString("Control type or ProgID, such as VB.CommandButton, textbox, label, frame, or MSComDlg.CommonDialog."));
    Add(p, "name", SchemaString("Unique VB6 control name."));
    Add(p, "container", SchemaString("Optional parent control name for nested controls."));
    Add(p, "index", SchemaInteger("Optional control-array index.", MakeNumber(0)));
    Add(p, "properties", SchemaObjectMap("Designer properties. Numbers and booleans are emitted unquoted; strings are quoted."));
    return SchemaObject(p, R("type", "name"));
}

static Json ComponentSpecSchema() {
    std::map<std::string, Json> p = P();
    Add(p, "kind", SchemaString("Component kind.", {"form", "class", "module", "usercontrol", "propertypage"}));
    Add(p, "name", SchemaString("Unique VB6 component name."));
    Add(p, "fileName", SchemaString("Optional component file name, relative to the project directory."));
    Add(p, "title", SchemaString("Optional form caption when source is omitted."));
    Add(p, "source", SchemaString("Optional complete component source text. When omitted, a valid VB6 skeleton is generated."));
    Add(p, "code", SchemaString("Code appended after the generated/source component header. Use VB6 source syntax."));
    Add(p, "controls", SchemaArrayOfObjects("Optional controls to add to a form after its source is generated. Applied in order.",
                                            ControlSpecSchema()));
    return SchemaObject(p, R("kind", "name"));
}

static Json ReferenceSpecSchema() {
    std::map<std::string, Json> p = P();
    Add(p, "path", SchemaString("Type-library or ActiveX file path."));
    Add(p, "guid", SchemaString("Type-library GUID, when adding by registered GUID."));
    Add(p, "major", SchemaInteger("Type-library major version.", MakeNumber(0)));
    Add(p, "minor", SchemaInteger("Type-library minor version.", MakeNumber(0)));
    return SchemaObject(p, std::vector<std::string>());
}

Json GetToolDefinitions() {
    Json tools = Json::ArrayValue();

    {
        std::map<std::string, Json> p = P();
        Add(p, "calls", SchemaArrayOfObjects(
                "Sequential tool calls. Each item is {\"tool\":\"tool_name\",\"arguments\":{...}}.",
                [] {
                    std::map<std::string, Json> item = P();
                    Add(item, "tool", SchemaString("Registered MCP tool name. vb6_batch cannot call itself."));
                    Add(item, "arguments", SchemaObjectMap("Arguments object passed to that tool."));
                    return SchemaObject(item, R("tool"));
                }()));
        Add(p, "stopOnError", SchemaBoolean("Stop after the first failed call. Default true."));
        Add(p, "maxCalls", SchemaInteger("Maximum calls to execute, 1-200. Default 50.",
                                         MakeNumber(1), MakeNumber(200)));
        tools.push_back(Tool("vb6_batch", "Batch VB6 operations",
            "Run several MCP tool calls sequentially in one request. Use for multi-control form builds, edit/read verification loops, and multi-component code generation. Calls run in order on the VB6 UI thread; there is no transaction rollback.",
            SchemaObject(p, R("calls")), R("write", "orchestration")));
    }
    tools.push_back(Tool("ide_status", "IDE status",
        "Return VB6 IDE state, active project/component/code pane/window, add-in discovery, server endpoints, configuration location, tool groups, and the recommended AI workflow. Call this first in a new session.",
        EmptySchema(), R("read")));
    tools.push_back(Tool("project_list", "List projects",
        "List all projects currently open in the VB6 IDE, including paths, component counts, and saved state.",
        EmptySchema(), R("read")));
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or 1-based index (`1` or `#1`). Defaults to the active project."));
        tools.push_back(Tool("project_get", "Get project",
            "Return detailed information about one open project, its components, and references.",
            SchemaObject(p, std::vector<std::string>()), R("read")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "includeMembers", SchemaBoolean("Include code member/procedure summaries. Default true."));
        Add(p, "includeControls", SchemaBoolean("Include parsed form controls for designer components. Default true."));
        tools.push_back(Tool("project_inspect", "Inspect project",
            "Return a project-wide map for AI comprehension: component kinds, file names, code member summaries, form controls, references, and build settings. Prefer this over dumping every component's full code.",
            SchemaObject(p, std::vector<std::string>()), R("read", "project", "code", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "strict", SchemaBoolean("Treat warnings as validation failures. Default false."));
        Add(p, "checkReferences", SchemaBoolean("Check for broken references. Default true."));
        tools.push_back(Tool("project_validate", "Validate project",
            "Check project-file structure, component files, VB_Name attributes, Option Explicit, form designer syntax, duplicate controls, startup resolution, and broken references. This is a fast structural check; use project_build with commandline for compiler errors.",
            SchemaObject(p, std::vector<std::string>()), R("read", "project", "validation")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "directory", SchemaString("Directory in which to create the project. Use this or vbpPath."));
        Add(p, "vbpPath", SchemaString("Exact .vbp path to create. Use this or directory."));
        Add(p, "name", SchemaString("VB6 project name. Defaults to the file stem."));
        Add(p, "title", SchemaString("Display title and version description."));
        Add(p, "description", SchemaString("Project description."));
        Add(p, "company", SchemaString("Version company name."));
        Add(p, "product", SchemaString("Version product name."));
        Add(p, "type", SchemaString("Project kind.", {"standard_exe", "activex_exe", "activex_dll", "activex_control"}));
        Add(p, "overwrite", SchemaBoolean("Overwrite existing project and component files. Default false."));
        Add(p, "open", SchemaBoolean("Open the generated project in the running VB6 IDE. Default true."));
        Add(p, "startup", SchemaString("Startup object: a form name or the literal Sub Main. Defaults to Sub Main when a generated startup module exists."));
        Add(p, "startupModule", SchemaBoolean("When components is omitted, create Module1 with Sub Main for standard EXE projects. Default true."));
        Add(p, "components", SchemaArrayOfObjects(
                "Complete component manifest. Each item is a module, class, form, user control, or property page. Form controls may be supplied inline.",
                ComponentSpecSchema()));
        Add(p, "references", SchemaArrayOfObjects(
                "References to add after the project is opened. Requires open=true.",
                ReferenceSpecSchema()));
        Add(p, "toolboxProgIds", SchemaArrayOfStrings(
                "Optional ActiveX ProgIDs to register in the VB6 toolbox after opening."));
        Add(p, "build", SchemaBoolean("Build after creation. Default false; command-line build is recommended for unattended work."));
        Add(p, "buildMethod", SchemaString("Build method when build=true.", {"ide", "commandline"}));
        Add(p, "buildOutputPath", SchemaString("Optional build output path when build=true."));
        Add(p, "buildTimeoutSeconds", SchemaInteger("Build timeout when build=true.",
                                                   MakeNumber(5), MakeNumber(3600)));
        tools.push_back(Tool("project_scaffold", "Scaffold complete app",
            "PREFERRED bootstrap for new VB6 applications. Create a complete on-disk project from a declarative component manifest, add form controls and references, open it in the IDE, and optionally build it in one call. Use project_create only for a minimal empty project.",
            SchemaObject(p, std::vector<std::string>()), R("write", "project", "component", "orchestration")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "directory", SchemaString("Directory in which to create the project. Use this or vbpPath."));
        Add(p, "vbpPath", SchemaString("Exact .vbp path to create. Use this or directory."));
        Add(p, "name", SchemaString("VB6 project name. Defaults to Project1 or the VBP file stem."));
        Add(p, "title", SchemaString("Display title."));
        Add(p, "description", SchemaString("Project description."));
        Add(p, "company", SchemaString("Version company name."));
        Add(p, "product", SchemaString("Version product name."));
        Add(p, "type", SchemaString("Project kind.", {"standard_exe", "activex_exe", "activex_dll", "activex_control"}));
        Add(p, "overwrite", SchemaBoolean("Overwrite an existing VBP and component files. Default false."));
        Add(p, "open", SchemaBoolean("Open the generated project in the running VB6 IDE. Default true."));
        Add(p, "startupModule", SchemaBoolean("Create a module with Sub Main for standard EXE projects. Default true."));
        tools.push_back(Tool("project_create", "Create project",
            "Generate a complete on-disk VB6 project and optionally open it in the running IDE. The generated project is code-only by default and uses a Sub Main startup module.",
            SchemaObject(p, R("type")), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "path", SchemaString("Path to a .vbp project file."));
        tools.push_back(Tool("project_open", "Open project",
            "Open a VB6 .vbp project in the running IDE.",
            SchemaObject(p, R("path")), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "save", SchemaBoolean("Save before closing. Default true."));
        tools.push_back(Tool("project_close", "Close project",
            "Close a project in the running IDE.",
            SchemaObject(p, std::vector<std::string>()), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "path", SchemaString("Optional new .vbp path for SaveAs."));
        tools.push_back(Tool("project_save", "Save project",
            "Save the project and its components, optionally using SaveAs.",
            SchemaObject(p, std::vector<std::string>()), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index."));
        Add(p, "name", SchemaString("New project name."));
        Add(p, "title", SchemaString("Project title."));
        Add(p, "description", SchemaString("Project description."));
        Add(p, "helpFile", SchemaString("Help file path."));
        Add(p, "helpContextId", SchemaInteger("Help context ID.", MakeNumber(0), MakeNumber(2147483647)));
        Add(p, "buildFileName", SchemaString("Build output path."));
        Add(p, "compatibleOleServer", SchemaString("Compatible ActiveX OLE server path."));
        Add(p, "startMode", SchemaString("Project start mode.", {"standalone", "ole_server"}));
        tools.push_back(Tool("project_set_properties", "Set project properties",
            "Set project-level properties supported by the VBIDE automation model.",
            SchemaObject(p, std::vector<std::string>()), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "section", SchemaString("VBP section name, for example Project or MSComctlLib."));
        Add(p, "key", SchemaString("Property key within the VBP section."));
        tools.push_back(Tool("project_read_property", "Read VBP property",
            "Read a project property through VBProject.ReadProperty using a VBP section and key.",
            SchemaObject(p, R("section", "key")), R("read", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "section", SchemaString("VBP section name, for example Project or MSComctlLib."));
        Add(p, "key", SchemaString("Property key within the VBP section."));
        Add(p, "value", SchemaString("Property value to write."));
        tools.push_back(Tool("project_write_property", "Write VBP property",
            "Write a project property through VBProject.WriteProperty using a VBP section and key.",
            SchemaObject(p, R("section", "key", "value")), R("write", "project")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "progId", SchemaString("ActiveX control ProgID to add to the IDE toolbox."));
        Add(p, "fileName", SchemaString("Optional control library path."));
        tools.push_back(Tool("project_add_toolbox_progid", "Add toolbox control",
            "Add an ActiveX control ProgID to the VB6 toolbox through VBProject.AddToolboxProgID.",
            SchemaObject(p, R("progId")), R("write", "ide")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "outputPath", SchemaString("Optional explicit build output path."));
        Add(p, "method", SchemaString("Build method.", {"ide", "commandline"}));
        Add(p, "outputDirectory", SchemaString("Optional output directory for command-line builds (/outdir)."));
        Add(p, "timeoutSeconds", SchemaInteger("Build timeout.", MakeNumber(5), MakeNumber(3600)));
        Add(p, "logPath", SchemaString("Compiler log path for command-line builds."));
        Add(p, "vb6Exe", SchemaString("Optional VB6.EXE path override for command-line builds."));
        tools.push_back(Tool("project_build", "Build project",
            "Build a VB6 project using the live IDE's MakeCompiledFile method or VB6.EXE /make. Returns output existence, size, timestamp, and compiler log when available.",
            SchemaObject(p, std::vector<std::string>()), R("write", "build")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "kind", SchemaString("Component kind.", {"form", "class", "module", "usercontrol", "propertypage", "designer", "custom"}));
        Add(p, "name", SchemaString("Component name."));
        Add(p, "progId", SchemaString("Designer ProgID for AddCustom, such as VB.UserControl."));
        Add(p, "code", SchemaString("Optional initial code for class/module components."));
        tools.push_back(Tool("project_add_component", "Add component",
            "Add a new component to the project using VBComponents.Add or AddCustom.",
            SchemaObject(p, R("kind")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "name", SchemaString("Form/component name."));
        Add(p, "fileName", SchemaString("Optional .frm filename. Relative paths are resolved inside the project directory."));
        Add(p, "title", SchemaString("Default form caption when source is omitted."));
        Add(p, "source", SchemaString("Optional complete .frm text. When omitted, a default VB6 form is generated."));
        Add(p, "code", SchemaString("Optional code appended to the generated form."));
        Add(p, "overwrite", SchemaBoolean("Overwrite an existing .frm file. Default false."));
        Add(p, "startup", SchemaBoolean("Set this form as the project startup object in the VBP."));
        tools.push_back(Tool("project_add_form", "Add form file",
            "VB6-compatible form creation. Writes the .frm and updates the VBP directly instead of relying on VBComponents.Import, which retail VB6 does not expose. Reload the project after this call.",
            SchemaObject(p, R("name")), R("write", "project", "component", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "path", SchemaString("Existing .frm, .cls, .bas, .ctl, .pag, or .dsr file to import."));
        tools.push_back(Tool("project_add_file", "Import component file",
            "Import an existing VB6 component file into the project.",
            SchemaObject(p, R("path")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "kind", SchemaString("Component kind.", {"form", "class", "module", "usercontrol", "propertypage"}));
        Add(p, "name", SchemaString("Component name."));
        Add(p, "fileName", SchemaString("Optional file name or path. Relative paths are resolved inside the project directory."));
        Add(p, "source", SchemaString("Optional complete VB6 source file text."));
        Add(p, "code", SchemaString("Optional code section text used when source is omitted."));
        Add(p, "import", SchemaBoolean("Import the generated file after writing it. Default true."));
        Add(p, "overwrite", SchemaBoolean("Overwrite an existing file. Default false."));
        tools.push_back(Tool("project_create_file", "Create component file",
            "Write a new VB6 component source file into the project directory and optionally import it.",
            SchemaObject(p, R("kind", "name")), R("write", "component", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "saveFile", SchemaBoolean("Export the component before removing it. Default false."));
        Add(p, "exportPath", SchemaString("Export destination when saveFile is true."));
        tools.push_back(Tool("project_remove_component", "Remove component",
            "Remove a component from the project.",
            SchemaObject(p, R("component")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        tools.push_back(Tool("component_list", "List components",
            "List project components with type, saved state, designer availability, and containing project.",
            SchemaObject(p, std::vector<std::string>()), R("read")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "includeCode", SchemaBoolean("Include the component code module text. Default true."));
        tools.push_back(Tool("component_get", "Get component",
            "Return component metadata and optionally its complete code module text.",
            SchemaObject(p, R("component")), R("read")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        tools.push_back(Tool("component_get_code", "Get code",
            "Read a component's complete code module text through CodeModule.",
            SchemaObject(p, R("component")), R("read")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "code", SchemaString("Complete replacement code text."));
        tools.push_back(Tool("component_set_code", "Set code",
            "Replace a component's complete code module text.",
            SchemaObject(p, R("component", "code")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "find", SchemaString("Text to find."));
        Add(p, "replace", SchemaString("Replacement text."));
        Add(p, "all", SchemaBoolean("Replace all matches. Default true."));
        Add(p, "matchCase", SchemaBoolean("Case-sensitive replacement. Default false."));
        tools.push_back(Tool("component_edit_code", "Edit code",
            "Find and replace text inside a component code module.",
            SchemaObject(p, R("component", "find", "replace")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "startLine", SchemaInteger("First 1-based line to read. Default 1.", MakeNumber(1)));
        Add(p, "count", SchemaInteger("Number of lines to read. Omit or use 0 for the rest of the module.", MakeNumber(0)));
        tools.push_back(Tool("code_get_lines", "Get code lines",
            "Read a line range from a component's CodeModule and report the module line count.",
            SchemaObject(p, R("component")), R("read", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "action", SchemaString("Line edit operation.", {"insert", "replace", "delete", "append"}));
        Add(p, "line", SchemaInteger("1-based line for insert/replace/delete.", MakeNumber(1)));
        Add(p, "count", SchemaInteger("Number of lines to delete. Default 1.", MakeNumber(1)));
        Add(p, "text", SchemaString("Text to insert, replace, or append."));
        tools.push_back(Tool("code_lines_edit", "Edit code lines",
            "Insert, replace, delete, or append lines through CodeModule.InsertLines, ReplaceLine, DeleteLines, or AddFromString.",
            SchemaObject(p, R("component", "action")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "target", SchemaString("Text or pattern to find."));
        Add(p, "startLine", SchemaInteger("Starting 1-based line. Default 1.", MakeNumber(1)));
        Add(p, "startColumn", SchemaInteger("Starting 1-based column. Default 1.", MakeNumber(1)));
        Add(p, "wholeWord", SchemaBoolean("Match whole words only. Default false."));
        Add(p, "matchCase", SchemaBoolean("Use case-sensitive matching. Default false."));
        Add(p, "patternSearch", SchemaBoolean("Treat target as a VBIDE pattern. Default false."));
        tools.push_back(Tool("code_find", "Find code",
            "Search a component with CodeModule.Find and return the matched start/end positions.",
            SchemaObject(p, R("component", "target")), R("read", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "query", SchemaString("Plain-text search string."));
        Add(p, "component", SchemaString("Optional component name filter."));
        Add(p, "matchCase", SchemaBoolean("Use case-sensitive matching. Default false."));
        Add(p, "wholeWord", SchemaBoolean("Require VB identifier boundaries around each match. Default false."));
        Add(p, "maxResults", SchemaInteger("Maximum matches to return, 1-1000. Default 200.",
                                           MakeNumber(1), MakeNumber(1000)));
        tools.push_back(Tool("code_search", "Search project code",
            "Search code modules across the whole project and return component, line, column, and preview text. Designer/header text is not searched; use project_inspect or form_list_controls for form metadata.",
            SchemaObject(p, R("query")), R("read", "code")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "eventName", SchemaString("Event name, for example Click."));
        Add(p, "objectName", SchemaString("Object name, for example Command1."));
        tools.push_back(Tool("code_create_event", "Create event procedure",
            "Create an event procedure with CodeModule.CreateEventProc and return its line number.",
            SchemaObject(p, R("component", "eventName", "objectName")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        tools.push_back(Tool("code_structure", "Inspect code structure",
            "List CodeModule members with type, scope, static flag, and code location, plus declaration and total line counts.",
            SchemaObject(p, R("component")), R("read", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Current component name."));
        Add(p, "newName", SchemaString("New VB6 identifier."));
        tools.push_back(Tool("component_rename", "Rename component",
            "Rename a component in the project.",
            SchemaObject(p, R("component", "newName")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "path", SchemaString("Export destination path."));
        tools.push_back(Tool("component_export", "Export component",
            "Export a component to a VB6 source file.",
            SchemaObject(p, R("component", "path")), R("write", "component", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "text", SchemaString("Complete replacement component file text, including designer block for forms."));
        tools.push_back(Tool("component_replace_file", "Replace component file",
            "Export, rewrite, and re-import a component file. Useful for changing designer text or code that is not exposed by CodeModule.",
            SchemaObject(p, R("component", "text")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "includeValues", SchemaBoolean("Read scalar property values. Default true."));
        tools.push_back(Tool("component_properties_list", "List component properties",
            "Enumerate the VBComponent.Properties collection with names, index counts, and optional scalar values.",
            SchemaObject(p, R("component")), R("read", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "key", SchemaString("Property key to read through VBComponent.ReadProperty."));
        tools.push_back(Tool("component_read_property", "Read component property",
            "Read a component property-bag value through VBComponent.ReadProperty.",
            SchemaObject(p, R("component", "key")), R("read", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "key", SchemaString("Property key to write through VBComponent.WriteProperty."));
        Add(p, "value", SchemaString("String value to write."));
        tools.push_back(Tool("component_write_property", "Write component property",
            "Write a component property-bag value through VBComponent.WriteProperty.",
            SchemaObject(p, R("component", "key", "value")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Component name."));
        Add(p, "action", SchemaString("Component action.", {"activate", "reload", "saveAs", "designerWindow"}));
        Add(p, "path", SchemaString("Destination path for saveAs."));
        tools.push_back(Tool("component_action", "Run component action",
            "Activate or reload a component, save it under a new file name, or return its open designer window.",
            SchemaObject(p, R("component", "action")), R("write", "component")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        tools.push_back(Tool("form_get_text", "Get form text",
            "Export a form and return its complete .frm designer and code text.",
            SchemaObject(p, R("component")), R("read", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        Add(p, "text", SchemaString("Complete .frm replacement text."));
        tools.push_back(Tool("form_set_text", "Set form text",
            "Replace a form by exporting it, rewriting the .frm file, and re-importing it.",
            SchemaObject(p, R("component", "text")), R("write", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        tools.push_back(Tool("form_list_controls", "List form controls",
            "Parse the form designer text and return controls, types, names, control-array indexes, and properties.",
            SchemaObject(p, R("component")), R("read", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        Add(p, "type", SchemaString("Control type or ProgID, such as VB.CommandButton, textbox, label, frame."));
        Add(p, "name", SchemaString("Control name."));
        Add(p, "container", SchemaString("Optional parent control name for nested controls."));
        Add(p, "index", SchemaInteger("Optional control-array index."));
        Add(p, "properties", SchemaObjectMap("Designer properties. JSON numbers and booleans are emitted unquoted; strings are quoted."));
        tools.push_back(Tool("form_add_control", "Add form control",
            "Add an intrinsic or registered ActiveX control to a form by safely rewriting the form designer text and re-importing it.",
            SchemaObject(p, R("component", "type", "name")), R("write", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        Add(p, "name", SchemaString("Control name."));
        Add(p, "index", SchemaInteger("Optional control-array index."));
        tools.push_back(Tool("form_remove_control", "Remove form control",
            "Remove a control block from a form by name and optional control-array index.",
            SchemaObject(p, R("component", "name")), R("write", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "component", SchemaString("Form component name."));
        Add(p, "name", SchemaString("Control name."));
        Add(p, "index", SchemaInteger("Optional control-array index."));
        Add(p, "property", SchemaString("Designer property name."));
        {
            Json any = Json::ObjectValue();
            any.Set("description", MakeString("Designer property value. Numbers and booleans are emitted unquoted; strings are quoted."));
            Add(p, "value", any);
        }
        tools.push_back(Tool("form_set_control_property", "Set control property",
            "Set or add a designer property on a form control.",
            SchemaObject(p, R("component", "name", "property", "value")), R("write", "designer")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        tools.push_back(Tool("references_list", "List references",
            "List project references with names, GUIDs, versions, paths, and broken-reference state.",
            SchemaObject(p, std::vector<std::string>()), R("read")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "path", SchemaString("Type library path to add."));
        Add(p, "guid", SchemaString("Type library GUID to add instead of path."));
        Add(p, "major", SchemaInteger("Type library major version."));
        Add(p, "minor", SchemaInteger("Type library minor version."));
        tools.push_back(Tool("reference_add", "Add reference",
            "Add a type library reference by path or GUID/version.",
            SchemaObject(p, std::vector<std::string>()), R("write")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index. Defaults to active project."));
        Add(p, "reference", SchemaString("Reference name or full path."));
        tools.push_back(Tool("reference_remove", "Remove reference",
            "Remove a project reference by name or path.",
            SchemaObject(p, R("reference")), R("write")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "project", SchemaString("Project name, VBP path, or index."));
        tools.push_back(Tool("active_project_set", "Set active project",
            "Make a project the active VBIDE project.",
            SchemaObject(p, R("project")), R("write")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "pane", SchemaString("Code pane component/module name, caption, or 1-based index. Defaults to the active code pane."));
        Add(p, "action", SchemaString("Code pane action.", {"focus", "show", "selection", "topLine"}));
        Add(p, "startLine", SchemaInteger("Selection start line.", MakeNumber(1)));
        Add(p, "startColumn", SchemaInteger("Selection start column.", MakeNumber(1)));
        Add(p, "endLine", SchemaInteger("Selection end line."));
        Add(p, "endColumn", SchemaInteger("Selection end column."));
        Add(p, "topLine", SchemaInteger("Top visible line for the topLine action.", MakeNumber(1)));
        tools.push_back(Tool("code_pane_set", "Set code pane",
            "Focus or show a code pane, set its selection, or scroll it to a top line.",
            SchemaObject(p, R("action")), R("write", "ide")));
    }
    tools.push_back(Tool("code_panes_list", "List code panes",
        "List open code panes with module/component names, window captions, visible line counts, and current selections.",
        EmptySchema(), R("read", "ide")));
    {
        std::map<std::string, Json> p = P();
        Add(p, "window", SchemaString("Window caption or 1-based index. Defaults to the active window."));
        Add(p, "action", SchemaString("Window action.", {"focus", "show", "hide", "close", "move", "state", "kind", "detach"}));
        Add(p, "left", SchemaInteger("Window left coordinate."));
        Add(p, "top", SchemaInteger("Window top coordinate."));
        Add(p, "width", SchemaInteger("Window width."));
        Add(p, "height", SchemaInteger("Window height."));
        Add(p, "state", SchemaString("Window state.", {"normal", "minimize", "maximize"}));
        Add(p, "kind", SchemaInteger("vbext_WindowType value for SetKind.", MakeNumber(0), MakeNumber(15)));
        tools.push_back(Tool("window_action", "Control IDE window",
            "Focus, show, hide, close, move, change state, set kind, or detach a VB6 IDE window.",
            SchemaObject(p, R("action")), R("write", "ide")));
    }
    tools.push_back(Tool("windows_list", "List IDE windows",
        "List VB6 IDE windows with caption, type, visibility, bounds, window state, HWND, and linked frame.",
        EmptySchema(), R("read", "ide")));
    {
        std::map<std::string, Json> p = P();
        Add(p, "command", SchemaString("CommandBar caption to execute, for example 'Make Project1.exe...'."));
        Add(p, "id", SchemaInteger("Optional CommandBar control ID."));
        tools.push_back(Tool("ide_execute_command", "Execute IDE command",
            "Execute a VB6 IDE CommandBar control by caption or numeric ID. Useful for commands not exposed by the VBIDE object model.",
            SchemaObject(p, R("command")), R("write", "ide")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "path", SchemaString("Text file path."));
        Add(p, "encoding", SchemaString("Encoding override: auto, ansi, utf8, utf16le, utf16be.", {"auto", "ansi", "utf8", "utf16le", "utf16be"}));
        tools.push_back(Tool("source_read", "Read source file",
            "Read a text or VB6 source file. Access is restricted to configured allowed roots and open project directories.",
            SchemaObject(p, R("path")), R("read", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "path", SchemaString("Text file path."));
        Add(p, "content", SchemaString("File content."));
        Add(p, "encoding", SchemaString("Encoding override: auto, ansi, utf8, utf16le, utf16be.", {"auto", "ansi", "utf8", "utf16le", "utf16be"}));
        Add(p, "createDirectories", SchemaBoolean("Create parent directories. Default true."));
        tools.push_back(Tool("source_write", "Write source file",
            "Atomically write a text or VB6 source file. Access is restricted to configured allowed roots and open project directories.",
            SchemaObject(p, R("path", "content")), R("write", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "path", SchemaString("Text file path."));
        Add(p, "find", SchemaString("Text to find."));
        Add(p, "replace", SchemaString("Replacement text."));
        Add(p, "all", SchemaBoolean("Replace all matches. Default true."));
        Add(p, "matchCase", SchemaBoolean("Case-sensitive search. Default false."));
        tools.push_back(Tool("source_edit", "Edit source file",
            "Find and replace text in a source file.",
            SchemaObject(p, R("path", "find", "replace")), R("write", "filesystem")));
    }
    {
        std::map<std::string, Json> p = P();
        Add(p, "directory", SchemaString("Directory to list."));
        Add(p, "pattern", SchemaString("Wildcard pattern. Default *."));
        Add(p, "recursive", SchemaBoolean("Recurse into subdirectories. Default false."));
        tools.push_back(Tool("source_list", "List source files",
            "List files in a project or allowed directory.",
            SchemaObject(p, R("directory")), R("read", "filesystem")));
    }
    return tools;
}

Json GetToolGroups() {
    Json groups = Json::ObjectValue();
    auto addGroup = [&groups](const char* name, const std::vector<std::string>& names) {
        Json values = Json::ArrayValue();
        for (size_t i = 0; i < names.size(); ++i) values.push_back(MakeString(names[i]));
        groups.Set(name, values);
    };
    addGroup("bootstrap", {
        "ide_status", "project_scaffold", "project_inspect", "project_validate", "vb6_batch"
    });
    addGroup("projects", {
        "project_list", "project_get", "project_create", "project_open", "project_close",
        "project_save", "project_set_properties", "project_read_property",
        "project_write_property", "project_add_toolbox_progid", "project_build",
        "active_project_set"
    });
    addGroup("components", {
        "project_add_component", "project_add_form", "project_add_file",
        "project_create_file", "project_remove_component", "component_list",
        "component_get", "component_get_code", "component_set_code",
        "component_edit_code", "component_rename", "component_export",
        "component_replace_file", "component_properties_list",
        "component_read_property", "component_write_property", "component_action"
    });
    addGroup("code", {
        "code_get_lines", "code_lines_edit", "code_find", "code_search",
        "code_create_event", "code_structure"
    });
    addGroup("forms", {
        "form_get_text", "form_set_text", "form_list_controls", "form_add_control",
        "form_remove_control", "form_set_control_property"
    });
    addGroup("ide", {
        "code_panes_list", "code_pane_set", "windows_list", "window_action",
        "ide_execute_command"
    });
    addGroup("references", {
        "references_list", "reference_add", "reference_remove"
    });
    addGroup("files", {
        "source_read", "source_write", "source_edit", "source_list"
    });
    return groups;
}

Json GetRecommendedWorkflow() {
    Json workflow = Json::ArrayValue();
    auto step = [&workflow](int order, const char* title, const char* detail,
                            const std::vector<std::string>& tools) {
        Json item = Json::ObjectValue();
        item.Set("step", MakeNumber(order));
        item.Set("title", MakeString(title));
        item.Set("detail", MakeString(detail));
        Json names = Json::ArrayValue();
        for (size_t i = 0; i < tools.size(); ++i) names.push_back(MakeString(tools[i]));
        item.Set("tools", names);
        workflow.push_back(item);
    };
    step(1, "Discover the IDE", "Call ide_status. Confirm the backend, active project, endpoints, and capabilities.", {"ide_status"});
    step(2, "Understand existing code", "Inspect the project map, then search and read only the code needed for the task.", {"project_list", "project_inspect", "code_search", "component_get_code"});
    step(3, "Bootstrap new applications", "Create the project and all initial components/forms in one declarative call.", {"project_scaffold"});
    step(4, "Implement behavior", "Edit code by component, create event procedures, and add form controls. Use vb6_batch for repeated independent edits.", {"component_set_code", "code_lines_edit", "code_create_event", "form_add_control", "vb6_batch"});
    step(5, "Check structure", "Validate VBP/component files, names, startup, forms, and references before compiling.", {"project_validate"});
    step(6, "Compile with diagnostics", "Use commandline builds for unattended work so compiler output is captured without a modal IDE dialog.", {"project_build"});
    step(7, "Verify output", "Check success, outputExists, compilerOutput, and then inspect the final project if more changes are needed.", {"project_build", "project_inspect"});
    return workflow;
}

bool IsKnownTool(const std::string& name) {
    static std::set<std::string> names;
    if (names.empty()) {
        Json tools = GetToolDefinitions();
        for (size_t i = 0; i < tools.size(); ++i) {
            names.insert(tools.at(i).Get("name").AsString());
        }
    }
    return names.find(name) != names.end();
}

} // namespace mcpaddin
