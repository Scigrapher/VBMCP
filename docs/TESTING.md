# Testing and verification

## Reproduce

```powershell
.\build.ps1 -Clean -Tests
python .\tests\run_tests.py
```

The test runner uses `tests\mock_ide.cpp` as a deterministic simulated VB6
IDE. It does not locate or launch VB6.

## What is covered

| Area | Coverage |
|---|---|
| Build | x86 DLL, OS/subsystem 5.01, reviewed XP kernel import allow-list |
| COM | `DllGetClassObject`, `IClassFactory::CreateInstance`, `IDTExtensibility2` QI |
| Registration | per-user CLSID/ProgID and VB6 AddIn Manager registry values |
| HTTP | keep-alive, chunked bodies, authentication, CORS, HEAD/health |
| HTTPS | SChannel listener, PFX loading, TLS MCP request/response |
| MCP | initialize, ping, tools/list, tools/call, batches, notifications |
| MCP guidance | resources/list/read, prompts/list/get, `ide_status` workflow metadata |
| Legacy MCP | `/sse` endpoint event, `/messages` POST, SSE response event |
| Projects | create, scaffold, inspect, validate, open, close, save, list, get, active project |
| Components | add, import, create file, list, get, rename, export, remove |
| Project properties | VBP section/key read/write, toolbox ProgID registration |
| Components | property-bag list/read/write, activate/reload/save-as/designer |
| Code | get, replace, find/replace, project-wide search, line read/edit, find, events, members |
| IDE UI | windows list/action, code panes list/selection |
| Forms | designer export, control add/remove, properties, control arrays |
| Files | allow-root enforcement, read/write/edit/list, wildcard recursion |
| Builds | EXE and ActiveX DLL output resolution and output-file creation |
| Orchestration | sequential `vb6_batch`, stop-on-error, partial-result reporting |
| Unit | JSON, VBP parsing, templates, configuration/token generation, path safety |

## XP import verification

The static UCRT built by Visual Studio 2022 references several Vista+ APIs.
`src\compat_xp.cpp` supplies XP-compatible implementations for:

- `InitializeCriticalSectionEx`
- `FlsAlloc`, `FlsFree`, `FlsGetValue`, `FlsSetValue`
- `AcquireSRWLockExclusive`, `ReleaseSRWLockExclusive`
- `SleepConditionVariableSRW`, `WakeAllConditionVariable`

The linker uses a reviewed import library generated from
`src\kernel32_symbols.txt`. The build then checks every final `KERNEL32.dll`
import against `src\xp_kernel32_allowed.txt`; an unreviewed import fails the
build.

The resulting DLL has:

```text
machine:              x86
operating system:     5.01
subsystem version:    5.01
```

## Manual registration check

The self-test executable also exposes non-destructive-to-source probes:

```powershell
.\bin\mcpaddin_selftest.exe --register-probe --addin-dll (Resolve-Path .\bin\MCPAddin.dll)
.\bin\mcpaddin_selftest.exe --unregister-probe --addin-dll (Resolve-Path .\bin\MCPAddin.dll)
```

These write the same per-user registry values as `install.cmd` and then remove
them. Run the unregister probe if running the register probe on a machine where
the add-in was not previously installed.

## Real VB6 validation

Actual VBIDE automation cannot be executed in this workspace because VB6 is
not installed and was intentionally not sought, downloaded, or used. The
production `ComIdeBackend` implements the documented VBIDE late-bound object
model and is isolated from the MCP/HTTP code by `IIdeBackend`. The simulated
backend exercises the complete transport and tool pipeline.

On a VB6 test machine, the first smoke sequence should be:

1. Load the add-in.
2. Read the generated token and call `ide_status`.
3. Read `vb6://guide/ai-quickstart` with `resources/read`.
4. Call `project_scaffold` with a startup module, class, and form containing
   two controls; set `"build": true` and `"method": "commandline"`.
5. Call `project_inspect`, `code_search`, and `project_validate`.
6. Use `vb6_batch` to add another control and an event handler.
7. Exercise `code_get_lines`, `code_find`, `code_structure`, and
   `code_create_event`.
8. Exercise `code_panes_list`, `code_pane_set`, `windows_list`, and
   `window_action`.
9. Call `project_build` with `"method": "commandline"` and inspect the log.
