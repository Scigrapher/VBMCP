# MCP tool reference

Generated from `tools/list` by `tests/generate_tool_reference.py`.

All tools accept a JSON object as `arguments`. Current project/component
selectors accept the VBIDE name, VBP path, or a 1-based numeric index.

## `ide_status`

Return VB6 IDE state, active project/component/code pane/window, add-in discovery, server endpoints, configuration location, and capabilities.

No arguments.

## `project_list`

List all projects currently open in the VB6 IDE, including paths, component counts, and saved state.

No arguments.

## `project_get`

Return detailed information about one open project, its components, and references.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or 1-based index (`1` or `#1`). Defaults to the active project. |

## `project_create`

Generate a complete on-disk VB6 project and optionally open it in the running IDE. The generated project is code-only by default and uses a Sub Main startup module.

| Argument | Type | Required | Description |
|---|---|---|---|
| `company` | `string` | no | Version company name. |
| `description` | `string` | no | Project description. |
| `directory` | `string` | no | Directory in which to create the project. Use this or vbpPath. |
| `name` | `string` | no | VB6 project name. Defaults to Project1 or the VBP file stem. |
| `open` | `boolean` | no | Open the generated project in the running VB6 IDE. Default true. |
| `overwrite` | `boolean` | no | Overwrite an existing VBP and component files. Default false. |
| `product` | `string` | no | Version product name. |
| `startupModule` | `boolean` | no | Create a module with Sub Main for standard EXE projects. Default true. |
| `title` | `string` | no | Display title. |
| `type` | `string` | yes | Project kind. |
| `vbpPath` | `string` | no | Exact .vbp path to create. Use this or directory. |

## `project_open`

Open a VB6 .vbp project in the running IDE.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | `string` | yes | Path to a .vbp project file. |

## `project_close`

Close a project in the running IDE.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `save` | `boolean` | no | Save before closing. Default true. |

## `project_save`

Save the project and its components, optionally using SaveAs.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | `string` | no | Optional new .vbp path for SaveAs. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `project_set_properties`

Set project-level properties supported by the VBIDE automation model.

| Argument | Type | Required | Description |
|---|---|---|---|
| `buildFileName` | `string` | no | Build output path. |
| `compatibleOleServer` | `string` | no | Compatible ActiveX OLE server path. |
| `description` | `string` | no | Project description. |
| `helpContextId` | `integer` | no | Help context ID. |
| `helpFile` | `string` | no | Help file path. |
| `name` | `string` | no | New project name. |
| `project` | `string` | no | Project name, VBP path, or index. |
| `startMode` | `string` | no | Project start mode. |
| `title` | `string` | no | Project title. |

## `project_read_property`

Read a project property through VBProject.ReadProperty using a VBP section and key.

| Argument | Type | Required | Description |
|---|---|---|---|
| `key` | `string` | yes | Property key within the VBP section. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `section` | `string` | yes | VBP section name, for example Project or MSComctlLib. |

## `project_write_property`

Write a project property through VBProject.WriteProperty using a VBP section and key.

| Argument | Type | Required | Description |
|---|---|---|---|
| `key` | `string` | yes | Property key within the VBP section. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `section` | `string` | yes | VBP section name, for example Project or MSComctlLib. |
| `value` | `string` | yes | Property value to write. |

## `project_add_toolbox_progid`

Add an ActiveX control ProgID to the VB6 toolbox through VBProject.AddToolboxProgID.

| Argument | Type | Required | Description |
|---|---|---|---|
| `fileName` | `string` | no | Optional control library path. |
| `progId` | `string` | yes | ActiveX control ProgID to add to the IDE toolbox. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `project_build`

Build a VB6 project using the live IDE's MakeCompiledFile method or VB6.EXE /make. Returns output existence, size, timestamp, and compiler log when available.

| Argument | Type | Required | Description |
|---|---|---|---|
| `logPath` | `string` | no | Compiler log path for command-line builds. |
| `method` | `string` | no | Build method. |
| `outputDirectory` | `string` | no | Optional output directory for command-line builds (/outdir). |
| `outputPath` | `string` | no | Optional explicit build output path. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `timeoutSeconds` | `integer` | no | Build timeout. |
| `vb6Exe` | `string` | no | Optional VB6.EXE path override for command-line builds. |

## `project_add_component`

Add a new component to the project using VBComponents.Add or AddCustom.

| Argument | Type | Required | Description |
|---|---|---|---|
| `code` | `string` | no | Optional initial code for class/module components. |
| `kind` | `string` | yes | Component kind. |
| `name` | `string` | no | Component name. |
| `progId` | `string` | no | Designer ProgID for AddCustom, such as VB.UserControl. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `project_add_form`

VB6-compatible form creation. Writes the .frm and updates the VBP directly instead of relying on VBComponents.Import, which retail VB6 does not expose. Reload the project after this call.

| Argument | Type | Required | Description |
|---|---|---|---|
| `code` | `string` | no | Optional code appended to the generated form. |
| `fileName` | `string` | no | Optional .frm filename. Relative paths are resolved inside the project directory. |
| `name` | `string` | yes | Form/component name. |
| `overwrite` | `boolean` | no | Overwrite an existing .frm file. Default false. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `source` | `string` | no | Optional complete .frm text. When omitted, a default VB6 form is generated. |
| `startup` | `boolean` | no | Set this form as the project startup object in the VBP. |
| `title` | `string` | no | Default form caption when source is omitted. |

## `project_add_file`

Import an existing VB6 component file into the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `path` | `string` | yes | Existing .frm, .cls, .bas, .ctl, .pag, or .dsr file to import. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `project_create_file`

Write a new VB6 component source file into the project directory and optionally import it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `code` | `string` | no | Optional code section text used when source is omitted. |
| `fileName` | `string` | no | Optional file name or path. Relative paths are resolved inside the project directory. |
| `import` | `boolean` | no | Import the generated file after writing it. Default true. |
| `kind` | `string` | yes | Component kind. |
| `name` | `string` | yes | Component name. |
| `overwrite` | `boolean` | no | Overwrite an existing file. Default false. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `source` | `string` | no | Optional complete VB6 source file text. |

## `project_remove_component`

Remove a component from the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `exportPath` | `string` | no | Export destination when saveFile is true. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `saveFile` | `boolean` | no | Export the component before removing it. Default false. |

## `component_list`

List project components with type, saved state, designer availability, and containing project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_get`

Return component metadata and optionally its complete code module text.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `includeCode` | `boolean` | no | Include the component code module text. Default true. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_get_code`

Read a component's complete code module text through CodeModule.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_set_code`

Replace a component's complete code module text.

| Argument | Type | Required | Description |
|---|---|---|---|
| `code` | `string` | yes | Complete replacement code text. |
| `component` | `string` | yes | Component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_edit_code`

Find and replace text inside a component code module.

| Argument | Type | Required | Description |
|---|---|---|---|
| `all` | `boolean` | no | Replace all matches. Default true. |
| `component` | `string` | yes | Component name. |
| `find` | `string` | yes | Text to find. |
| `matchCase` | `boolean` | no | Case-sensitive replacement. Default false. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `replace` | `string` | yes | Replacement text. |

## `code_get_lines`

Read a line range from a component's CodeModule and report the module line count.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `count` | `integer` | no | Number of lines to read. Omit or use 0 for the rest of the module. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `startLine` | `integer` | no | First 1-based line to read. Default 1. |

## `code_lines_edit`

Insert, replace, delete, or append lines through CodeModule.InsertLines, ReplaceLine, DeleteLines, or AddFromString.

| Argument | Type | Required | Description |
|---|---|---|---|
| `action` | `string` | yes | Line edit operation. |
| `component` | `string` | yes | Component name. |
| `count` | `integer` | no | Number of lines to delete. Default 1. |
| `line` | `integer` | no | 1-based line for insert/replace/delete. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `text` | `string` | no | Text to insert, replace, or append. |

## `code_find`

Search a component with CodeModule.Find and return the matched start/end positions.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `matchCase` | `boolean` | no | Use case-sensitive matching. Default false. |
| `patternSearch` | `boolean` | no | Treat target as a VBIDE pattern. Default false. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `startColumn` | `integer` | no | Starting 1-based column. Default 1. |
| `startLine` | `integer` | no | Starting 1-based line. Default 1. |
| `target` | `string` | yes | Text or pattern to find. |
| `wholeWord` | `boolean` | no | Match whole words only. Default false. |

## `code_create_event`

Create an event procedure with CodeModule.CreateEventProc and return its line number.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `eventName` | `string` | yes | Event name, for example Click. |
| `objectName` | `string` | yes | Object name, for example Command1. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `code_structure`

List CodeModule members with type, scope, static flag, and code location, plus declaration and total line counts.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_rename`

Rename a component in the project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Current component name. |
| `newName` | `string` | yes | New VB6 identifier. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_export`

Export a component to a VB6 source file.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `path` | `string` | yes | Export destination path. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_replace_file`

Export, rewrite, and re-import a component file. Useful for changing designer text or code that is not exposed by CodeModule.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `text` | `string` | yes | Complete replacement component file text, including designer block for forms. |

## `component_properties_list`

Enumerate the VBComponent.Properties collection with names, index counts, and optional scalar values.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `includeValues` | `boolean` | no | Read scalar property values. Default true. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_read_property`

Read a component property-bag value through VBComponent.ReadProperty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `key` | `string` | yes | Property key to read through VBComponent.ReadProperty. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `component_write_property`

Write a component property-bag value through VBComponent.WriteProperty.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Component name. |
| `key` | `string` | yes | Property key to write through VBComponent.WriteProperty. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `value` | `string` | yes | String value to write. |

## `component_action`

Activate or reload a component, save it under a new file name, or return its open designer window.

| Argument | Type | Required | Description |
|---|---|---|---|
| `action` | `string` | yes | Component action. |
| `component` | `string` | yes | Component name. |
| `path` | `string` | no | Destination path for saveAs. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `form_get_text`

Export a form and return its complete .frm designer and code text.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `form_set_text`

Replace a form by exporting it, rewriting the .frm file, and re-importing it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `text` | `string` | yes | Complete .frm replacement text. |

## `form_list_controls`

Parse the form designer text and return controls, types, names, control-array indexes, and properties.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `form_add_control`

Add an intrinsic or registered ActiveX control to a form by safely rewriting the form designer text and re-importing it.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `container` | `string` | no | Optional parent control name for nested controls. |
| `index` | `integer` | no | Optional control-array index. |
| `name` | `string` | yes | Control name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `properties` | `object` | no | Designer properties. JSON numbers and booleans are emitted unquoted; strings are quoted. |
| `type` | `string` | yes | Control type or ProgID, such as VB.CommandButton, textbox, label, frame. |

## `form_remove_control`

Remove a control block from a form by name and optional control-array index.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `index` | `integer` | no | Optional control-array index. |
| `name` | `string` | yes | Control name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `form_set_control_property`

Set or add a designer property on a form control.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | yes | Form component name. |
| `index` | `integer` | no | Optional control-array index. |
| `name` | `string` | yes | Control name. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `property` | `string` | yes | Designer property name. |
| `value` | `any` | yes | Designer property value. Numbers and booleans are emitted unquoted; strings are quoted. |

## `references_list`

List project references with names, GUIDs, versions, paths, and broken-reference state.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `reference_add`

Add a type library reference by path or GUID/version.

| Argument | Type | Required | Description |
|---|---|---|---|
| `guid` | `string` | no | Type library GUID to add instead of path. |
| `major` | `integer` | no | Type library major version. |
| `minor` | `integer` | no | Type library minor version. |
| `path` | `string` | no | Type library path to add. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `reference_remove`

Remove a project reference by name or path.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `reference` | `string` | yes | Reference name or full path. |

## `active_project_set`

Make a project the active VBIDE project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | yes | Project name, VBP path, or index. |

## `code_pane_set`

Focus or show a code pane, set its selection, or scroll it to a top line.

| Argument | Type | Required | Description |
|---|---|---|---|
| `action` | `string` | yes | Code pane action. |
| `endColumn` | `integer` | no | Selection end column. |
| `endLine` | `integer` | no | Selection end line. |
| `pane` | `string` | no | Code pane component/module name, caption, or 1-based index. Defaults to the active code pane. |
| `startColumn` | `integer` | no | Selection start column. |
| `startLine` | `integer` | no | Selection start line. |
| `topLine` | `integer` | no | Top visible line for the topLine action. |

## `code_panes_list`

List open code panes with module/component names, window captions, visible line counts, and current selections.

No arguments.

## `window_action`

Focus, show, hide, close, move, change state, set kind, or detach a VB6 IDE window.

| Argument | Type | Required | Description |
|---|---|---|---|
| `action` | `string` | yes | Window action. |
| `height` | `integer` | no | Window height. |
| `kind` | `integer` | no | vbext_WindowType value for SetKind. |
| `left` | `integer` | no | Window left coordinate. |
| `state` | `string` | no | Window state. |
| `top` | `integer` | no | Window top coordinate. |
| `width` | `integer` | no | Window width. |
| `window` | `string` | no | Window caption or 1-based index. Defaults to the active window. |

## `windows_list`

List VB6 IDE windows with caption, type, visibility, bounds, window state, HWND, and linked frame.

No arguments.

## `ide_execute_command`

Execute a VB6 IDE CommandBar control by caption or numeric ID. Useful for commands not exposed by the VBIDE object model.

| Argument | Type | Required | Description |
|---|---|---|---|
| `command` | `string` | yes | CommandBar caption to execute, for example 'Make Project1.exe...'. |
| `id` | `integer` | no | Optional CommandBar control ID. |

## `source_read`

Read a text or VB6 source file. Access is restricted to configured allowed roots and open project directories.

| Argument | Type | Required | Description |
|---|---|---|---|
| `encoding` | `string` | no | Encoding override: auto, ansi, utf8, utf16le, utf16be. |
| `path` | `string` | yes | Text file path. |

## `source_write`

Atomically write a text or VB6 source file. Access is restricted to configured allowed roots and open project directories.

| Argument | Type | Required | Description |
|---|---|---|---|
| `content` | `string` | yes | File content. |
| `createDirectories` | `boolean` | no | Create parent directories. Default true. |
| `encoding` | `string` | no | Encoding override: auto, ansi, utf8, utf16le, utf16be. |
| `path` | `string` | yes | Text file path. |

## `source_edit`

Find and replace text in a source file.

| Argument | Type | Required | Description |
|---|---|---|---|
| `all` | `boolean` | no | Replace all matches. Default true. |
| `find` | `string` | yes | Text to find. |
| `matchCase` | `boolean` | no | Case-sensitive search. Default false. |
| `path` | `string` | yes | Text file path. |
| `replace` | `string` | yes | Replacement text. |

## `source_list`

List files in a project or allowed directory.

| Argument | Type | Required | Description |
|---|---|---|---|
| `directory` | `string` | yes | Directory to list. |
| `pattern` | `string` | no | Wildcard pattern. Default *. |
| `recursive` | `boolean` | no | Recurse into subdirectories. Default false. |
