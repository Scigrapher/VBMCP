#!/usr/bin/env python3
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SELFTEST = ROOT / "bin" / "mcpaddin_selftest.exe"
OUTPUT = ROOT / "docs" / "MCP_TOOLS.md"


def main():
    process = subprocess.Popen(
        [str(SELFTEST), "--run-seconds", "20", "--token", "doc-token"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        ready = json.loads(process.stdout.readline())
        host, port = ready["http"].split("//", 1)[1].split(":", 1)
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/list",
            "params": {},
        }
        import http.client

        conn = http.client.HTTPConnection(host, int(port), timeout=10)
        conn.request(
            "POST",
            "/mcp",
            json.dumps(request).encode(),
            {
                "Content-Type": "application/json",
                "Authorization": "Bearer doc-token",
            },
        )
        response = conn.getresponse()
        payload = json.loads(response.read())
        conn.close()
        tools = payload["result"]["tools"]

        lines = [
            "# MCP tool reference",
            "",
            "Generated from `tools/list` by `tests/generate_tool_reference.py`.",
            "",
            "For AI-oriented workflow guidance, read MCP resource",
            "`vb6://guide/ai-quickstart` or see `docs/AI_GUIDE.md`.",
            "",
            "Start new applications with `project_scaffold`; use `project_inspect`,",
            "`code_search`, and `project_validate` to understand and verify existing",
            "projects; use `vb6_batch` for multiple sequential operations.",
            "",
            "All tools accept a JSON object as `arguments`. Current project/component",
            "selectors accept the VBIDE name, VBP path, or a 1-based numeric index.",
            "",
            "## Tool index",
            "",
            "| Tool | Purpose |",
            "|---|---|",
        ]
        for tool in tools:
            description = tool["description"].replace("|", "\\|")
            lines.append(f"| [`{tool['name']}`](#{tool['name']}) | {description} |")
        lines.append("")

        def emit_schema(path, schema, heading):
            properties = schema.get("properties", {})
            if not properties:
                return
            required = set(schema.get("required", []))
            lines.append(f"### `{path}`")
            lines.append("")
            lines.append(f"{heading}")
            lines.append("")
            lines.append("| Property | Type | Required | Description |")
            lines.append("|---|---|---|---|")
            nested = []
            for name in sorted(properties):
                prop = properties[name]
                typ = prop.get("type", "any")
                item = prop.get("items", {})
                if typ == "array" and item.get("type") == "object":
                    typ = "array<object>"
                description = prop.get("description", "")
                if prop.get("enum"):
                    description += " Allowed: " + ", ".join(
                        f"`{value}`" for value in prop["enum"]
                    ) + "."
                description = description.replace("|", "\\|")
                lines.append(
                    f"| `{name}` | `{typ}` | {'yes' if name in required else 'no'} | {description} |"
                )
                if prop.get("type") == "object" and prop.get("properties"):
                    nested.append((f"{path}.{name}", prop, "Nested object schema."))
                if item.get("type") == "object" and item.get("properties"):
                    nested.append((f"{path}.{name}[]", item, "Array item schema."))
            lines.append("")
            for nested_path, nested_schema, nested_heading in nested:
                emit_schema(nested_path, nested_schema, nested_heading)

        for tool in tools:
            lines.append(f"## `{tool['name']}`")
            lines.append("")
            lines.append(tool["description"])
            lines.append("")
            schema = tool.get("inputSchema", {})
            properties = schema.get("properties", {})
            required = set(schema.get("required", []))
            if properties:
                lines.append("| Argument | Type | Required | Description |")
                lines.append("|---|---|---|---|")
                for name in sorted(properties):
                    prop = properties[name]
                    typ = prop.get("type", "any")
                    if typ == "array" and prop.get("items", {}).get("type") == "object":
                        typ = "array<object>"
                    description = prop.get("description", "")
                    if prop.get("enum"):
                        description += " Allowed: " + ", ".join(
                            f"`{value}`" for value in prop["enum"]
                        ) + "."
                    description = description.replace("|", "\\|")
                    lines.append(
                        f"| `{name}` | `{typ}` | {'yes' if name in required else 'no'} | {description} |"
                    )
                lines.append("")
                for name in sorted(properties):
                    prop = properties[name]
                    item = prop.get("items", {})
                    if prop.get("type") == "object" and prop.get("properties"):
                        emit_schema(name, prop, "Nested object schema.")
                    if item.get("type") == "object" and item.get("properties"):
                        emit_schema(f"{name}[]", item, "Array item schema.")
            else:
                lines.append("No arguments.")
                lines.append("")
        OUTPUT.write_text("\n".join(lines), encoding="utf-8")
        print(f"Wrote {OUTPUT}")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    main()
