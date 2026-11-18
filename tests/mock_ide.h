#pragma once

#include "config.h"
#include "json.h"
#include "tools.h"

#include <map>
#include <memory>

namespace mcpaddin {

class MockIdeBackend : public IIdeBackend {
public:
    explicit MockIdeBackend(const ServerConfig& config);
    ~MockIdeBackend();

    ToolResult CallTool(const std::string& name, const Json& arguments);
    Json Status();
    bool IsAvailable();
    std::string BackendName();

private:
    struct Component {
        std::string name;
        std::string kind;
        std::wstring code;
        std::wstring fileText;
        std::map<std::string, std::string> properties;
        Component() {}
    };
    struct Project {
        std::string name;
        std::wstring vbpPath;
        std::string type;
        std::map<std::string, Component> components;
        std::map<std::string, std::string> properties;
        std::vector<std::string> references;
        std::string startMode;
        std::string compatibleOleServer;
        Project() {}
    };

    ToolResult Dispatch(const std::string& name, const Json& args);
    ToolResult ProjectListTool();
    ToolResult ProjectGetTool(const Json& args);
    ToolResult ProjectCreateTool(const Json& args);
    ToolResult ProjectScaffoldTool(const Json& args);
    ToolResult ProjectInspectTool(const Json& args);
    ToolResult ProjectValidateTool(const Json& args);
    ToolResult ProjectOpenTool(const Json& args);
    ToolResult ProjectCloseTool(const Json& args);
    ToolResult ProjectSaveTool(const Json& args);
    ToolResult ProjectBuildTool(const Json& args);
    ToolResult AddComponentTool(const Json& args);
    ToolResult AddFormTool(const Json& args);
    ToolResult ImportFileTool(const Json& args);
    ToolResult CreateFileTool(const Json& args);
    ToolResult RemoveComponentTool(const Json& args);
    ToolResult ComponentListTool(const Json& args);
    ToolResult ComponentGetTool(const Json& args);
    ToolResult ComponentGetCodeTool(const Json& args);
    ToolResult ComponentSetCodeTool(const Json& args);
    ToolResult ComponentEditCodeTool(const Json& args);
    ToolResult ComponentRenameTool(const Json& args);
    ToolResult ComponentExportTool(const Json& args);
    ToolResult ComponentReplaceTool(const Json& args);
    ToolResult ComponentPropertiesListTool(const Json& args);
    ToolResult ComponentReadPropertyTool(const Json& args);
    ToolResult ComponentWritePropertyTool(const Json& args);
    ToolResult ComponentActionTool(const Json& args);
    ToolResult CodeGetLinesTool(const Json& args);
    ToolResult CodeLinesEditTool(const Json& args);
    ToolResult CodeFindTool(const Json& args);
    ToolResult CodeSearchTool(const Json& args);
    ToolResult CodeCreateEventTool(const Json& args);
    ToolResult CodeStructureTool(const Json& args);
    ToolResult CodePanesListTool(const Json& args);
    ToolResult CodePaneSetTool(const Json& args);
    ToolResult WindowsListTool(const Json& args);
    ToolResult WindowActionTool(const Json& args);
    ToolResult ProjectReadPropertyTool(const Json& args);
    ToolResult ProjectWritePropertyTool(const Json& args);
    ToolResult ProjectAddToolboxProgIdTool(const Json& args);
    ToolResult FormGetTextTool(const Json& args);
    ToolResult FormSetTextTool(const Json& args);
    ToolResult FormListControlsTool(const Json& args);
    ToolResult FormAddControlTool(const Json& args);
    ToolResult FormRemoveControlTool(const Json& args);
    ToolResult FormSetPropertyTool(const Json& args);
    ToolResult SourceReadTool(const Json& args);
    ToolResult SourceWriteTool(const Json& args);
    ToolResult SourceEditTool(const Json& args);
    ToolResult SourceListTool(const Json& args);
    ToolResult ReferenceListTool(const Json& args);
    ToolResult ReferenceAddTool(const Json& args);
    ToolResult ReferenceRemoveTool(const Json& args);
    ToolResult Vb6BatchTool(const Json& args);

    Project* FindProject(const std::string& selector);
    Component* FindComponent(Project* project, const std::string& selector);
    void LoadProjectFromFiles(const std::wstring& vbpPath);
    Json DescribeProject(const Project& project, bool includeComponents, bool includeCode);
    Json DescribeComponent(const Component& component, bool includeCode);
    std::vector<std::wstring> AllowedRoots();
    bool IsAllowed(const std::wstring& path);

    const ServerConfig& config_;
    std::map<std::string, Project> projects_;
    std::string activeProject_;
};

} // namespace mcpaddin
