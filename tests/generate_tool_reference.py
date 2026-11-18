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
            "All tools accept a JSON object as `arguments`. Current project/component",
            "selectors accept the VBIDE name, VBP path, or a 1-based numeric index.",
            "",
        ]
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
                    description = prop.get("description", "").replace("|", "\\|")
                    lines.append(
                        f"| `{name}` | `{typ}` | {'yes' if name in required else 'no'} | {description} |"
                    )
                lines.append("")
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
