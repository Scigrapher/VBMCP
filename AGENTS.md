# MCPAddin repository guide

## Scope

This directory is the native VB6 IDE MCP server. Keep it deployable on Windows
XP SP3 and newer, self-contained, and usable by both humans and AI MCP clients.

## Read before changing code

1. `README.md`
2. `docs/AI_GUIDE.md`
3. `docs/MCP_PROTOCOL.md`
4. `src/tools.h`, `src/tools.cpp`
5. `src/com_ide.h`, relevant parts of `src/com_ide.cpp`
6. `tests/mock_ide.cpp` and `tests/run_tests.py`

## Architecture contract

- All VBIDE `IDispatch` calls run on the VB6 UI thread through
  `ComIdeBackend::ExecuteTool`.
- Network, HTTP, TLS, and JSON work stay off the UI thread.
- Do not add ATL, MFC, .NET, or a non-XP runtime dependency.
- Keep production and mock backend behavior aligned.
- Preserve the `/mcp`, `/rpc`, legacy `/sse`, `/messages`, and `/health`
  transport behavior.

## Add a tool checklist

1. Add the tool schema in `src/tools.cpp`.
2. Add the tool name to `GetToolGroups()`.
3. Declare and implement the method in `src/com_ide.{h,cpp}`.
4. Dispatch it from `ComIdeBackend::ExecuteTool`.
5. Implement the same behavior in `tests/mock_ide.{h,cpp}` and its `Dispatch`.
6. Add integration coverage in `tests/run_tests.py`.
7. Update `README.md`, `docs/AI_GUIDE.md`, and `docs/MCP_PROTOCOL.md` when the
   workflow or protocol changes.
8. Regenerate `docs/MCP_TOOLS.md`.

## Build and verify

```powershell
.\build.ps1 -Clean -Tests
python .\tests\run_tests.py
python .\tests\generate_tool_reference.py
```

Do not leave the generated tool reference stale. The build must retain:

- x86 output;
- OS/subsystem version 5.01;
- passing XP kernel import allow-list check;
- no compiler warnings if avoidable.

## Style and safety

- Use C++14 and the existing Win32/COM helper abstractions.
- Return `ToolResult::Failure(message, machine_readable_code)` for expected
  errors.
- Keep structured results compact and stable; prefer file/component selectors
  and line ranges over dumping entire projects.
- Never assume a form designer is open. Use the existing live-designer and
  file-backed fallback paths.
- Do not remove or replace existing user projects during tests.
- Treat `requiresReload` and `.frx` preservation as compatibility-critical.
