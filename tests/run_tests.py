#!/usr/bin/env python3
import argparse
import http.client
import json
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "bin"
SELFTEST = BIN / "mcpaddin_selftest.exe"
ADDIN = BIN / "MCPAddin.dll"


class TestFailure(AssertionError):
    pass


def check(condition, message):
    if not condition:
        raise TestFailure(message)


def generate_certificate(directory: Path) -> Path:
    pfx = directory / "server.pfx"
    script = directory / "make-cert.ps1"
    script.write_text(
        r'''
param([string]$Pfx)
$ErrorActionPreference = "Stop"
$password = ConvertTo-SecureString -String "testpass" -Force -AsPlainText
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=localhost" `
    -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -DnsName "localhost","127.0.0.1" -NotAfter (Get-Date).AddDays(30)
try {
    Export-PfxCertificate -Cert $cert -FilePath $Pfx -Password $password | Out-Null
} finally {
    Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue
}
''',
        encoding="ascii",
    )
    subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
            "-Pfx",
            str(pfx),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    check(pfx.exists() and pfx.stat().st_size > 100, "PFX generation failed")
    return pfx


def parse_json_response(response):
    data = response.read()
    if not data:
        return None
    return json.loads(data.decode("utf-8"))


def request(host, port, method, path, body=None, token=None, headers=None, tls=False, timeout=20):
    context = ssl._create_unverified_context() if tls else None
    conn = http.client.HTTPSConnection(host, port, context=context, timeout=timeout) if tls else http.client.HTTPConnection(host, port, timeout=timeout)
    all_headers = {}
    if token:
        all_headers["Authorization"] = "Bearer " + token
    if headers:
        all_headers.update(headers)
    payload = None
    if body is not None:
        payload = json.dumps(body).encode("utf-8")
        all_headers.setdefault("Content-Type", "application/json")
    conn.request(method, path, payload, all_headers)
    response = conn.getresponse()
    raw = response.read()
    status = response.status
    response_headers = {k.lower(): v for k, v in response.getheaders()}
    conn.close()
    return status, response_headers, raw


def rpc(host, port, method, params=None, request_id=1, token=None, tls=False):
    message = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        message["params"] = params
    status, _, raw = request(
        host,
        port,
        "POST",
        "/mcp",
        message,
        token=token,
        headers={"Accept": "application/json, text/event-stream"},
        tls=tls,
    )
    check(status == 200, f"{method} returned HTTP {status}: {raw[:500]!r}")
    payload = json.loads(raw.decode("utf-8"))
    check("error" not in payload, f"{method} failed: {payload}")
    return payload["result"]


def tool(host, port, name, arguments=None, request_id=100, token=None, tls=False):
    result = rpc(
        host,
        port,
        "tools/call",
        {"name": name, "arguments": arguments or {}},
        request_id=request_id,
        token=token,
        tls=tls,
    )
    check("content" in result, f"{name} returned no content")
    if result.get("isError"):
        raise TestFailure(f"{name} returned isError: {result.get('structuredContent')}")
    return result.get("structuredContent", result)


def wait_ready(process):
    line = process.stdout.readline()
    if not line:
        stderr = process.stderr.read() if process.stderr else ""
        raise TestFailure(f"selftest did not announce readiness: {stderr}")
    return json.loads(line.decode("utf-8"))


def test_modern_http(ready, work: Path):
    host = "127.0.0.1"
    port = int(ready["http"].rsplit(":", 1)[1])
    token = ready["token"]

    status, headers, raw = request(host, port, "GET", "/health")
    check(status == 200 and json.loads(raw)["ok"] is True, "health endpoint failed")
    check(headers.get("x-content-type-options") == "nosniff", "security headers missing")

    status, _, _ = request(
        host,
        port,
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        token=None,
        headers={"Content-Type": "application/json"},
    )
    check(status == 401, f"unauthenticated MCP request returned {status}")

    status, _, _ = request(
        host,
        port,
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 1, "method": "ping", "params": {}},
        token=None,
        headers={"X-MCP-Token": token},
    )
    check(status == 200, "X-MCP-Token authentication failed")

    status, _, raw = request(
        host,
        port,
        "POST",
        "/mcp?token=" + token,
        {"jsonrpc": "2.0", "id": 2, "method": "ping", "params": {}},
        token=None,
    )
    check(status == 200 and json.loads(raw)["result"] == {}, "query-token authentication failed")

    init = rpc(
        host,
        port,
        "initialize",
        {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "mcpaddin-selftest", "version": "1.0"},
        },
        token=token,
    )
    check(init["protocolVersion"] == "2025-03-26", "protocol version negotiation failed")
    check(init["serverInfo"]["name"] == "VB6 MCP Addin", "serverInfo mismatch")
    check("tools" in init["capabilities"], "tools capability missing")

    notification = {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}
    status, _, raw = request(
        host, port, "POST", "/mcp", notification, token=token
    )
    check(status == 202 and raw == b"", "notification did not return 202")

    tools_result = rpc(host, port, "tools/list", {}, token=token)
    names = {item["name"] for item in tools_result["tools"]}
    required = {
        "project_create",
        "project_list",
        "project_add_component",
        "project_add_file",
        "component_set_code",
        "component_edit_code",
        "code_get_lines",
        "code_lines_edit",
        "code_find",
        "code_create_event",
        "code_structure",
        "component_properties_list",
        "component_read_property",
        "component_write_property",
        "component_action",
        "project_read_property",
        "project_write_property",
        "project_add_toolbox_progid",
        "code_panes_list",
        "code_pane_set",
        "windows_list",
        "window_action",
        "form_add_control",
        "project_build",
        "source_write",
    }
    check(required.issubset(names), f"missing tools: {required - names}")
    for item in tools_result["tools"]:
        check(item["inputSchema"]["type"] == "object", f"bad schema for {item['name']}")

    project_dir = work / "SimulatedProject"
    created = tool(
        host,
        port,
        "project_create",
        {
            "directory": str(project_dir),
            "name": "SimProject",
            "title": "Simulated Project",
            "description": "MCP add-in integration test",
            "type": "standard_exe",
            "startupModule": True,
        },
        token=token,
    )
    check(Path(created["vbpPath"]).exists(), "project VBP was not created")
    check(any(Path(p).exists() for p in created["filesCreated"]), "project source files were not created")

    projects = tool(host, port, "project_list", token=token)
    check(projects["count"] == 1, "project_list did not return the created project")
    project = tool(host, port, "project_get", {"project": "SimProject", "includeCode": False}, token=token)
    check(project["name"] == "SimProject", "project_get name mismatch")
    check(project["componentCount"] >= 1, "default component missing")

    module_name = project["components"][0]["name"]
    tool(
        host,
        port,
        "component_set_code",
        {
            "project": "SimProject",
            "component": module_name,
            "code": "Option Explicit\n\nPublic Sub Main()\n    Debug.Print \"hello\"\nEnd Sub\n",
        },
        token=token,
    )
    code = tool(
        host,
        port,
        "component_get_code",
        {"project": "SimProject", "component": module_name},
        token=token,
    )
    check("Debug.Print" in code["code"], "component_set_code/get_code failed")
    edited = tool(
        host,
        port,
        "component_edit_code",
        {
            "project": "SimProject",
            "component": module_name,
            "find": "Debug.Print \"hello\"",
            "replace": "Debug.Print \"edited\"",
        },
        token=token,
    )
    check(edited["replaced"] is True, "component_edit_code did not report a change")
    code = tool(
        host,
        port,
        "component_get_code",
        {"project": "SimProject", "component": module_name},
        token=token,
    )
    check("edited" in code["code"], "component_edit_code did not persist")

    lines = tool(
        host,
        port,
        "code_get_lines",
        {"project": "SimProject", "component": module_name, "startLine": 4, "count": 1},
        token=token,
    )
    check("Debug.Print" in lines["lines"], "code_get_lines failed")
    found = tool(
        host,
        port,
        "code_find",
        {"project": "SimProject", "component": module_name, "target": "Debug.Print"},
        token=token,
    )
    check(found["found"] is True and found["startLine"] == 4, "code_find failed")
    structure = tool(
        host,
        port,
        "code_structure",
        {"project": "SimProject", "component": module_name},
        token=token,
    )
    check(any(member["name"] == "Main" for member in structure["members"]),
          "code_structure did not find Main")
    tool(
        host,
        port,
        "code_lines_edit",
        {
            "project": "SimProject",
            "component": module_name,
            "action": "replace",
            "line": 4,
            "text": '    Debug.Print "line-edited"',
        },
        token=token,
    )
    tool(
        host,
        port,
        "code_lines_edit",
        {
            "project": "SimProject",
            "component": module_name,
            "action": "insert",
            "line": 4,
            "text": "    ' inserted by code_lines_edit",
        },
        token=token,
    )
    code = tool(
        host,
        port,
        "component_get_code",
        {"project": "SimProject", "component": module_name},
        token=token,
    )
    check("inserted by code_lines_edit" in code["code"] and "line-edited" in code["code"],
          "code_lines_edit insert/replace failed")
    tool(
        host,
        port,
        "code_lines_edit",
        {
            "project": "SimProject",
            "component": module_name,
            "action": "delete",
            "line": 4,
            "count": 1,
        },
        token=token,
    )
    created_event = tool(
        host,
        port,
        "code_create_event",
        {
            "project": "SimProject",
            "component": module_name,
            "eventName": "Click",
            "objectName": "Form1",
        },
        token=token,
    )
    check(created_event["created"] is True, "code_create_event failed")

    tool(
        host,
        port,
        "component_write_property",
        {
            "project": "SimProject",
            "component": module_name,
            "key": "Description",
            "value": "Updated by integration test",
        },
        token=token,
    )
    component_property = tool(
        host,
        port,
        "component_read_property",
        {"project": "SimProject", "component": module_name, "key": "Description"},
        token=token,
    )
    check("integration test" in component_property["value"], "component property tools failed")
    component_properties = tool(
        host,
        port,
        "component_properties_list",
        {"project": "SimProject", "component": module_name},
        token=token,
    )
    check(any(item["name"] == "Description" for item in component_properties["properties"]),
          "component_properties_list failed")
    component_action = tool(
        host,
        port,
        "component_action",
        {"project": "SimProject", "component": module_name, "action": "activate"},
        token=token,
    )
    check(component_action["activated"] is True, "component_action failed")

    tool(
        host,
        port,
        "project_write_property",
        {
            "project": "SimProject",
            "section": "Project",
            "key": "VersionCompanyName",
            "value": "Integration Tests",
        },
        token=token,
    )
    project_property = tool(
        host,
        port,
        "project_read_property",
        {"project": "SimProject", "section": "Project", "key": "VersionCompanyName"},
        token=token,
    )
    check(project_property["value"] == "Integration Tests", "project property tools failed")
    toolbox = tool(
        host,
        port,
        "project_add_toolbox_progid",
        {"project": "SimProject", "progId": "MSComctlLib.TreeView"},
        token=token,
    )
    check(toolbox["added"] is True, "project_add_toolbox_progid failed")

    panes = tool(host, port, "code_panes_list", token=token)
    check(panes["count"] >= 1, "code_panes_list returned no panes")
    pane = tool(
        host,
        port,
        "code_pane_set",
        {
            "pane": module_name,
            "action": "selection",
            "startLine": 1,
            "startColumn": 1,
            "endLine": 1,
            "endColumn": 8,
        },
        token=token,
    )
    check(pane["codePane"]["selection"]["endColumn"] == 8, "code_pane_set selection failed")
    windows = tool(host, port, "windows_list", token=token)
    check(windows["count"] >= 1, "windows_list returned no windows")
    window = tool(
        host,
        port,
        "window_action",
        {"window": "#1", "action": "focus"},
        token=token,
    )
    check(window["action"] == "focus", "window_action failed")

    status, _, raw = request(
        host,
        port,
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 3, "method": "ping", "params": {}},
        token=token,
    )
    check(status == 200, "baseline HTTP request failed")
    status, headers, raw = request(
        host,
        port,
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 31, "method": "ping", "params": {}},
        token=token,
        headers={"Accept": "text/event-stream"},
    )
    check(status == 200 and headers.get("content-type", "").startswith("text/event-stream"),
          "SSE-only POST response failed")
    event_text = raw.decode("utf-8")
    check("event: message" in event_text and '"result":{}' in event_text,
          "SSE-only POST payload is malformed")
    conn = http.client.HTTPConnection(host, port, timeout=10)
    body_parts = [b'{"jsonrpc":"2.0","id":4,', b'"method":"ping","params":{}}']
    conn.request(
        "POST",
        "/mcp",
        body_parts,
        {"Authorization": "Bearer " + token, "Content-Type": "application/json"},
        encode_chunked=True,
    )
    chunked = conn.getresponse()
    chunked_payload = json.loads(chunked.read().decode("utf-8"))
    check(chunked.status == 200 and chunked_payload["result"] == {}, "chunked request failed")
    conn.close()

    form_file = tool(
        host,
        port,
        "project_add_form",
        {"project": "SimProject", "name": "frmMain", "title": "Main Form"},
        token=token,
    )
    check(Path(form_file["path"]).exists(), "project_add_form did not write the form file")
    check("Form=frmMain.frm" in Path(project_dir / "SimProject.vbp").read_text(encoding="ansi"),
          "project_add_form did not update the VBP")
    designer = tool(
        host,
        port,
        "component_action",
        {"project": "SimProject", "component": "frmMain", "action": "designerWindow"},
        token=token,
    )
    check("Design" in designer["window"]["caption"], "component designerWindow action failed")
    tool(
        host,
        port,
        "form_add_control",
        {
            "project": "SimProject",
            "component": "frmMain",
            "type": "commandbutton",
            "name": "cmdOk",
            "properties": {"Caption": "OK", "Width": 1500, "Height": 500},
        },
        token=token,
    )
    controls = tool(
        host,
        port,
        "form_list_controls",
        {"project": "SimProject", "component": "frmMain"},
        token=token,
    )
    check(any(c["name"] == "cmdOk" for c in controls["controls"]), "form_add_control failed")
    tool(
        host,
        port,
        "form_set_control_property",
        {
            "project": "SimProject",
            "component": "frmMain",
            "name": "cmdOk",
            "property": "Caption",
            "value": "Continue",
        },
        token=token,
    )
    form_text = tool(
        host,
        port,
        "form_get_text",
        {"project": "SimProject", "component": "frmMain"},
        token=token,
    )["text"]
    check('"Continue"' in form_text, "form_set_control_property failed")
    tool(
        host,
        port,
        "form_remove_control",
        {"project": "SimProject", "component": "frmMain", "name": "cmdOk"},
        token=token,
    )
    controls = tool(
        host,
        port,
        "form_list_controls",
        {"project": "SimProject", "component": "frmMain"},
        token=token,
    )
    check(not any(c["name"] == "cmdOk" for c in controls["controls"]), "form_remove_control failed")

    for index in (0, 1):
        tool(
            host,
            port,
            "form_add_control",
            {
                "project": "SimProject",
                "component": "frmMain",
                "type": "VB.Label",
                "name": "lblArray",
                "index": index,
                "properties": {"Caption": f"Item {index}"},
            },
            token=token,
        )
    controls = tool(
        host,
        port,
        "form_list_controls",
        {"project": "SimProject", "component": "frmMain"},
        token=token,
    )
    array_items = [c for c in controls["controls"] if c["name"] == "lblArray"]
    check(len(array_items) == 2, "control array insertion failed")
    tool(
        host,
        port,
        "form_set_control_property",
        {
            "project": "SimProject",
            "component": "frmMain",
            "name": "lblArray",
            "index": 1,
            "property": "Caption",
            "value": "Item one",
        },
        token=token,
    )
    tool(
        host,
        port,
        "form_remove_control",
        {"project": "SimProject", "component": "frmMain", "name": "lblArray", "index": 0},
        token=token,
    )
    controls = tool(
        host,
        port,
        "form_list_controls",
        {"project": "SimProject", "component": "frmMain"},
        token=token,
    )
    check(len([c for c in controls["controls"] if c["name"] == "lblArray"]) == 1,
          "control-array removal failed")

    denied = rpc(
        host,
        port,
        "tools/call",
        {"name": "source_read", "arguments": {"path": r"C:\Windows\win.ini"}},
        request_id=55,
        token=token,
    )
    check(denied.get("isError") is True, "source_read path restriction was not enforced")

    imported_source = project_dir / "ImportedModule.bas"
    write_result = tool(
        host,
        port,
        "source_write",
        {
            "path": str(imported_source),
            "content": "Attribute VB_Name = \"ImportedModule\"\r\nOption Explicit\r\nPublic Sub Imported()\r\nEnd Sub\r\n",
            "encoding": "ansi",
        },
        token=token,
    )
    check(Path(write_result["path"]) == imported_source.resolve(), "source_write path mismatch")
    read_result = tool(host, port, "source_read", {"path": str(imported_source)}, token=token)
    check("Public Sub Imported" in read_result["content"], "source_read failed")
    tool(
        host,
        port,
        "source_edit",
        {
            "path": str(imported_source),
            "find": "Public Sub Imported()",
            "replace": "Public Sub ImportedEdited()",
        },
        token=token,
    )
    read_result = tool(host, port, "source_read", {"path": str(imported_source)}, token=token)
    check("ImportedEdited" in read_result["content"], "source_edit failed")
    listing = tool(
        host,
        port,
        "source_list",
        {"directory": str(project_dir), "pattern": "*.bas", "recursive": True},
        token=token,
    )
    listed_names = {Path(item["path"]).name for item in listing["files"]}
    check({"Module1.bas", "ImportedModule.bas"}.issubset(listed_names), "source_list wildcard failed")
    check(not any(name.endswith(".cls") for name in listed_names), "source_list pattern included .cls")
    imported = tool(
        host,
        port,
        "project_add_file",
        {"project": "SimProject", "path": str(imported_source)},
        token=token,
    )
    check(imported["name"] == "ImportedModule", "project_add_file failed")

    created_file = tool(
        host,
        port,
        "project_create_file",
        {
            "project": "SimProject",
            "kind": "class",
            "name": "AutomationClass",
            "code": "Public Function Ping() As String\n    Ping = \"pong\"\nEnd Function\n",
        },
        token=token,
    )
    check(Path(created_file["path"]).exists(), "project_create_file did not write a file")

    build = tool(host, port, "project_build", {"project": "SimProject"}, token=token)
    check(build["success"] is True, "project_build failed")
    check(Path(build["outputPath"]).exists(), "build output missing")
    saved = tool(host, port, "project_save", {"project": "SimProject"}, token=token)
    check(saved["name"] == "SimProject", "project_save failed")
    exported = tool(
        host,
        port,
        "component_export",
        {"project": "SimProject", "component": "frmMain", "path": str(project_dir / "frmMainExported.frm")},
        token=token,
    )
    check(Path(exported["exportedPath"]).exists(), "component_export failed")
    renamed = tool(
        host,
        port,
        "component_rename",
        {"project": "SimProject", "component": "AutomationClass", "newName": "AutomationRenamed"},
        token=token,
    )
    check(renamed["name"] == "AutomationRenamed", "component_rename failed")
    tool(
        host,
        port,
        "project_add_component",
        {"project": "SimProject", "kind": "module", "name": "TemporaryModule"},
        token=token,
    )
    tool(
        host,
        port,
        "project_remove_component",
        {"project": "SimProject", "component": "TemporaryModule"},
        token=token,
    )
    tool(
        host,
        port,
        "reference_add",
        {"project": "SimProject", "path": r"C:\Windows\System32\stdole2.tlb"},
        token=token,
    )
    references = tool(host, port, "references_list", {"project": "SimProject"}, token=token)
    check(len(references["references"]) == 1, "reference_add/list failed")
    tool(
        host,
        port,
        "reference_remove",
        {"project": "SimProject", "reference": r"C:\Windows\System32\stdole2.tlb"},
        token=token,
    )
    command = tool(
        host,
        port,
        "ide_execute_command",
        {"command": "Make SimProject.exe..."},
        token=token,
    )
    check(command["executed"] is True, "ide_execute_command failed")

    dll_dir = work / "SimulatedDll"
    tool(
        host,
        port,
        "project_create",
        {"directory": str(dll_dir), "name": "SimDll", "type": "activex_dll"},
        token=token,
    )
    dll_build = tool(host, port, "project_build", {"project": "SimDll"}, token=token)
    check(dll_build["outputPath"].lower().endswith(".dll"), "ActiveX DLL output extension is wrong")
    check(Path(dll_build["outputPath"]).exists(), "ActiveX DLL build output missing")

    tool(host, port, "active_project_set", {"project": "SimProject"}, token=token)
    tool(host, port, "project_close", {"project": "SimProject", "save": True}, token=token)
    projects = tool(host, port, "project_list", token=token)
    check(projects["count"] == 1 and projects["projects"][0]["name"] == "SimDll",
          "project_close failed")
    reopened = tool(host, port, "project_open", {"path": str(project_dir / "SimProject.vbp")}, token=token)
    check(reopened["name"] == "SimProject", "project_open failed")
    status = tool(host, port, "ide_status", token=token)
    check(status["transport"]["mcpEndpoint"].endswith("/mcp"), "ide_status transport merge failed")

    status, _, raw = request(
        host,
        port,
        "POST",
        "/mcp",
        {"jsonrpc": "2.0", "id": 900, "method": "tools/call",
         "params": {"name": "not_a_tool", "arguments": {}}},
        token=token,
    )
    check(status == 200, "unknown tool transport status is wrong")
    error = json.loads(raw)["error"]
    check(error["code"] == -32602, "unknown tool did not return -32602")

    status, _, raw = request(
        host,
        port,
        "POST",
        "/mcp",
        None,
        token=token,
        headers={"Content-Type": "application/json"},
    )
    check(status == 400, "empty JSON body did not return HTTP 400")

    batch = [
        {"jsonrpc": "2.0", "id": "batch-ping", "method": "ping", "params": {}},
        {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
    ]
    status, _, raw = request(host, port, "POST", "/mcp", batch, token=token)
    check(status == 200, "batch request failed")
    batch_result = json.loads(raw)
    check(isinstance(batch_result, list) and len(batch_result) == 1, "batch response filtering failed")

    status, headers, _ = request(
        host,
        port,
        "OPTIONS",
        "/mcp",
        token=token,
        headers={"Origin": "https://example.test", "Access-Control-Request-Method": "POST"},
    )
    check(status == 204 and headers.get("access-control-allow-origin") == "*", "CORS preflight failed")

    context = ssl._create_unverified_context()
    conn = http.client.HTTPConnection(host, port, timeout=10)
    conn.request("POST", "/mcp", b'{"jsonrpc":"2.0","id":1,"method":"ping","params":{}}',
                 {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    first = conn.getresponse()
    check(first.status == 200, "first keep-alive request failed")
    first.read()
    conn.request("POST", "/mcp", b'{"jsonrpc":"2.0","id":2,"method":"ping","params":{}}',
                 {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    second = conn.getresponse()
    check(second.status == 200, "second keep-alive request failed")
    second.read()
    conn.close()
    del context

    sse_conn = http.client.HTTPConnection(host, port, timeout=15)
    sse_conn.request("GET", "/sse", headers={"Authorization": "Bearer " + token,
                                             "Accept": "text/event-stream"})
    sse = sse_conn.getresponse()
    check(sse.status == 200, "SSE endpoint failed")
    endpoint_event = sse.fp.readline().decode("utf-8").strip()
    check(endpoint_event == "event: endpoint", "SSE endpoint event missing")
    endpoint_data = sse.fp.readline().decode("utf-8").strip()
    check(endpoint_data.startswith("data: /messages?sessionId="), "SSE endpoint data invalid")
    sse.fp.readline()
    endpoint_path = endpoint_data[6:]
    status, _, raw = request(
        host,
        port,
        "POST",
        endpoint_path,
        {"jsonrpc": "2.0", "id": "sse-init", "method": "initialize",
         "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "legacy-test", "version": "1"}}},
        token=token,
    )
    check(status == 202 and raw == b"", "legacy POST did not return 202")
    event_line = sse.fp.readline().decode("utf-8").strip()
    data_line = sse.fp.readline().decode("utf-8").strip()
    check(event_line == "event: message", "legacy SSE message event missing")
    check(data_line.startswith("data: "), "legacy SSE data missing")
    legacy = json.loads(data_line[6:])
    check(legacy["result"]["protocolVersion"] == "2024-11-05", "legacy initialize failed")
    sse_conn.close()


def test_https(ready):
    if not ready.get("https"):
        raise TestFailure("HTTPS endpoint was not started")
    host = "127.0.0.1"
    port = int(ready["https"].rsplit(":", 1)[1])
    token = ready["token"]
    init = rpc(host, port, "initialize",
               {"protocolVersion": "2025-06-18", "capabilities": {},
                "clientInfo": {"name": "https-test", "version": "1"}},
               token=token, tls=True)
    check(init["serverInfo"]["name"] == "VB6 MCP Addin", "HTTPS initialize failed")
    ping = rpc(host, port, "ping", {}, token=token, tls=True)
    check(ping == {}, "HTTPS ping failed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    check(SELFTEST.exists(), f"missing test runner: {SELFTEST}")
    check(ADDIN.exists(), f"missing add-in DLL: {ADDIN}")

    unit = subprocess.run([str(SELFTEST), "--unit"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if unit.returncode != 0:
        raise TestFailure(
            "unit tests failed: "
            + unit.stdout.decode("utf-8", errors="replace")
            + unit.stderr.decode("utf-8", errors="replace")
        )
    print("PASS JSON, project templates, VBP parsing, form designer edits, configuration")

    run_root = ROOT / "build" / ("test-run-" + uuid.uuid4().hex[:8])
    work = run_root / "work"
    work.mkdir(parents=True)
    pfx = generate_certificate(run_root)

    process = subprocess.Popen(
        [
            str(SELFTEST),
            "--work-dir", str(work),
            "--https",
            "--cert-file", str(pfx),
            "--cert-password", "testpass",
            "--addin-dll", str(ADDIN),
            "--run-seconds", "120",
            "--token", "integration-token",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        ready = wait_ready(process)
        check(ready.get("ready") is True, "server did not become ready")
        check(ready["comProbe"].get("ok") is True, f"COM probe failed: {ready['comProbe']}")
        check(ready["https"].startswith("https://"), "HTTPS endpoint missing")
        print("PASS server startup, HTTP/HTTPS listeners, COM class factory")
        test_modern_http(ready, work)
        print("PASS Streamable HTTP, tools, projects, components, forms, files, builds")
        test_https(ready)
        print("PASS HTTPS MCP transport")
        print("ALL TESTS PASSED")
    except Exception:
        try:
            process.terminate()
        except Exception:
            pass
        stderr = process.stderr.read().decode("utf-8", errors="replace")
        if stderr:
            print("SERVER STDERR:\n" + stderr, file=sys.stderr)
        raise
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
        if not args.keep and process.poll() is not None:
            shutil.rmtree(run_root, ignore_errors=True)


if __name__ == "__main__":
    main()
