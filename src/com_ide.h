#pragma once

#include "common.h"
#include "config.h"
#include "json.h"
#include "tools.h"

namespace mcpaddin {

class ComIdeBackend : public IIdeBackend {
public:
    static const UINT kToolMessage = WM_APP + 0x51;

    explicit ComIdeBackend(const ServerConfig& config);
    ~ComIdeBackend();

    void Attach(IDispatch* vbe, HWND dispatchWindow);
    void BeginShutdown();
    void Detach();
    void HandlePostedCall(LPARAM parameter);

    ToolResult CallTool(const std::string& name, const Json& arguments);
    Json Status();
    bool IsAvailable();
    std::string BackendName();

private:
    struct WorkCall;
    ToolResult ExecuteTool(const std::string& name, const Json& arguments);
    void SetLastError(const std::string& error);
    bool SameThread() const;

    ToolResult IdeStatus();
    ToolResult ProjectList();
    ToolResult ProjectGet(const Json& args);
    ToolResult ProjectCreate(const Json& args);
    ToolResult ProjectOpen(const Json& args);
    ToolResult ProjectClose(const Json& args);
    ToolResult ProjectSave(const Json& args);
    ToolResult ProjectSetProperties(const Json& args);
    ToolResult ProjectReadProperty(const Json& args);
    ToolResult ProjectWriteProperty(const Json& args);
    ToolResult ProjectAddToolboxProgId(const Json& args);
    ToolResult ProjectBuild(const Json& args);
    ToolResult ProjectAddComponent(const Json& args);
    ToolResult ProjectAddForm(const Json& args);
    ToolResult ProjectAddFile(const Json& args);
    ToolResult ProjectCreateFile(const Json& args);
    ToolResult ProjectRemoveComponent(const Json& args);
    ToolResult ComponentList(const Json& args);
    ToolResult ComponentGet(const Json& args);
    ToolResult ComponentGetCode(const Json& args);
    ToolResult ComponentSetCode(const Json& args);
    ToolResult ComponentEditCode(const Json& args);
    ToolResult ComponentRename(const Json& args);
    ToolResult ComponentExport(const Json& args);
    ToolResult ComponentReplaceFile(const Json& args);
    ToolResult ComponentPropertiesList(const Json& args);
    ToolResult ComponentReadProperty(const Json& args);
    ToolResult ComponentWriteProperty(const Json& args);
    ToolResult ComponentAction(const Json& args);
    ToolResult CodeGetLines(const Json& args);
    ToolResult CodeLinesEdit(const Json& args);
    ToolResult CodeFind(const Json& args);
    ToolResult CodeCreateEvent(const Json& args);
    ToolResult CodeStructure(const Json& args);
    ToolResult CodePanesList(const Json& args);
    ToolResult CodePaneSet(const Json& args);
    ToolResult WindowsList(const Json& args);
    ToolResult WindowAction(const Json& args);
    ToolResult FormGetText(const Json& args);
    ToolResult FormSetText(const Json& args);
    ToolResult FormListControls(const Json& args);
    ToolResult FormAddControl(const Json& args);
    ToolResult FormRemoveControl(const Json& args);
    ToolResult FormSetControlProperty(const Json& args);
    ToolResult ReferencesList(const Json& args);
    ToolResult ReferenceAdd(const Json& args);
    ToolResult ReferenceRemove(const Json& args);
    ToolResult ActiveProjectSet(const Json& args);
    ToolResult IdeExecuteCommand(const Json& args);
    ToolResult SourceRead(const Json& args);
    ToolResult SourceWrite(const Json& args);
    ToolResult SourceEdit(const Json& args);
    ToolResult SourceList(const Json& args);

    const ServerConfig& config_;
    ComPtr<IDispatch> vbe_;
    HWND dispatchWindow_;
    DWORD ideThreadId_;
    volatile LONG connected_;
    volatile LONG shuttingDown_;
    HANDLE shutdownEvent_;
    CRITICAL_SECTION stateLock_;
    std::string lastError_;
};

} // namespace mcpaddin
