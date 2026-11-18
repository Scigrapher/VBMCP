# MCPAddin

`MCPAddin` is a native x86 COM add-in for Visual Basic 6 that exposes the live
VB6 IDE as a Model Context Protocol server over HTTP or HTTPS. It is designed
for automation from a remote MCP harness: create/open projects, add components
and files, read and edit code, manipulate form designer text and controls,
manage references, save, and build.

The repository contains:

- A compiled, statically linked `bin\MCPAddin.dll`
- Complete C++ source
- XP-compatible install/uninstall scripts
- A simulated VB6 IDE test runner
- An end-to-end HTTP/HTTPS/MCP test suite

No VB6 installation, Visual Basic runtime, or Microsoft add-in designer DLL is
required to build or run the repository tests. A real VB6 installation is only
required to exercise the actual IDE connection.

## Architecture

The add-in is a single in-process ActiveX DLL implementing
`IDTExtensibility2`. It has no dependency on `MSADDNDR.DLL`, ATL, MFC, .NET, or
the Visual C++ runtime at deployment time.

```text
Remote MCP client
       |
       |  HTTP / HTTPS (bearer token)
       v
MCPAddin.dll  ---- worker threads: sockets, TLS, HTTP, JSON-RPC
       |
       |  PostMessage to a hidden window on the VB6 UI thread
       v
VB6 IDE STA  ---- IDispatch / VBIDE automation
       |
       +---- projects, VBComponents, CodeModule, References, build
       +---- Windows, CodePanes, project/component property bags
       +---- form designer text export/modify/re-import
```

Network work never executes `IDispatch` calls directly. All VBIDE work is
marshaled to the thread that called `OnConnection`, preserving the VB6 COM
apartment and avoiding cross-thread automation.

## Requirements

Runtime:

- Windows XP SP2+ (XP SP3 recommended), Windows Vista/7/8/10/11
- Visual Basic 6.0 IDE
- 32-bit COM registration
- A token in `%APPDATA%\MCPAddin\mcpaddin.ini`

Build/test:

- Windows
- Visual Studio 2022 C++ x86 tools (or an equivalent MSVC x86 toolset)
- Windows SDK
- Python 3 for the integration suite

## Build

From a normal command prompt:

```bat
build.cmd -Clean -Tests
```

or from PowerShell:

```powershell
.\build.ps1 -Clean -Tests
```

Outputs:

```text
bin\MCPAddin.dll
bin\mcpaddin_selftest.exe
```

The build script:

1. Compiles all C++ as static `/MT` x86 code.
2. Generates an XP-compatible `kernel32` import library.
3. Supplies the CRT's Vista-era `Fls*`, SRW-lock, condition-variable, and
   `InitializeCriticalSectionEx` primitives.
4. Links with OS/subsystem version 5.01.
5. Verifies that no known post-XP kernel imports remain.

## Test

```powershell
python .\tests\run_tests.py
```

The suite starts the add-in core with a simulated IDE and tests:

- COM class factory and `IDTExtensibility2` query
- HTTP and HTTPS listeners
- bearer-token and query-token authentication
- MCP initialize, ping, tools/list, tools/call, batching, and notifications
- Streamable HTTP, keep-alive, chunked requests, and legacy SSE
- project creation/opening/saving/listing/build outputs
- class, module, form, and user-project file creation
- code read/write/find/replace
- line-level code read/edit, native `CodeModule.Find`, event creation, and
  member structure discovery
- form control add/remove/property editing
- IDE window and code-pane listing/control
- component property-bag access and activate/reload/save-as/designer actions
- project VBP section/key properties and toolbox ProgID registration
- source file read/write/edit/list and allowed-root enforcement
- VBP parsing, JSON, configuration generation, and project templates

The actual DLL is loaded by the self-test to verify its exports and COM class
factory. The simulated IDE intentionally drives the same MCP/tool pipeline that
the live VB6 backend uses. Real VBIDE automation is isolated behind
`ComIdeBackend`, because the task environment does not include VB6.

## Install

Run as the user who will run VB6:

```bat
install.cmd
```

Then start VB6 and enable **VB6 MCP Add-in (HTTP/HTTPS)** in
**Add-Ins > Add-In Manager**.

The first load creates:

```text
%APPDATA%\MCPAddin\mcpaddin.ini
%APPDATA%\MCPAddin\mcpaddin.log
```

See `docs\DEPLOY_XP.md` for XP-specific details, firewall commands, and HTTPS.

## Default endpoints

```text
POST http://host:8765/mcp
GET  http://host:8765/sse          legacy MCP SSE transport
GET  http://host:8765/health
```

HTTPS is disabled by default until a certificate is configured. When enabled,
the default HTTPS port is 8766.

All MCP endpoints require:

```http
Authorization: Bearer <token>
```

`X-MCP-Token: <token>` is also accepted. `/health` is intentionally
unauthenticated and returns only minimal liveness information.

## Configuration

The INI file is read on add-in load. A complete example is
`mcpaddin.example.ini`. Important settings:

| Setting | Meaning |
|---|---|
| `bindAddress` | `0.0.0.0` for remote access, `127.0.0.1` for local only |
| `httpPort` / `httpsPort` | Listener ports |
| `enableHttp` / `enableHttps` | Transport switches |
| `authToken` | Bearer token; generated automatically if empty |
| `allowQueryToken` | Permit `?token=...`; off by default |
| `corsOrigin` | `*` or an exact allowed browser origin; blank disables CORS |
| `maxBodyBytes` | Request body cap (default 16 MiB) |
| `ideTimeoutSeconds` | How long a network request waits for the VB6 UI thread |
| `certificateFile` | PFX/PKCS#12 path |
| `certificatePassword` | PFX password |
| `certificateThumbprint` | Alternative Windows-store certificate selector |
| `allowedRoots` | Semicolon-separated roots for source file tools |

Open project directories are automatically added to the source-tool allowed
roots. `source_*` tools never accept arbitrary paths outside those roots.

## Tool coverage

See `docs\MCP_TOOLS.md` for the generated schema reference. The functional
groups are:

- IDE: detailed status/add-in discovery, windows, code panes, active project,
  CommandBar execution
- Projects: create, open, close, save, inspect, set automation properties,
  read/write VBP section properties, register toolbox controls, build
- Components: add built-in/designer components, import files, create files,
  remove, rename, export, list, inspect, enumerate component properties,
  read/write property-bag values, activate/reload/save-as/open designer
- Code: read, replace, find/replace, line-range read/edit, native find with
  positions, event-procedure creation, member/structure discovery
- Code panes: list, focus/show, selection, and top-line scrolling
- Windows: list captions/types/bounds and focus/show/hide/close/move/state/kind/
  detach actions
- Forms/designers: export/rewrite/import, list controls, add/remove controls,
  set properties, control arrays
- References: list/add/remove
- Source files: allow-root-constrained read/write/edit/list

`examples\mcp_client.py` is a minimal client:

```powershell
python .\examples\mcp_client.py --list
python .\examples\mcp_client.py project_list "{}"
```

Project type values:

```text
standard_exe
activex_exe
activex_dll
activex_control
```

Component kind values:

```text
form
class
module
usercontrol
propertypage
designer
custom
```

## VB6 integration notes

The VBIDE automation model is late-bound and called by name, so the add-in does
not require a VB6-specific type library at compile time. It uses the documented
VBIDE object model:

- `VBE.VBProjects`
- `VBE.Windows`, `VBE.CodePanes`, `VBE.Addins`
- `VBProject.VBComponents`, `SaveAs`, `MakeCompiledFile`, `ReadProperty`,
  `WriteProperty`, `AddToolboxProgID`
- `VBComponents.Add`, `AddCustom`, `Import`, `Remove`
- `VBComponent.CodeModule`, `Export`, `Properties`, `ReadProperty`,
  `WriteProperty`, `Activate`, `Reload`, `SaveAs`, `DesignerWindow`
- `CodeModule.Lines`, `InsertLines`, `ReplaceLine`, `DeleteLines`, `Find`,
  `CreateEventProc`, `Members`
- `References.AddFromFile`, `AddFromGuid`, `Remove`

Some IDE operations are inherently heuristic:

- Retail VB6 does not expose `VBComponents.Import` through its automation
  `IDispatch` interface, and `VBProjects.Open` is absent in some VB6 builds.
  VB6 also reports native forms as component type `5` (`VB.Form`), whereas the
  VBA type library documents `vbext_ct_MSForm` as `3`; the add-in recognizes
  both.
  `project_add_form` therefore writes the `.frm` and updates the `.vbp`
  directly. `project_create_file`, `project_add_file`, and form text/control
  edits automatically use the same file-backed fallback when native import is
  unavailable. Reload the project in VB6 after a fallback edit; affected tool
  results include `"requiresReload": true`.
- When a VB6 form designer is already open, control edits use the documented
  extensibility path `VBComponent.Designer` -> `VBForm.VBControls.Add` and set
  values through the control's `Properties` collection. The file-backed path is
  used only when the designer object or requested container is unavailable.
- `VBForm`/`VBControls` is a late-bound designer API and does not cover every
  container/property combination. When it cannot handle an operation, form
  edits export the `.frm`, modify its textual `Begin ... End` blocks, then
  re-import it or write the modified text back directly on VB6. Complex binary
  properties stored in `.frx` remain in the companion `.frx` file and are
  preserved.
- `CodeModule` edits the code section of forms/classes/modules, not the form
  designer header. Use the `form_*` tools or `component_replace_file` for the
  full component file.
- An in-IDE build can display a modal compiler diagnostic on source errors.
  For unattended builds, use `project_build` with
  `"method": "commandline"` to capture a compiler log.
- Window and code-pane actions are design-time IDE operations. `window_action`
  can close a window and `component_action` can activate or reload a designer;
  avoid closing the main IDE window during unattended automation.
- VBIDE has no project-level title property in the automation object. The title
  is stored in the `.vbp`; `project_set_properties` reports it as unsupported
  rather than silently modifying a separate copy.

## Security

This is a remote IDE-control service. Treat its token like a password.

- The default bind is `0.0.0.0` so a remote harness can reach the machine.
- A 256-bit token is generated on first load.
- Remote access with an empty token is refused by configuration validation.
- Prefer a VPN/private network and firewall allow-listing.
- Do not enable query-token authentication on an Internet-facing endpoint.
- Use HTTPS when the network is not trusted. On XP, modern TLS may require a
  TLS-terminating proxy because XP SChannel is limited to old protocol
  versions.

## Layout

```text
MCPAddin/
  bin/                 compiled DLL and test runner
  src/                 production C++ sources
  tests/               simulated IDE and end-to-end tests
  docs/                protocol, XP deployment, generated tools
  build.ps1/.cmd       reproducible build
  install.cmd          per-user COM/add-in registration
  uninstall.cmd        cleanup
  make-cert.ps1        development PFX helper
  mcpaddin.example.ini configuration reference
```
