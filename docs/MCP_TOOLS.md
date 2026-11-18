# MCP tool reference

Generated from `tools/list` by `tests/generate_tool_reference.py`.

For AI-oriented workflow guidance, read MCP resource
`vb6://guide/ai-quickstart` or see `docs/AI_GUIDE.md`.

Start new applications with `project_scaffold`; use `project_inspect`,
`code_search`, and `project_validate` to understand and verify existing
projects; use `vb6_batch` for multiple sequential operations.

All tools accept a JSON object as `arguments`. Current project/component
selectors accept the VBIDE name, VBP path, or a 1-based numeric index.

## Tool index

| Tool | Purpose |
|---|---|
| [`vb6_batch`](#vb6_batch) | Run several MCP tool calls sequentially in one request. Use for multi-control form builds, edit/read verification loops, and multi-component code generation. Calls run in order on the VB6 UI thread; there is no transaction rollback. |
| [`ide_status`](#ide_status) | Return VB6 IDE state, active project/component/code pane/window, add-in discovery, server endpoints, configuration location, tool groups, and the recommended AI workflow. Call this first in a new session. |
| [`project_list`](#project_list) | List all projects currently open in the VB6 IDE, including paths, component counts, and saved state. |
| [`project_get`](#project_get) | Return detailed information about one open project, its components, and references. |
| [`project_inspect`](#project_inspect) | Return a project-wide map for AI comprehension: component kinds, file names, code member summaries, form controls, references, and build settings. Prefer this over dumping every component's full code. |
| [`project_validate`](#project_validate) | Check project-file structure, component files, VB_Name attributes, Option Explicit, form designer syntax, duplicate controls, startup resolution, and broken references. This is a fast structural check; use project_build with commandline for compiler errors. |
| [`project_scaffold`](#project_scaffold) | PREFERRED bootstrap for new VB6 applications. Create a complete on-disk project from a declarative component manifest, add form controls and references, open it in the IDE, and optionally build it in one call. Use project_create only for a minimal empty project. |
| [`project_create`](#project_create) | Generate a complete on-disk VB6 project and optionally open it in the running IDE. The generated project is code-only by default and uses a Sub Main startup module. |
| [`project_open`](#project_open) | Open a VB6 .vbp project in the running IDE. |
| [`project_close`](#project_close) | Close a project in the running IDE. |
| [`project_save`](#project_save) | Save the project and its components, optionally using SaveAs. |
| [`project_set_properties`](#project_set_properties) | Set project-level properties supported by the VBIDE automation model. |
| [`project_read_property`](#project_read_property) | Read a project property through VBProject.ReadProperty using a VBP section and key. |
| [`project_write_property`](#project_write_property) | Write a project property through VBProject.WriteProperty using a VBP section and key. |
| [`project_add_toolbox_progid`](#project_add_toolbox_progid) | Add an ActiveX control ProgID to the VB6 toolbox through VBProject.AddToolboxProgID. |
| [`project_build`](#project_build) | Build a VB6 project using the live IDE's MakeCompiledFile method or VB6.EXE /make. Returns output existence, size, timestamp, and compiler log when available. |
| [`project_add_component`](#project_add_component) | Add a new component to the project using VBComponents.Add or AddCustom. |
| [`project_add_form`](#project_add_form) | VB6-compatible form creation. Writes the .frm and updates the VBP directly instead of relying on VBComponents.Import, which retail VB6 does not expose. Reload the project after this call. |
| [`project_add_file`](#project_add_file) | Import an existing VB6 component file into the project. |
| [`project_create_file`](#project_create_file) | Write a new VB6 component source file into the project directory and optionally import it. |
| [`project_remove_component`](#project_remove_component) | Remove a component from the project. |
| [`component_list`](#component_list) | List project components with type, saved state, designer availability, and containing project. |
| [`component_get`](#component_get) | Return component metadata and optionally its complete code module text. |
| [`component_get_code`](#component_get_code) | Read a component's complete code module text through CodeModule. |
| [`component_set_code`](#component_set_code) | Replace a component's complete code module text. |
| [`component_edit_code`](#component_edit_code) | Find and replace text inside a component code module. |
| [`code_get_lines`](#code_get_lines) | Read a line range from a component's CodeModule and report the module line count. |
| [`code_lines_edit`](#code_lines_edit) | Insert, replace, delete, or append lines through CodeModule.InsertLines, ReplaceLine, DeleteLines, or AddFromString. |
| [`code_find`](#code_find) | Search a component with CodeModule.Find and return the matched start/end positions. |
| [`code_search`](#code_search) | Search code modules across the whole project and return component, line, column, and preview text. Designer/header text is not searched; use project_inspect or form_list_controls for form metadata. |
| [`code_create_event`](#code_create_event) | Create an event procedure with CodeModule.CreateEventProc and return its line number. |
| [`code_structure`](#code_structure) | List CodeModule members with type, scope, static flag, and code location, plus declaration and total line counts. |
| [`component_rename`](#component_rename) | Rename a component in the project. |
| [`component_export`](#component_export) | Export a component to a VB6 source file. |
| [`component_replace_file`](#component_replace_file) | Export, rewrite, and re-import a component file. Useful for changing designer text or code that is not exposed by CodeModule. |
| [`component_properties_list`](#component_properties_list) | Enumerate the VBComponent.Properties collection with names, index counts, and optional scalar values. |
| [`component_read_property`](#component_read_property) | Read a component property-bag value through VBComponent.ReadProperty. |
| [`component_write_property`](#component_write_property) | Write a component property-bag value through VBComponent.WriteProperty. |
| [`component_action`](#component_action) | Activate or reload a component, save it under a new file name, or return its open designer window. |
| [`form_get_text`](#form_get_text) | Export a form and return its complete .frm designer and code text. |
| [`form_set_text`](#form_set_text) | Replace a form by exporting it, rewriting the .frm file, and re-importing it. |
| [`form_list_controls`](#form_list_controls) | Parse the form designer text and return controls, types, names, control-array indexes, and properties. |
| [`form_add_control`](#form_add_control) | Add an intrinsic or registered ActiveX control to a form by safely rewriting the form designer text and re-importing it. |
| [`form_remove_control`](#form_remove_control) | Remove a control block from a form by name and optional control-array index. |
| [`form_set_control_property`](#form_set_control_property) | Set or add a designer property on a form control. |
| [`references_list`](#references_list) | List project references with names, GUIDs, versions, paths, and broken-reference state. |
| [`reference_add`](#reference_add) | Add a type library reference by path or GUID/version. |
| [`reference_remove`](#reference_remove) | Remove a project reference by name or path. |
| [`active_project_set`](#active_project_set) | Make a project the active VBIDE project. |
| [`code_pane_set`](#code_pane_set) | Focus or show a code pane, set its selection, or scroll it to a top line. |
| [`code_panes_list`](#code_panes_list) | List open code panes with module/component names, window captions, visible line counts, and current selections. |
| [`window_action`](#window_action) | Focus, show, hide, close, move, change state, set kind, or detach a VB6 IDE window. |
| [`windows_list`](#windows_list) | List VB6 IDE windows with caption, type, visibility, bounds, window state, HWND, and linked frame. |
| [`ide_execute_command`](#ide_execute_command) | Execute a VB6 IDE CommandBar control by caption or numeric ID. Useful for commands not exposed by the VBIDE object model. |
| [`source_read`](#source_read) | Read a text or VB6 source file. Access is restricted to configured allowed roots and open project directories. |
| [`source_write`](#source_write) | Atomically write a text or VB6 source file. Access is restricted to configured allowed roots and open project directories. |
| [`source_edit`](#source_edit) | Find and replace text in a source file. |
| [`source_list`](#source_list) | List files in a project or allowed directory. |

## `vb6_batch`

Run several MCP tool calls sequentially in one request. Use for multi-control form builds, edit/read verification loops, and multi-component code generation. Calls run in order on the VB6 UI thread; there is no transaction rollback.

| Argument | Type | Required | Description |
|---|---|---|---|
| `calls` | `array<object>` | yes | Sequential tool calls. Each item is {"tool":"tool_name","arguments":{...}}. |
| `maxCalls` | `integer` | no | Maximum calls to execute, 1-200. Default 50. |
| `stopOnError` | `boolean` | no | Stop after the first failed call. Default true. |

### `calls[]`

Array item schema.

| Property | Type | Required | Description |
|---|---|---|---|
| `arguments` | `object` | no | Arguments object passed to that tool. |
| `tool` | `string` | yes | Registered MCP tool name. vb6_batch cannot call itself. |

## `ide_status`

Return VB6 IDE state, active project/component/code pane/window, add-in discovery, server endpoints, configuration location, tool groups, and the recommended AI workflow. Call this first in a new session.

No arguments.

## `project_list`

List all projects currently open in the VB6 IDE, including paths, component counts, and saved state.

No arguments.

## `project_get`

Return detailed information about one open project, its components, and references.

| Argument | Type | Required | Description |
|---|---|---|---|
| `project` | `string` | no | Project name, VBP path, or 1-based index (`1` or `#1`). Defaults to the active project. |

## `project_inspect`

Return a project-wide map for AI comprehension: component kinds, file names, code member summaries, form controls, references, and build settings. Prefer this over dumping every component's full code.

| Argument | Type | Required | Description |
|---|---|---|---|
| `includeControls` | `boolean` | no | Include parsed form controls for designer components. Default true. |
| `includeMembers` | `boolean` | no | Include code member/procedure summaries. Default true. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |

## `project_validate`

Check project-file structure, component files, VB_Name attributes, Option Explicit, form designer syntax, duplicate controls, startup resolution, and broken references. This is a fast structural check; use project_build with commandline for compiler errors.

| Argument | Type | Required | Description |
|---|---|---|---|
| `checkReferences` | `boolean` | no | Check for broken references. Default true. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `strict` | `boolean` | no | Treat warnings as validation failures. Default false. |

## `project_scaffold`

PREFERRED bootstrap for new VB6 applications. Create a complete on-disk project from a declarative component manifest, add form controls and references, open it in the IDE, and optionally build it in one call. Use project_create only for a minimal empty project.

| Argument | Type | Required | Description |
|---|---|---|---|
| `build` | `boolean` | no | Build after creation. Default false; command-line build is recommended for unattended work. |
| `buildMethod` | `string` | no | Build method when build=true. Allowed: `ide`, `commandline`. |
| `buildOutputPath` | `string` | no | Optional build output path when build=true. |
| `buildTimeoutSeconds` | `integer` | no | Build timeout when build=true. |
| `company` | `string` | no | Version company name. |
| `components` | `array<object>` | no | Complete component manifest. Each item is a module, class, form, user control, or property page. Form controls may be supplied inline. |
| `description` | `string` | no | Project description. |
| `directory` | `string` | no | Directory in which to create the project. Use this or vbpPath. |
| `name` | `string` | no | VB6 project name. Defaults to the file stem. |
| `open` | `boolean` | no | Open the generated project in the running VB6 IDE. Default true. |
| `overwrite` | `boolean` | no | Overwrite existing project and component files. Default false. |
| `product` | `string` | no | Version product name. |
| `references` | `array<object>` | no | References to add after the project is opened. Requires open=true. |
| `startup` | `string` | no | Startup object: a form name or the literal Sub Main. Defaults to Sub Main when a generated startup module exists. |
| `startupModule` | `boolean` | no | When components is omitted, create Module1 with Sub Main for standard EXE projects. Default true. |
| `title` | `string` | no | Display title and version description. |
| `toolboxProgIds` | `array` | no | Optional ActiveX ProgIDs to register in the VB6 toolbox after opening. |
| `type` | `string` | no | Project kind. Allowed: `standard_exe`, `activex_exe`, `activex_dll`, `activex_control`. |
| `vbpPath` | `string` | no | Exact .vbp path to create. Use this or directory. |

### `components[]`

Array item schema.

| Property | Type | Required | Description |
|---|---|---|---|
| `code` | `string` | no | Code appended after the generated/source component header. Use VB6 source syntax. |
| `controls` | `array<object>` | no | Optional controls to add to a form after its source is generated. Applied in order. |
| `fileName` | `string` | no | Optional component file name, relative to the project directory. |
| `kind` | `string` | yes | Component kind. Allowed: `form`, `class`, `module`, `usercontrol`, `propertypage`. |
| `name` | `string` | yes | Unique VB6 component name. |
| `source` | `string` | no | Optional complete component source text. When omitted, a valid VB6 skeleton is generated. |
| `title` | `string` | no | Optional form caption when source is omitted. |

### `components[].controls[]`

Array item schema.

| Property | Type | Required | Description |
|---|---|---|---|
| `container` | `string` | no | Optional parent control name for nested controls. |
| `index` | `integer` | no | Optional control-array index. |
| `name` | `string` | yes | Unique VB6 control name. |
| `properties` | `object` | no | Designer properties. Numbers and booleans are emitted unquoted; strings are quoted. |
| `type` | `string` | yes | Control type or ProgID, such as VB.CommandButton, textbox, label, frame, or MSComDlg.CommonDialog. |

### `references[]`

Array item schema.

| Property | Type | Required | Description |
|---|---|---|---|
| `guid` | `string` | no | Type-library GUID, when adding by registered GUID. |
| `major` | `integer` | no | Type-library major version. |
| `minor` | `integer` | no | Type-library minor version. |
| `path` | `string` | no | Type-library or ActiveX file path. |

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
| `type` | `string` | yes | Project kind. Allowed: `standard_exe`, `activex_exe`, `activex_dll`, `activex_control`. |
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
| `startMode` | `string` | no | Project start mode. Allowed: `standalone`, `ole_server`. |
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
| `method` | `string` | no | Build method. Allowed: `ide`, `commandline`. |
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
| `kind` | `string` | yes | Component kind. Allowed: `form`, `class`, `module`, `usercontrol`, `propertypage`, `designer`, `custom`. |
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
| `kind` | `string` | yes | Component kind. Allowed: `form`, `class`, `module`, `usercontrol`, `propertypage`. |
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
| `action` | `string` | yes | Line edit operation. Allowed: `insert`, `replace`, `delete`, `append`. |
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

## `code_search`

Search code modules across the whole project and return component, line, column, and preview text. Designer/header text is not searched; use project_inspect or form_list_controls for form metadata.

| Argument | Type | Required | Description |
|---|---|---|---|
| `component` | `string` | no | Optional component name filter. |
| `matchCase` | `boolean` | no | Use case-sensitive matching. Default false. |
| `maxResults` | `integer` | no | Maximum matches to return, 1-1000. Default 200. |
| `project` | `string` | no | Project name, VBP path, or index. Defaults to active project. |
| `query` | `string` | yes | Plain-text search string. |
| `wholeWord` | `boolean` | no | Require VB identifier boundaries around each match. Default false. |

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
| `action` | `string` | yes | Component action. Allowed: `activate`, `reload`, `saveAs`, `designerWindow`. |
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
| `action` | `string` | yes | Code pane action. Allowed: `focus`, `show`, `selection`, `topLine`. |
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
| `action` | `string` | yes | Window action. Allowed: `focus`, `show`, `hide`, `close`, `move`, `state`, `kind`, `detach`. |
| `height` | `integer` | no | Window height. |
| `kind` | `integer` | no | vbext_WindowType value for SetKind. |
| `left` | `integer` | no | Window left coordinate. |
| `state` | `string` | no | Window state. Allowed: `normal`, `minimize`, `maximize`. |
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
| `encoding` | `string` | no | Encoding override: auto, ansi, utf8, utf16le, utf16be. Allowed: `auto`, `ansi`, `utf8`, `utf16le`, `utf16be`. |
| `path` | `string` | yes | Text file path. |

## `source_write`

Atomically write a text or VB6 source file. Access is restricted to configured allowed roots and open project directories.

| Argument | Type | Required | Description |
|---|---|---|---|
| `content` | `string` | yes | File content. |
| `createDirectories` | `boolean` | no | Create parent directories. Default true. |
| `encoding` | `string` | no | Encoding override: auto, ansi, utf8, utf16le, utf16be. Allowed: `auto`, `ansi`, `utf8`, `utf16le`, `utf16be`. |
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
