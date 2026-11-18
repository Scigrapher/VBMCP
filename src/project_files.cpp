#include "project_files.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace mcpaddin {

namespace {

std::wstring QuoteVbp(const std::string& value) {
    return L"\"" + Utf8ToWide(value) + L"\"";
}

std::wstring ProjectTypeString(ProjectKind kind) {
    switch (kind) {
        case ProjectKind::ActiveXExe: return L"OleExe";
        case ProjectKind::ActiveXDll: return L"OleDll";
        case ProjectKind::ActiveXControl: return L"Control";
        case ProjectKind::StandardExe:
        default: return L"Exe";
    }
}

std::wstring DefaultComponentExtension(const std::string& kind) {
    if (IEquals(kind, "form")) return L".frm";
    if (IEquals(kind, "class")) return L".cls";
    if (IEquals(kind, "module")) return L".bas";
    if (IEquals(kind, "usercontrol")) return L".ctl";
    if (IEquals(kind, "propertypage")) return L".pag";
    if (IEquals(kind, "designer")) return L".dsr";
    return L".bas";
}

std::wstring VbpComponentKey(const std::string& kind) {
    if (IEquals(kind, "form")) return L"Form";
    if (IEquals(kind, "class")) return L"Class";
    if (IEquals(kind, "module")) return L"Module";
    if (IEquals(kind, "usercontrol")) return L"UserControl";
    if (IEquals(kind, "propertypage")) return L"PropertyPage";
    if (IEquals(kind, "designer")) return L"Designer";
    return L"Module";
}

std::string SanitizeIdentifier(const std::string& value, const std::string& fallback) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char ch = (unsigned char)value[i];
        bool valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') || ch == '_';
        if (valid) out.push_back((char)ch);
    }
    if (out.empty()) out = fallback;
    if (out[0] >= '0' && out[0] <= '9') out = "_" + out;
    return out;
}

std::wstring FormSource(const std::string& name, const std::string& title) {
    std::wstring source;
    source += L"VERSION 5.00\r\n";
    source += L"Begin VB.Form " + Utf8ToWide(name) + L" \r\n";
    source += L"   Caption         =   \"" + Utf8ToWide(title.empty() ? name : title) + L"\"\r\n";
    source += L"   ClientHeight    =   3195\r\n";
    source += L"   ClientLeft      =   60\r\n";
    source += L"   ClientTop       =   345\r\n";
    source += L"   ClientWidth     =   4680\r\n";
    source += L"   LinkTopic       =   \"" + Utf8ToWide(name) + L"\"\r\n";
    source += L"   ScaleHeight     =   3195\r\n";
    source += L"   ScaleWidth      =   4680\r\n";
    source += L"   StartUpPosition =   3  'Windows Default\r\n";
    source += L"End\r\n";
    source += L"Attribute VB_Name = \"" + Utf8ToWide(name) + L"\"\r\n";
    source += L"Attribute VB_GlobalNameSpace = False\r\n";
    source += L"Attribute VB_Creatable = False\r\n";
    source += L"Attribute VB_PredeclaredId = True\r\n";
    source += L"Attribute VB_Exposed = False\r\n";
    source += L"Option Explicit\r\n";
    return source;
}

std::wstring ClassSource(const std::string& name, bool exposed) {
    std::wstring source;
    source += L"VERSION 1.0 CLASS\r\n";
    source += L"BEGIN\r\n";
    source += L"  MultiUse = -1  'True\r\n";
    source += L"END\r\n";
    source += L"Attribute VB_Name = \"" + Utf8ToWide(name) + L"\"\r\n";
    source += L"Attribute VB_GlobalNameSpace = False\r\n";
    source += L"Attribute VB_Creatable = " + std::wstring(exposed ? L"True" : L"False") + L"\r\n";
    source += L"Attribute VB_PredeclaredId = False\r\n";
    source += L"Attribute VB_Exposed = " + std::wstring(exposed ? L"True" : L"False") + L"\r\n";
    source += L"Option Explicit\r\n";
    return source;
}

std::wstring ModuleSource(const std::string& name, bool startup) {
    std::wstring source;
    source += L"Attribute VB_Name = \"" + Utf8ToWide(name) + L"\"\r\n";
    source += L"Option Explicit\r\n";
    if (startup) {
        source += L"\r\nPublic Sub Main()\r\nEnd Sub\r\n";
    }
    return source;
}

std::wstring UserControlSource(const std::string& name) {
    std::wstring source;
    source += L"VERSION 5.00\r\n";
    source += L"Begin VB.UserControl " + Utf8ToWide(name) + L" \r\n";
    source += L"   ClientHeight    =   3000\r\n";
    source += L"   ClientLeft      =   0\r\n";
    source += L"   ClientTop       =   0\r\n";
    source += L"   ClientWidth     =   3000\r\n";
    source += L"   ScaleHeight     =   3000\r\n";
    source += L"   ScaleWidth      =   3000\r\n";
    source += L"End\r\n";
    source += L"Attribute VB_Name = \"" + Utf8ToWide(name) + L"\"\r\n";
    source += L"Attribute VB_GlobalNameSpace = False\r\n";
    source += L"Attribute VB_Creatable = True\r\n";
    source += L"Attribute VB_PredeclaredId = False\r\n";
    source += L"Attribute VB_Exposed = True\r\n";
    source += L"Option Explicit\r\n";
    return source;
}

std::wstring PropertyPageSource(const std::string& name) {
    std::wstring source = UserControlSource(name);
    const std::wstring oldType = L"Begin VB.UserControl";
    const std::wstring newType = L"Begin VB.PropertyPage";
    size_t pos = source.find(oldType);
    if (pos != std::wstring::npos) source.replace(pos, oldType.size(), newType);
    return source;
}

struct Block {
    int beginLine;
    int endLine;
    int depth;
    std::wstring header;
    std::wstring type;
    std::wstring name;
    int index;
    bool hasIndex;
    Block() : beginLine(-1), endLine(-1), depth(0), index(0), hasIndex(false) {}
};

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            size_t end = i;
            if (end > start && text[end - 1] == L'\r') --end;
            lines.push_back(text.substr(start, end - start));
            start = i + 1;
        }
    }
    if (!lines.empty() && lines.back().empty() && !text.empty() && text[text.size() - 1] == L'\n') {
        lines.pop_back();
    }
    return lines;
}

std::wstring JoinLines(const std::vector<std::wstring>& lines) {
    std::wstring result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        result += L"\r\n";
    }
    return result;
}

std::wstring Indent(int depth) {
    return std::wstring((size_t)std::max(0, depth) * 3, L' ');
}

bool StartsWithI(const std::wstring& text, const wchar_t* prefix) {
    size_t len = wcslen(prefix);
    return text.size() >= len && _wcsnicmp(text.c_str(), prefix, len) == 0;
}

std::wstring TrimLeftWide(const std::wstring& text) {
    size_t pos = 0;
    while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) ++pos;
    return text.substr(pos);
}

std::vector<Block> FindBlocks(const std::vector<std::wstring>& lines) {
    std::vector<Block> blocks;
    std::vector<int> stack;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring trimmed = TrimLeftWide(lines[i]);
        if (StartsWithI(trimmed, L"Begin ")) {
            Block block;
            block.beginLine = (int)i;
            block.depth = (int)stack.size();
            block.header = trimmed;
            std::wstring rest = trimmed.substr(6);
            size_t space = rest.find(L' ');
            if (space == std::wstring::npos) {
                block.type = rest;
            } else {
                block.type = rest.substr(0, space);
                std::wstring tail = Trim(rest.substr(space + 1));
                size_t tailSpace = tail.find(L' ');
                block.name = tailSpace == std::wstring::npos ? tail : tail.substr(0, tailSpace);
            }
            blocks.push_back(block);
            stack.push_back((int)blocks.size() - 1);
        } else if (StartsWithI(trimmed, L"End") &&
                   (trimmed.size() == 3 || trimmed[3] == L' ' || trimmed[3] == L'\t' || trimmed[3] == L'\'')) {
            if (!stack.empty()) {
                int index = stack.back();
                stack.pop_back();
                blocks[index].endLine = (int)i;
            }
        }
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].endLine < 0) continue;
        for (int line = blocks[i].beginLine + 1; line < blocks[i].endLine; ++line) {
            std::wstring trimmed = Trim(lines[line]);
            size_t eq = trimmed.find(L'=');
            if (eq != std::wstring::npos && _wcsicmp(Trim(trimmed.substr(0, eq)).c_str(), L"Index") == 0) {
                std::wstring value = Trim(trimmed.substr(eq + 1));
                blocks[i].index = _wtoi(value.c_str());
                blocks[i].hasIndex = true;
                break;
            }
        }
    }
    return blocks;
}

bool MatchesControl(const Block& block, const std::string& name, const Json& index) {
    if (_wcsicmp(block.name.c_str(), Utf8ToWide(name).c_str()) != 0) return false;
    if (index.IsNull()) return true;
    if (!block.hasIndex) return index.AsInt() == 0;
    return block.index == index.AsInt();
}

int FindFormBlock(const std::vector<Block>& blocks) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].depth == 0 && IEquals(WideToUtf8(blocks[i].type), "VB.Form")) return (int)i;
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].depth == 0) return (int)i;
    }
    return -1;
}

int FindControlBlock(const std::vector<Block>& blocks, const std::string& name, const Json& index) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].depth >= 1 && MatchesControl(blocks[i], name, index)) return (int)i;
    }
    return -1;
}

std::wstring FormatPropertyValue(const std::string& name, const Json& value) {
    if (value.IsString()) {
        std::wstring text = value.AsWString();
        std::wstring out = L"\"";
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'"') out += L"\"\"";
            else out.push_back(text[i]);
        }
        out += L"\"";
        return out;
    }
    if (value.IsBool()) return value.AsBool() ? L"-1" : L"0";
    if (value.IsNumber()) {
        char buffer[64] = {0};
        double number = value.AsDouble();
        if (number == floor(number)) _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.0f", number);
        else _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.8g", number);
        (void)name;
        return Utf8ToWide(buffer);
    }
    return L"\"\"";
}

bool ValidateProperty(const std::string& name, const Json& value, std::wstring& error) {
    if (!IsValidVbIdentifier(name)) {
        error = L"Invalid designer property name: " + Utf8ToWide(name);
        return false;
    }
    if (value.IsString()) {
        std::wstring text = value.AsWString();
        if (text.find(L'\r') != std::wstring::npos || text.find(L'\n') != std::wstring::npos) {
            error = L"Designer property values cannot contain CR/LF: " + Utf8ToWide(name);
            return false;
        }
    }
    return true;
}

std::wstring FormatPropertyLine(int depth, const std::string& name, const Json& value) {
    std::wstring out = Indent(depth);
    std::wstring prop = Utf8ToWide(name);
    out += prop;
    if (prop.size() < 16) out.append(16 - prop.size(), L' ');
    out += L"=   ";
    out += FormatPropertyValue(name, value);
    return out;
}

void CollectDirectProperties(const std::vector<std::wstring>& lines, const Block& block,
                             std::map<std::wstring, std::pair<int, std::wstring> >& properties) {
    int nested = 0;
    for (int line = block.beginLine + 1; line < block.endLine; ++line) {
        std::wstring trimmed = TrimLeftWide(lines[line]);
        if (StartsWithI(trimmed, L"Begin ")) {
            ++nested;
            continue;
        }
        if (StartsWithI(trimmed, L"End") &&
            (trimmed.size() == 3 || trimmed[3] == L' ' || trimmed[3] == L'\t' || trimmed[3] == L'\'')) {
            if (nested > 0) --nested;
            continue;
        }
        if (nested != 0) continue;
        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(trimmed.substr(0, eq));
        std::wstring value = Trim(trimmed.substr(eq + 1));
        if (!key.empty() && key[0] != L'\'') {
            properties[key] = std::make_pair(line, value);
        }
    }
}

} // namespace

bool ProjectKindFromString(const std::string& value, ProjectKind& kind) {
    std::string v = ToLowerAscii(Trim(value));
    if (v == "exe" || v == "standard_exe" || v == "standardexe" || v == "standard exe") {
        kind = ProjectKind::StandardExe;
        return true;
    }
    if (v == "activex_exe" || v == "activexexe" || v == "oleexe" || v == "activex exe") {
        kind = ProjectKind::ActiveXExe;
        return true;
    }
    if (v == "dll" || v == "activex_dll" || v == "activexdll" || v == "oledll" || v == "activex dll") {
        kind = ProjectKind::ActiveXDll;
        return true;
    }
    if (v == "control" || v == "activex_control" || v == "activexcontrol" || v == "ocx" || v == "activex control") {
        kind = ProjectKind::ActiveXControl;
        return true;
    }
    return false;
}

std::string ProjectKindToString(ProjectKind kind) {
    switch (kind) {
        case ProjectKind::ActiveXExe: return "activex_exe";
        case ProjectKind::ActiveXDll: return "activex_dll";
        case ProjectKind::ActiveXControl: return "activex_control";
        case ProjectKind::StandardExe:
        default: return "standard_exe";
    }
}

std::string DefaultProjectExtension(ProjectKind kind) {
    switch (kind) {
        case ProjectKind::ActiveXDll: return ".dll";
        case ProjectKind::ActiveXControl: return ".ocx";
        case ProjectKind::ActiveXExe:
        case ProjectKind::StandardExe:
        default: return ".exe";
    }
}

bool IsValidVbIdentifier(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    unsigned char first = (unsigned char)name[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) return false;
    for (size_t i = 1; i < name.size(); ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) return false;
    }
    return true;
}

std::string NormalizeControlProgId(const std::string& value) {
    std::string v = Trim(value);
    if (v.find('.') != std::string::npos) return v;
    std::string lower = ToLowerAscii(v);
    if (lower == "commandbutton" || lower == "command" || lower == "button") return "VB.CommandButton";
    if (lower == "textbox" || lower == "text") return "VB.TextBox";
    if (lower == "label") return "VB.Label";
    if (lower == "frame") return "VB.Frame";
    if (lower == "checkbox" || lower == "check") return "VB.CheckBox";
    if (lower == "optionbutton" || lower == "radio") return "VB.OptionButton";
    if (lower == "combobox" || lower == "combo") return "VB.ComboBox";
    if (lower == "listbox" || lower == "list") return "VB.ListBox";
    if (lower == "picturebox" || lower == "picture") return "VB.PictureBox";
    if (lower == "image") return "VB.Image";
    if (lower == "line") return "VB.Line";
    if (lower == "shape") return "VB.Shape";
    if (lower == "timer") return "VB.Timer";
    if (lower == "hscrollbar" || lower == "hscroll") return "VB.HScrollBar";
    if (lower == "vscrollbar" || lower == "vscroll") return "VB.VScrollBar";
    if (lower == "drivelistbox" || lower == "drive") return "VB.DriveListBox";
    if (lower == "dirlistbox" || lower == "dir") return "VB.DirListBox";
    if (lower == "filelistbox" || lower == "file") return "VB.FileListBox";
    if (lower == "commondialog") return "MSComDlg.CommonDialog";
    return v;
}

bool CreateComponentSource(const std::string& kind, const std::string& name,
                           const std::string& title, const std::wstring& sourceEncoding,
                           std::wstring& source, std::wstring& suggestedExtension,
                           std::wstring& error) {
    (void)sourceEncoding;
    std::string cleanName = SanitizeIdentifier(name, "Item1");
    suggestedExtension = DefaultComponentExtension(kind);
    if (IEquals(kind, "form")) source = FormSource(cleanName, title);
    else if (IEquals(kind, "class")) source = ClassSource(cleanName, true);
    else if (IEquals(kind, "module")) source = ModuleSource(cleanName, false);
    else if (IEquals(kind, "usercontrol")) source = UserControlSource(cleanName);
    else if (IEquals(kind, "propertypage")) source = PropertyPageSource(cleanName);
    else {
        error = L"Unsupported component kind: " + Utf8ToWide(kind);
        return false;
    }
    return true;
}

bool CreateProjectVbp(const ProjectTemplateSpec& spec, const std::vector<std::wstring>& componentFiles,
                      std::wstring& vbpText, std::wstring& error) {
    if (!IsValidVbIdentifier(spec.name)) {
        error = L"Project name is not a valid VB6 identifier.";
        return false;
    }
    std::string projectName = spec.name;
    std::string title = spec.title.empty() ? projectName : spec.title;
    std::string exeName = projectName + DefaultProjectExtension(spec.kind);

    vbpText.clear();
    vbpText += L"Type=" + ProjectTypeString(spec.kind) + L"\r\n";
    for (size_t i = 0; i < componentFiles.size(); ++i) {
        const ProjectComponentSpec& comp = spec.components[i];
        std::wstring key = VbpComponentKey(comp.kind);
        std::wstring file = Utf8ToWide(comp.fileName);
        if (IEquals(comp.kind, "class") || IEquals(comp.kind, "module")) {
            vbpText += key + L"=" + Utf8ToWide(comp.name) + L";" + file + L"\r\n";
        } else {
            vbpText += key + L"=" + file + L"\r\n";
        }
    }
    if (spec.kind == ProjectKind::StandardExe && !spec.startup.empty()) {
        vbpText += L"Startup=\"" + Utf8ToWide(spec.startup) + L"\"\r\n";
    } else if (spec.kind == ProjectKind::StandardExe && spec.createDefaultStartup) {
        bool hasStartup = false;
        for (size_t i = 0; i < spec.components.size(); ++i) {
            if (IEquals(spec.components[i].kind, "module") &&
                spec.components[i].source.find(L"Sub Main") != std::wstring::npos) {
                hasStartup = true;
                break;
            }
        }
        if (hasStartup) vbpText += L"Startup=\"Sub Main\"\r\n";
    }
    if (!spec.description.empty()) vbpText += L"Description=" + QuoteVbp(spec.description) + L"\r\n";
    vbpText += L"Name=" + QuoteVbp(projectName) + L"\r\n";
    vbpText += L"Title=" + QuoteVbp(title) + L"\r\n";
    vbpText += L"ExeName32=" + QuoteVbp(exeName) + L"\r\n";
    vbpText += L"Command32=\"\"\r\n";
    vbpText += L"HelpContextID=\"0\"\r\n";
    vbpText += L"CompatibleMode=\"0\"\r\n";
    vbpText += L"MajorVer=1\r\n";
    vbpText += L"MinorVer=0\r\n";
    vbpText += L"RevisionVer=0\r\n";
    vbpText += L"AutoIncrementVer=0\r\n";
    vbpText += L"ServerSupportFiles=0\r\n";
    vbpText += L"VersionCompanyName=" + QuoteVbp(spec.company) + L"\r\n";
    vbpText += L"VersionFileDescription=" + QuoteVbp(title) + L"\r\n";
    vbpText += L"VersionLegalCopyright=\"\"\r\n";
    vbpText += L"VersionLegalTrademarks=\"\"\r\n";
    vbpText += L"VersionProductName=" + QuoteVbp(spec.product.empty() ? title : spec.product) + L"\r\n";
    vbpText += L"VersionComments=\"\"\r\n";
    vbpText += L"VersionFileVersion=1.00.0000\r\n";
    vbpText += L"VersionProductVersion=1.00.0000\r\n";
    vbpText += L"Reference=*\\G{00020430-0000-0000-C000-000000000046}#2.0#0#C:\\Windows\\System32\\stdole2.tlb#OLE Automation\r\n";
    return true;
}

bool CreateProjectFromTemplate(const ProjectTemplateSpec& input,
                               std::vector<std::wstring>& filesCreated,
                               std::wstring& error) {
    ProjectTemplateSpec spec = input;
    if (spec.name.empty()) spec.name = "Project1";
    if (!IsValidVbIdentifier(spec.name)) {
        error = L"Project name is not a valid VB6 identifier.";
        return false;
    }
    if (spec.directory.empty()) {
        error = L"Project directory is required.";
        return false;
    }
    if (!EnsureDirectory(spec.directory)) {
        error = L"Cannot create project directory: " + spec.directory;
        return false;
    }
    if (spec.vbpPath.empty()) spec.vbpPath = PathJoin(spec.directory, Utf8ToWide(spec.name) + L".vbp");
    if (!spec.overwrite && (FileExists(spec.vbpPath) || !spec.components.empty())) {
        // File conflict checks are performed below per file.
    }

    if (spec.components.empty()) {
        ProjectComponentSpec component;
        if (spec.kind == ProjectKind::StandardExe) {
            component.kind = "module";
            component.name = "Module1";
            component.fileName = "Module1.bas";
            component.source = ModuleSource(component.name, spec.createDefaultStartup);
            component.sourceProvided = true;
        } else if (spec.kind == ProjectKind::ActiveXControl) {
            component.kind = "usercontrol";
            component.name = "UserControl1";
            component.fileName = "UserControl1.ctl";
            component.source = UserControlSource(component.name);
            component.sourceProvided = true;
        } else {
            component.kind = "class";
            component.name = "Class1";
            component.fileName = "Class1.cls";
            component.source = ClassSource(component.name, true);
            component.sourceProvided = true;
        }
        spec.components.push_back(component);
    }

    for (size_t i = 0; i < spec.components.size(); ++i) {
        ProjectComponentSpec& comp = spec.components[i];
        if (comp.name.empty()) {
            char fallback[32] = {0};
            _snprintf_s(fallback, sizeof(fallback), _TRUNCATE, "Item%u", (unsigned)(i + 1));
            comp.name = fallback;
        }
        if (!IsValidVbIdentifier(comp.name)) {
            error = L"Component name is not a valid VB6 identifier: " + Utf8ToWide(comp.name);
            return false;
        }
        if (comp.fileName.empty()) {
            comp.fileName = comp.name + WideToUtf8(DefaultComponentExtension(comp.kind));
        }
        if (PathDirectory(Utf8ToWide(comp.fileName)).empty()) {
            comp.fileName = WideToUtf8(PathFileName(Utf8ToWide(comp.fileName)));
        }
        if (!comp.sourceProvided) {
            std::wstring ext;
            if (!CreateComponentSource(comp.kind, comp.name, comp.name, L"ansi", comp.source, ext, error)) {
                return false;
            }
        }
        std::wstring fullPath = PathJoin(spec.directory, Utf8ToWide(comp.fileName));
        if (!EnsureDirectory(PathDirectory(fullPath))) {
            error = L"Cannot create component directory: " + PathDirectory(fullPath);
            return false;
        }
        if (!spec.overwrite && FileExists(fullPath)) {
            error = L"Component file already exists: " + fullPath;
            return false;
        }
    }

    std::vector<std::wstring> componentFiles;
    for (size_t i = 0; i < spec.components.size(); ++i) componentFiles.push_back(Utf8ToWide(spec.components[i].fileName));
    std::wstring vbpText;
    if (!CreateProjectVbp(spec, componentFiles, vbpText, error)) return false;
    if (!spec.overwrite && FileExists(spec.vbpPath)) {
        error = L"Project file already exists: " + spec.vbpPath;
        return false;
    }

    std::vector<std::pair<std::wstring, std::wstring> > written;
    for (size_t i = 0; i < spec.components.size(); ++i) {
        std::wstring path = PathJoin(spec.directory, Utf8ToWide(spec.components[i].fileName));
        DWORD writeError = 0;
        if (!WriteTextFileAtomic(path, spec.components[i].source, L"ansi", &writeError)) {
            error = L"Could not write " + path + L": " + GetLastErrorMessage(writeError);
            for (size_t j = 0; j < written.size(); ++j) DeleteFileQuiet(written[j].first);
            return false;
        }
        written.push_back(std::make_pair(path, spec.components[i].source));
        filesCreated.push_back(path);
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(spec.vbpPath, vbpText, L"ansi", &writeError)) {
        error = L"Could not write project file: " + spec.vbpPath + L": " + GetLastErrorMessage(writeError);
        for (size_t j = 0; j < written.size(); ++j) DeleteFileQuiet(written[j].first);
        return false;
    }
    filesCreated.push_back(spec.vbpPath);
    return true;
}

static std::wstring Unquote(const std::wstring& value) {
    std::wstring result = Trim(value);
    if (result.size() >= 2 && result[0] == L'"' && result[result.size() - 1] == L'"') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

bool ParseVbpFile(const std::wstring& path, ProjectVbpInfo& info, std::wstring& error) {
    std::wstring text;
    DWORD readError = 0;
    if (!ReadTextFile(path, text, NULL, &readError)) {
        error = L"Cannot read project file: " + GetLastErrorMessage(readError);
        return false;
    }
    info = ProjectVbpInfo();
    std::vector<std::wstring> lines = SplitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = Trim(lines[i]);
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(0, eq));
        std::wstring value = Trim(line.substr(eq + 1));
        std::wstring unquoted = Unquote(value);
        if (_wcsicmp(key.c_str(), L"Type") == 0) info.type = WideToUtf8(unquoted);
        else if (_wcsicmp(key.c_str(), L"Name") == 0) info.name = WideToUtf8(unquoted);
        else if (_wcsicmp(key.c_str(), L"Title") == 0) info.title = WideToUtf8(unquoted);
        else if (_wcsicmp(key.c_str(), L"ExeName32") == 0) info.exeName32 = WideToUtf8(unquoted);
        else if (_wcsicmp(key.c_str(), L"Startup") == 0) info.startup = WideToUtf8(unquoted);
        else if (_wcsicmp(key.c_str(), L"Form") == 0 || _wcsicmp(key.c_str(), L"UserControl") == 0 ||
                 _wcsicmp(key.c_str(), L"PropertyPage") == 0 || _wcsicmp(key.c_str(), L"Designer") == 0 ||
                 _wcsicmp(key.c_str(), L"Class") == 0 || _wcsicmp(key.c_str(), L"Module") == 0) {
            std::wstring file = unquoted;
            size_t semicolon = file.find(L';');
            if (semicolon != std::wstring::npos) file = Trim(file.substr(semicolon + 1));
            info.componentEntries.push_back(WideToUtf8(file));
        }
    }
    if (info.outputExtension.empty()) info.outputExtension = ".exe";
    if (IEquals(info.type, "OleDll")) info.outputExtension = ".dll";
    else if (IEquals(info.type, "Control")) info.outputExtension = ".ocx";
    else info.outputExtension = ".exe";
    if (info.exeName32.empty()) info.exeName32 = info.name + info.outputExtension;
    if (info.name.empty()) {
        error = L"Project file has no Name entry.";
        return false;
    }
    return true;
}

namespace {

bool IsVbpComponentKey(const std::wstring& key) {
    return _wcsicmp(key.c_str(), L"Form") == 0 ||
           _wcsicmp(key.c_str(), L"UserControl") == 0 ||
           _wcsicmp(key.c_str(), L"PropertyPage") == 0 ||
           _wcsicmp(key.c_str(), L"Designer") == 0 ||
           _wcsicmp(key.c_str(), L"Class") == 0 ||
           _wcsicmp(key.c_str(), L"Module") == 0;
}

std::wstring VbpComponentPath(const std::wstring& value) {
    std::wstring item = Unquote(value);
    size_t semicolon = item.find(L';');
    if (semicolon != std::wstring::npos) item = Trim(item.substr(semicolon + 1));
    return item;
}

std::wstring VbpComponentName(const std::wstring& value) {
    std::wstring item = Unquote(value);
    size_t semicolon = item.find(L';');
    return semicolon == std::wstring::npos ? std::wstring() : Trim(item.substr(0, semicolon));
}

std::wstring VbpComponentValue(const std::string& kind, const std::string& componentName,
                               const std::wstring& componentFile) {
    std::wstring file = componentFile;
    if (IEquals(kind, "class")) {
        return Utf8ToWide(componentName) + L";" + file;
    }
    if (IEquals(kind, "module")) {
        return Utf8ToWide(componentName) + L";" + file;
    }
    return file;
}

std::wstring AttributeVbName(const std::wstring& source) {
    size_t marker = source.find(L"Attribute VB_Name");
    if (marker == std::wstring::npos) return std::wstring();
    size_t firstQuote = source.find(L'"', marker);
    if (firstQuote == std::wstring::npos) return std::wstring();
    size_t secondQuote = source.find(L'"', firstQuote + 1);
    if (secondQuote == std::wstring::npos) return std::wstring();
    return source.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

} // namespace

bool UpdateVbpComponentEntry(const std::wstring& vbpPath, const std::string& kind,
                             const std::string& componentName,
                             const std::wstring& componentFile,
                             bool setStartup, std::wstring& error) {
    std::wstring vbpText;
    std::wstring encoding;
    DWORD readError = 0;
    if (!ReadTextFile(vbpPath, vbpText, &encoding, &readError)) {
        error = L"Cannot read VBP: " + GetLastErrorMessage(readError);
        return false;
    }

    const std::wstring key = VbpComponentKey(kind);
    std::wstring value = VbpComponentValue(kind, componentName, componentFile);
    std::vector<std::wstring> lines = SplitLines(NormalizeNewlines(vbpText));
    int insertAfter = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = Trim(lines[i]);
        if (line.empty()) continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring currentKey = Trim(line.substr(0, eq));
        std::wstring currentValue = Trim(line.substr(eq + 1));
        if (_wcsicmp(currentKey.c_str(), L"Type") == 0) insertAfter = (int)i + 1;
        if (!IsVbpComponentKey(currentKey)) continue;
        insertAfter = (int)i + 1;

        bool matches = false;
        if (_wcsicmp(currentKey.c_str(), key.c_str()) == 0) {
            std::wstring currentPath = VbpComponentPath(currentValue);
            std::wstring wantedPath = VbpComponentPath(value);
            std::wstring currentFull = IsAbsolutePath(currentPath) ? currentPath :
                                       PathJoin(PathDirectory(vbpPath), currentPath);
            std::wstring wantedFull = IsAbsolutePath(wantedPath) ? wantedPath :
                                      PathJoin(PathDirectory(vbpPath), wantedPath);
            matches = NormalizePathForCompare(currentFull) == NormalizePathForCompare(wantedFull);
            if (!matches && (IEquals(kind, "class") || IEquals(kind, "module"))) {
                std::wstring currentName = VbpComponentName(currentValue);
                matches = _wcsicmp(currentName.c_str(), Utf8ToWide(componentName).c_str()) == 0;
            }
        }
        if (matches) {
            lines[i] = key + L"=" + value;
            if (setStartup && IEquals(kind, "form")) {
                bool foundStartup = false;
                for (size_t j = 0; j < lines.size(); ++j) {
                    std::wstring candidate = Trim(lines[j]);
                    if (candidate.size() > 8 && _wcsicmp(candidate.substr(0, 8).c_str(), L"Startup=") == 0) {
                        lines[j] = L"Startup=\"" + Utf8ToWide(componentName) + L"\"";
                        foundStartup = true;
                        break;
                    }
                }
                if (!foundStartup) {
                    lines.insert(lines.begin() + insertAfter, L"Startup=\"" + Utf8ToWide(componentName) + L"\"");
                }
            }
            DWORD writeError = 0;
            if (!WriteTextFileAtomic(vbpPath, JoinLines(lines), encoding, &writeError)) {
                error = L"Cannot write VBP: " + GetLastErrorMessage(writeError);
                return false;
            }
            return true;
        }
    }

    if (insertAfter < 0 || insertAfter > (int)lines.size()) insertAfter = (int)lines.size();
    bool hasStartupLine = false;
    if (setStartup && IEquals(kind, "form")) {
        for (size_t i = 0; i < lines.size(); ++i) {
            std::wstring candidate = Trim(lines[i]);
            if (candidate.size() > 8 && _wcsicmp(candidate.substr(0, 8).c_str(), L"Startup=") == 0) {
                lines[i] = L"Startup=\"" + Utf8ToWide(componentName) + L"\"";
                hasStartupLine = true;
                break;
            }
        }
    }
    lines.insert(lines.begin() + insertAfter, key + L"=" + value);
    if (setStartup && IEquals(kind, "form") && !hasStartupLine) {
        lines.insert(lines.begin() + insertAfter + 1, L"Startup=\"" + Utf8ToWide(componentName) + L"\"");
    }
    DWORD writeError = 0;
    if (!WriteTextFileAtomic(vbpPath, JoinLines(lines), encoding, &writeError)) {
        error = L"Cannot write VBP: " + GetLastErrorMessage(writeError);
        return false;
    }
    return true;
}

bool FindVbpComponentFile(const std::wstring& vbpPath, const std::string& componentName,
                          const std::wstring& expectedExtension,
                          std::wstring& componentFile, std::wstring& error) {
    std::wstring vbpText;
    DWORD readError = 0;
    if (!ReadTextFile(vbpPath, vbpText, NULL, &readError)) {
        error = L"Cannot read VBP: " + GetLastErrorMessage(readError);
        return false;
    }
    std::wstring wantedName = Utf8ToWide(componentName);
    std::vector<std::wstring> lines = SplitLines(NormalizeNewlines(vbpText));
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = Trim(lines[i]);
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(0, eq));
        if (!IsVbpComponentKey(key)) continue;
        std::wstring value = Trim(line.substr(eq + 1));
        std::wstring entryName = VbpComponentName(value);
        std::wstring entryPath = VbpComponentPath(value);
        if (entryPath.empty()) continue;
        std::wstring fullPath = IsAbsolutePath(entryPath) ? entryPath : PathJoin(PathDirectory(vbpPath), entryPath);
        if (!expectedExtension.empty() &&
            _wcsicmp(PathExtension(fullPath).c_str(), expectedExtension.c_str()) != 0) {
            continue;
        }
        if (wantedName.empty()) {
            componentFile = fullPath;
            return true;
        }
        if (!entryName.empty() && _wcsicmp(entryName.c_str(), wantedName.c_str()) == 0) {
            componentFile = fullPath;
            return true;
        }
        if (_wcsicmp(PathStem(fullPath).c_str(), wantedName.c_str()) == 0) {
            componentFile = fullPath;
            return true;
        }
        std::wstring source;
        if (FileExists(fullPath) && ReadTextFile(fullPath, source, NULL, NULL) &&
            _wcsicmp(AttributeVbName(source).c_str(), wantedName.c_str()) == 0) {
            componentFile = fullPath;
            return true;
        }
    }
    error = L"Could not map component to a file in the VBP: " + wantedName;
    return false;
}

std::wstring ResolveBuildOutput(const ProjectVbpInfo& info, const std::wstring& vbpPath,
                                const std::wstring& explicitOutput, const std::wstring& outputDirectory) {
    std::wstring output;
    if (!explicitOutput.empty()) {
        output = IsAbsolutePath(explicitOutput) ? explicitOutput : PathJoin(PathDirectory(vbpPath), explicitOutput);
    } else {
        std::wstring fileName = Utf8ToWide(info.exeName32);
        if (PathFileName(fileName).empty()) fileName = Utf8ToWide(info.name + info.outputExtension);
        output = IsAbsolutePath(fileName) ? fileName : PathJoin(PathDirectory(vbpPath), fileName);
    }
    if (!outputDirectory.empty()) {
        std::wstring dir = IsAbsolutePath(outputDirectory) ? outputDirectory : PathJoin(PathDirectory(vbpPath), outputDirectory);
        output = PathJoin(dir, PathFileName(output));
    }
    return GetFullPath(output);
}

std::wstring NormalizeNewlines(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 32);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            out += L"\r\n";
        } else if (text[i] == L'\n') {
            out += L"\r\n";
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool AddFormControl(std::wstring& formText, const std::string& controlType,
                    const std::string& controlName, const std::string& containerName,
                    const std::vector<std::pair<std::string, Json> >& properties,
                    const Json& index, std::wstring& error) {
    if (!IsValidVbIdentifier(controlName)) {
        error = L"Control name is not a valid VB6 identifier.";
        return false;
    }
    if (controlType.empty() || controlType.find('\r') != std::string::npos ||
        controlType.find('\n') != std::string::npos) {
        error = L"Control type is empty or contains a line break.";
        return false;
    }
    std::wstring text = NormalizeNewlines(formText);
    std::vector<std::wstring> lines = SplitLines(text);
    std::vector<Block> blocks = FindBlocks(lines);
    int formIndex = FindFormBlock(blocks);
    if (formIndex < 0) {
        error = L"The component text does not contain a VB6 designer block.";
        return false;
    }
    int parentIndex = formIndex;
    if (!containerName.empty()) {
        parentIndex = FindControlBlock(blocks, containerName, Json::NullValue());
        if (parentIndex < 0) {
            error = L"Container control not found: " + Utf8ToWide(containerName);
            return false;
        }
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].depth >= 1 && _wcsicmp(blocks[i].name.c_str(), Utf8ToWide(controlName).c_str()) == 0) {
            if (!index.IsNull() && (!blocks[i].hasIndex || blocks[i].index != index.AsInt())) continue;
            error = L"A control with that name and index already exists.";
            return false;
        }
    }

    int insertLine = blocks[parentIndex].endLine;
    int depth = blocks[parentIndex].depth + 1;
    std::vector<std::wstring> block;
    block.push_back(Indent(depth) + L"Begin " + Utf8ToWide(NormalizeControlProgId(controlType)) +
                    L" " + Utf8ToWide(controlName) + L" ");
    std::map<std::string, Json> supplied;
    for (size_t i = 0; i < properties.size(); ++i) {
        if (!ValidateProperty(properties[i].first, properties[i].second, error)) return false;
        supplied[properties[i].first] = properties[i].second;
    }
    if (!index.IsNull() || supplied.find("Index") != supplied.end()) {
        Json idx = index.IsNull() ? supplied["Index"] : index;
        block.push_back(FormatPropertyLine(depth + 1, "Index", idx));
    }
    if (supplied.find("Left") == supplied.end()) block.push_back(Indent(depth + 1) + L"Left            =   120");
    if (supplied.find("Top") == supplied.end()) block.push_back(Indent(depth + 1) + L"Top             =   120");
    if (supplied.find("Width") == supplied.end()) block.push_back(Indent(depth + 1) + L"Width           =   1215");
    if (supplied.find("Height") == supplied.end()) block.push_back(Indent(depth + 1) + L"Height          =   375");
    if (IEquals(NormalizeControlProgId(controlType), "VB.Timer") &&
        supplied.find("Interval") == supplied.end()) {
        block.push_back(Indent(depth + 1) + L"Interval        =   0");
    }
    for (std::map<std::string, Json>::const_iterator it = supplied.begin(); it != supplied.end(); ++it) {
        if (IEquals(it->first, "Index")) continue;
        block.push_back(FormatPropertyLine(depth + 1, it->first, it->second));
    }
    block.push_back(Indent(depth) + L"End");
    lines.insert(lines.begin() + insertLine, block.begin(), block.end());
    formText = JoinLines(lines);
    return true;
}

bool RemoveFormControl(std::wstring& formText, const std::string& controlName,
                       const Json& index, std::wstring& error) {
    std::wstring text = NormalizeNewlines(formText);
    std::vector<std::wstring> lines = SplitLines(text);
    std::vector<Block> blocks = FindBlocks(lines);
    int blockIndex = FindControlBlock(blocks, controlName, index);
    if (blockIndex < 0) {
        error = L"Control not found: " + Utf8ToWide(controlName);
        return false;
    }
    lines.erase(lines.begin() + blocks[blockIndex].beginLine, lines.begin() + blocks[blockIndex].endLine + 1);
    formText = JoinLines(lines);
    return true;
}

bool SetFormControlProperty(std::wstring& formText, const std::string& controlName,
                            const Json& index, const std::string& propertyName,
                            const Json& value, std::wstring& error) {
    if (propertyName.empty()) {
        error = L"Property name is required.";
        return false;
    }
    if (!ValidateProperty(propertyName, value, error)) return false;
    std::wstring text = NormalizeNewlines(formText);
    std::vector<std::wstring> lines = SplitLines(text);
    std::vector<Block> blocks = FindBlocks(lines);
    int blockIndex = FindControlBlock(blocks, controlName, index);
    if (blockIndex < 0) {
        error = L"Control not found: " + Utf8ToWide(controlName);
        return false;
    }
    const Block& block = blocks[blockIndex];
    std::wstring target = Utf8ToWide(propertyName);
    std::map<std::wstring, std::pair<int, std::wstring> > properties;
    CollectDirectProperties(lines, block, properties);
    for (std::map<std::wstring, std::pair<int, std::wstring> >::const_iterator it = properties.begin();
         it != properties.end(); ++it) {
        if (_wcsicmp(it->first.c_str(), target.c_str()) == 0) {
            lines[it->second.first] = FormatPropertyLine(block.depth + 1, propertyName, value);
            formText = JoinLines(lines);
            return true;
        }
    }
    lines.insert(lines.begin() + block.endLine, FormatPropertyLine(block.depth + 1, propertyName, value));
    formText = JoinLines(lines);
    return true;
}

bool ListFormControls(const std::wstring& formText, std::vector<FormControlInfo>& controls,
                      std::wstring& error) {
    std::wstring text = NormalizeNewlines(formText);
    std::vector<std::wstring> lines = SplitLines(text);
    std::vector<Block> blocks = FindBlocks(lines);
    int formIndex = FindFormBlock(blocks);
    if (formIndex < 0) {
        error = L"The component text does not contain a VB6 designer block.";
        return false;
    }
    controls.clear();
    for (size_t i = 0; i < blocks.size(); ++i) {
        if ((int)i == formIndex || blocks[i].depth < 1) continue;
        FormControlInfo info;
        info.type = WideToUtf8(blocks[i].type);
        info.name = WideToUtf8(blocks[i].name);
        info.line = blocks[i].beginLine + 1;
        info.hasIndex = blocks[i].hasIndex;
        info.index = blocks[i].index;
        std::map<std::wstring, std::pair<int, std::wstring> > properties;
        CollectDirectProperties(lines, blocks[i], properties);
        for (std::map<std::wstring, std::pair<int, std::wstring> >::const_iterator it = properties.begin();
             it != properties.end(); ++it) {
            info.rawProperties[WideToUtf8(it->first)] = WideToUtf8(it->second.second);
        }
        controls.push_back(info);
    }
    return true;
}

} // namespace mcpaddin
