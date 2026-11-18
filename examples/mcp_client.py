#!/usr/bin/env python3
"""Small command-line MCP client for MCPAddin."""

import argparse
import configparser
import json
import os
import ssl
import sys
import urllib.request


def read_token():
    config_path = os.path.join(os.environ.get("APPDATA", ""), "MCPAddin", "mcpaddin.ini")
    parser = configparser.ConfigParser()
    parser.read(config_path)
    return parser.get("auth", "token", fallback="")


def post(url, token, payload, insecure=False):
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": "Bearer " + token,
        },
        method="POST",
    )
    context = ssl._create_unverified_context() if insecure else None
    with urllib.request.urlopen(request, context=context, timeout=60) as response:
        raw = response.read()
        if not raw:
            return None
        return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8765/mcp")
    parser.add_argument("--token", default=read_token())
    parser.add_argument("--insecure", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--resources", action="store_true")
    parser.add_argument("--prompts", action="store_true")
    parser.add_argument("--read-resource")
    parser.add_argument("--prompt")
    parser.add_argument("--prompt-arguments", default="{}")
    parser.add_argument("tool", nargs="?")
    parser.add_argument("arguments", nargs="?", default="{}")
    args = parser.parse_args()

    if not args.token:
        print("No token. Pass --token or configure MCPAddin first.", file=sys.stderr)
        return 2

    initialize = post(
        args.url,
        args.token,
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "mcpaddin-example", "version": "1.0"},
            },
        },
        args.insecure,
    )
    print(json.dumps(initialize, indent=2))

    if args.list:
        tools = post(
            args.url,
            args.token,
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
            args.insecure,
        )
        print(json.dumps(tools, indent=2))
        return 0

    if args.resources:
        resources = post(
            args.url,
            args.token,
            {"jsonrpc": "2.0", "id": 2, "method": "resources/list", "params": {}},
            args.insecure,
        )
        print(json.dumps(resources, indent=2))
        return 0

    if args.read_resource:
        resource = post(
            args.url,
            args.token,
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "resources/read",
                "params": {"uri": args.read_resource},
            },
            args.insecure,
        )
        print(json.dumps(resource, indent=2))
        return 0

    if args.prompts:
        prompts = post(
            args.url,
            args.token,
            {"jsonrpc": "2.0", "id": 2, "method": "prompts/list", "params": {}},
            args.insecure,
        )
        print(json.dumps(prompts, indent=2))
        return 0

    if args.prompt:
        prompt = post(
            args.url,
            args.token,
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "prompts/get",
                "params": {
                    "name": args.prompt,
                    "arguments": json.loads(args.prompt_arguments),
                },
            },
            args.insecure,
        )
        print(json.dumps(prompt, indent=2))
        return 0

    if args.tool:
        result = post(
            args.url,
            args.token,
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": args.tool,
                    "arguments": json.loads(args.arguments),
                },
            },
            args.insecure,
        )
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
