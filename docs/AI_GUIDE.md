# AI guide to the VB6 MCP server

This guide is for an AI session that needs to create, understand, or modify a
Visual Basic 6 application through MCPAddin.

The central idea is simple:

1. discover the IDE and project;
2. inspect before reading every file;
3. edit at the right layer (code, designer, or project file);
4. validate structure;
5. compile with captured diagnostics;
6. verify the result.

## 1. Connect and discover

The default MCP endpoint is:

```text
http://HOST:8765/mcp
Authorization: Bearer <token>
```

Call `ide_status` first. It returns:

- the active project and selected component;
- the MCP endpoints and server version;
- `toolGroups`, listing every available tool family;
- `recommendedWorkflow`, a machine-readable build sequence;
- `aiGuide`, the resource URI to read next.

If your client supports MCP resources, read:

```text
vb6://guide/ai-quickstart
```

The other resources are:

```text
vb6://guide/build-complete-app
vb6://guide/designer-compatibility
vb6://reference/tool-groups
```

## 2. Tool map

| Need | Preferred tools |
|---|---|
| Check the IDE | `ide_status`, `project_list`, `project_get` |
| Understand a project | `project_inspect`, `code_search`, `code_structure` |
| Start a new app | `project_scaffold` |
| Read code | `component_get_code`, `code_get_lines`, `code_find` |
| Change code | `component_set_code`, `component_edit_code`, `code_lines_edit` |
| Add an event handler | `code_create_event` |
| Work with forms | `form_get_text`, `form_set_text`, `form_list_controls`, `form_add_control`, `form_remove_control`, `form_set_control_property` |
| Add/rename/remove components | `project_add_component`, `project_add_form`, `project_create_file`, `component_rename`, `project_remove_component` |
| Manage references | `references_list`, `reference_add`, `reference_remove` |
| Validate and build | `project_validate`, `project_build` |
| Do many operations at once | `vb6_batch` |
| Read/write supporting files | `source_read`, `source_write`, `source_edit`, `source_list` |

Project and component selectors accept a VBIDE name, a `.vbp` path, or a 1-based
index (`1` or `#1`). Omit `project` to target the active project.

## 3. Golden path: create a complete application

Use `project_scaffold` for the initial application. It creates the `.vbp`, every
component file, optional form controls, opens the project, applies references,
and can build immediately.

```json
{
  "directory": "C:\\Projects\\Contacts",
  "name": "Contacts",
  "title": "Contact Manager",
  "description": "Small contact manager",
  "type": "standard_exe",
  "startup": "Sub Main",
  "components": [
    {
      "kind": "module",
      "name": "AppMain",
      "code": "Public Sub Main()\r\n    frmContacts.Show\r\nEnd Sub"
    },
    {
      "kind": "class",
      "name": "Contact",
      "code": "Public FullName As String\r\nPublic Email As String"
    },
    {
      "kind": "class",
      "name": "ContactStore",
      "code": "Private mItems As Collection\r\n\r\nPrivate Sub Class_Initialize()\r\n    Set mItems = New Collection\r\nEnd Sub\r\n\r\nPublic Sub Add(ByVal value As Contact)\r\n    mItems.Add value\r\nEnd Sub\r\n\r\nPublic Property Get Count() As Long\r\n    Count = mItems.Count\r\nEnd Property"
    },
    {
      "kind": "form",
      "name": "frmContacts",
      "title": "Contacts",
      "controls": [
        {
          "type": "listbox",
          "name": "lstContacts",
          "properties": {"Left": 240, "Top": 240, "Width": 3600, "Height": 2400}
        },
        {
          "type": "textbox",
          "name": "txtName",
          "properties": {"Left": 240, "Top": 2880, "Width": 2400}
        },
        {
          "type": "commandbutton",
          "name": "cmdAdd",
          "properties": {"Caption": "Add", "Left": 2880, "Top": 2880}
        }
      ],
      "code": "Private mStore As New ContactStore\r\n\r\nPrivate Sub cmdAdd_Click()\r\n    Dim item As New Contact\r\n    item.FullName = txtName.Text\r\n    mStore.Add item\r\n    lstContacts.AddItem item.FullName\r\nEnd Sub"
    }
  ],
  "build": true,
  "buildMethod": "commandline"
}
```

Important scaffold fields:

- `components` replaces individual component calls for the initial build-out.
- `source` supplies complete source for an unusual component.
- `code` appends code after the generated header; use VB6 syntax.
- `controls` is applied only to forms.
- `references` uses `path` or `guid`/`major`/`minor` and requires `open=true`.
- `toolboxProgIds` registers ActiveX controls in the toolbox.
- Set `buildMethod="commandline"` for unattended compiler diagnostics.

After scaffolding:

1. call `project_inspect`;
2. implement remaining behavior;
3. call `project_validate`;
4. call `project_build` with `method="commandline"`;
5. inspect `success`, `outputPath`, and `compilerOutput`.

## 4. Golden path: understand or modify an existing application

Do not start by dumping every component. Use this sequence:

1. `ide_status`
2. `project_list`
3. `project_inspect` with `includeMembers=true` and
   `includeControls=true`
4. `code_search` for relevant symbols or strings
5. `code_get_lines` for the exact implementation

Example:

```json
{
  "project": "Calculator",
  "includeMembers": true,
  "includeControls": true
}
```

Then:

```json
{
  "project": "Calculator",
  "query": "cmdEquals_Click",
  "matchCase": false,
  "maxResults": 20
}
```

Read only the handler and its dependencies. Preserve existing behavior unless
the task explicitly changes it.

## 5. Golden path: forms and controls

Remember the two layers:

- `CodeModule` tools edit code only.
- `form_*` and `component_replace_file` tools edit complete designer/component
  text.

Safe form sequence:

1. `form_list_controls`
2. `form_add_control` / `form_remove_control` /
   `form_set_control_property`
3. `form_list_controls` again
4. `project_inspect` to verify the project-wide map

For a new form, use `project_scaffold` (new project) or `project_add_form`
(existing project). Add controls with inline `controls` or individual tools.

This is invalid:

```text
component_set_code on a form, trying to insert "Begin VB.CommandButton ..."
```

That call edits the code section, not the designer header. Use `form_*` or
`component_replace_file`.

## 6. Golden path: batch many independent operations

`vb6_batch` runs tools sequentially on the VB6 UI thread:

```json
{
  "stopOnError": true,
  "calls": [
    {
      "tool": "form_add_control",
      "arguments": {
        "project": "Contacts",
        "component": "frmContacts",
        "type": "label",
        "name": "lblName",
        "properties": {"Caption": "Name", "Left": 240, "Top": 2640}
      }
    },
    {
      "tool": "form_add_control",
      "arguments": {
        "project": "Contacts",
        "component": "frmContacts",
        "type": "commandbutton",
        "name": "cmdDelete",
        "properties": {"Caption": "Delete", "Left": 2880, "Top": 3120}
      }
    },
    {
      "tool": "form_list_controls",
      "arguments": {"project": "Contacts", "component": "frmContacts"}
    }
  ]
}
```

Batch semantics:

- there is no transaction rollback;
- calls see the effects of earlier calls;
- `stopOnError=true` is the safe default;
- the response includes per-call `ok`, `result`, and error codes;
- `vb6_batch` cannot call itself.

## 7. Validation and build

`project_validate` checks:

- `.vbp` parse and component entries;
- missing component files;
- `Attribute VB_Name`;
- `Option Explicit`;
- form designer syntax and duplicate control names/indexes;
- startup resolution;
- broken references.

It does not resolve types or compile code. The authoritative final check is:

```json
{
  "project": "Contacts",
  "method": "commandline",
  "timeoutSeconds": 300
}
```

The build result contains:

- `success`
- `exitCode`
- `outputPath`
- `outputExists`
- `compilerOutput`
- `compilerLogPath`, when a persistent log path was supplied

If the build fails, search the first compiler diagnostic, read the smallest
relevant code range, fix it, then rebuild.

## 8. VB6 constraints to respect

- Use valid identifiers: start with a letter, then letters, digits, or `_`.
- Add `Option Explicit` and declare variables deliberately.
- Prefer CRLF for persisted VB6 source.
- `VBComponents.Import` is unreliable or absent in retail VB6. A file-backed
  result may include `requiresReload=true`; reload the project in the IDE.
- Preserve `.frx` files associated with forms.
- Binary designer properties cannot be inferred from `.frm` text alone.
- Control arrays use one name with unique `Index` values.
- Do not close the main IDE window during unattended automation.
- The in-IDE build can open a modal compiler dialog on errors. Use the
  command-line build for unattended work.

## 9. Security and safety

This server can control a developer's live IDE. Treat it as a privileged
automation endpoint:

- protect the token like a password;
- prefer localhost, VPN, or firewall allow-listing;
- do not expose query-token authentication to the Internet;
- do not delete or replace components without an explicit task requirement;
- prefer additive edits and backups when changing existing projects.

## 10. Useful examples with the bundled client

```powershell
python .\examples\mcp_client.py --list
python .\examples\mcp_client.py ide_status "{}"
python .\examples\mcp_client.py project_inspect '{"project":"Calculator"}'
python .\examples\mcp_client.py code_search '{"project":"Calculator","query":"cmdEquals"}'
```

The generated complete schema is in [`MCP_TOOLS.md`](MCP_TOOLS.md).
