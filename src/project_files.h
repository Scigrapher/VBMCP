#pragma once

#include "common.h"
#include "json.h"

namespace mcpaddin {

enum class ProjectKind {
    StandardExe,
    ActiveXExe,
    ActiveXDll,
    ActiveXControl
};

struct ProjectComponentSpec {
    std::string kind;       // form, class, module, usercontrol
    std::string name;
    std::string fileName;
    std::wstring source;
    bool sourceProvided;
    ProjectComponentSpec() : sourceProvided(false) {}
};

struct ProjectTemplateSpec {
    std::string name;
    std::string title;
    std::string description;
    std::string company;
    std::string product;
    std::wstring directory;
    std::wstring vbpPath;
    std::string startup;
    ProjectKind kind;
    bool overwrite;
    bool createDefaultStartup;
    std::vector<ProjectComponentSpec> components;
    ProjectTemplateSpec() : kind(ProjectKind::StandardExe), overwrite(false), createDefaultStartup(true) {}
};

struct ProjectVbpInfo {
    std::string type;
    std::string name;
    std::string title;
    std::string exeName32;
    std::string startup;
    std::string componentFile;
    std::string outputExtension;
    std::vector<std::string> componentEntries;
    ProjectVbpInfo() : outputExtension(".exe") {}
};

bool ProjectKindFromString(const std::string& value, ProjectKind& kind);
std::string ProjectKindToString(ProjectKind kind);
std::string DefaultProjectExtension(ProjectKind kind);

bool CreateProjectFromTemplate(const ProjectTemplateSpec& spec,
                               std::vector<std::wstring>& filesCreated,
                               std::wstring& error);
bool CreateComponentSource(const std::string& kind, const std::string& name,
                           const std::string& title, const std::wstring& sourceEncoding,
                           std::wstring& source, std::wstring& suggestedExtension,
                           std::wstring& error);
bool CreateProjectVbp(const ProjectTemplateSpec& spec, const std::vector<std::wstring>& componentFiles,
                      std::wstring& vbpText, std::wstring& error);

bool ParseVbpFile(const std::wstring& path, ProjectVbpInfo& info, std::wstring& error);
std::wstring ResolveBuildOutput(const ProjectVbpInfo& info, const std::wstring& vbpPath,
                                const std::wstring& explicitOutput, const std::wstring& outputDirectory);

// VB6's VBIDE automation model in the retail IDE does not expose
// VBComponents.Import (DISP_E_UNKNOWNNAME).  These helpers provide the
// file-backed fallback used by the add-in for form/component creation.
bool UpdateVbpComponentEntry(const std::wstring& vbpPath, const std::string& kind,
                             const std::string& componentName,
                             const std::wstring& componentFile,
                             bool setStartup, std::wstring& error);
bool FindVbpComponentFile(const std::wstring& vbpPath, const std::string& componentName,
                          const std::wstring& expectedExtension,
                          std::wstring& componentFile, std::wstring& error);

struct FormControlInfo {
    std::string type;
    std::string name;
    int index;
    int line;
    bool hasIndex;
    std::map<std::string, std::string> rawProperties;
    FormControlInfo() : index(0), line(0), hasIndex(false) {}
};

bool AddFormControl(std::wstring& formText, const std::string& controlType,
                    const std::string& controlName, const std::string& containerName,
                    const std::vector<std::pair<std::string, Json> >& properties,
                    const Json& index, std::wstring& error);
bool RemoveFormControl(std::wstring& formText, const std::string& controlName,
                       const Json& index, std::wstring& error);
bool SetFormControlProperty(std::wstring& formText, const std::string& controlName,
                            const Json& index, const std::string& propertyName,
                            const Json& value, std::wstring& error);
bool ListFormControls(const std::wstring& formText, std::vector<FormControlInfo>& controls,
                      std::wstring& error);

std::wstring NormalizeNewlines(const std::wstring& text);
bool IsValidVbIdentifier(const std::string& name);
std::string NormalizeControlProgId(const std::string& value);

} // namespace mcpaddin
