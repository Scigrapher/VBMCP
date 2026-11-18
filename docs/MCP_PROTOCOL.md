# MCP and HTTP transport

## Endpoints

| Endpoint | Method | Purpose |
|---|---|---|
| `/mcp` | POST | MCP Streamable HTTP JSON-RPC endpoint |
| `/rpc` | POST | Alias for `/mcp` |
| `/sse` | GET | Legacy MCP HTTP+SSE endpoint |
| `/messages?sessionId=...` | POST | Legacy client-to-server messages |
| `/health` | GET | Unauthenticated liveness check |
| `/` | GET | Authenticated service information |
| `/mcp` | GET | Returns 405; use POST |
| `/mcp` | DELETE | Session termination acknowledgement |

The Streamable HTTP transport accepts JSON-RPC requests and batched requests.
Responses are `application/json` when the client accepts it. If the request's
`Accept` header offers only `text/event-stream`, the response is one
`event: message` SSE frame. Notifications return HTTP 202 with an empty body.
Empty batches and malformed JSON return JSON-RPC errors.

The server advertises protocol versions `2025-06-18`, `2025-03-26`, and
`2024-11-05`. A supported requested version is echoed. An unknown requested
version falls back to `2025-06-18`.

## Implemented MCP methods

- `initialize`
- `notifications/initialized`
- `notifications/cancelled`
- `ping`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- `resources/templates/list` (empty)
- `prompts/list`
- `prompts/get`
- `logging/setLevel` (acknowledged)

Unknown methods return JSON-RPC `-32601`. Protocol errors use standard
JSON-RPC codes. Failed tool execution returns a successful JSON-RPC response
with `isError: true`, a text content item, and structured content containing
`error` and `code`.

## MCP resources

`initialize` advertises the `resources` capability. The server exposes
read-only Markdown resources:

| URI | Purpose |
|---|---|
| `vb6://guide/ai-quickstart` | Workflow, selectors, and VB6 compatibility rules |
| `vb6://guide/build-complete-app` | Complete `project_scaffold` manifest and build flow |
| `vb6://guide/designer-compatibility` | CodeModule vs designer text, reloads, and FRX files |
| `vb6://reference/tool-groups` | Tool families and recommended workflow metadata |

`resources/read` returns:

```json
{
  "contents": [
    {
      "uri": "vb6://guide/ai-quickstart",
      "mimeType": "text/markdown",
      "text": "..."
    }
  ]
}
```

## MCP prompts

`initialize` advertises the `prompts` capability. The server exposes:

- `create-vb6-app`
- `inspect-and-extend-vb6-app`
- `fix-vb6-build`
- `create-vb6-form`

`prompts/get` returns a user-role message containing the selected workflow and
the caller's optional arguments. Prompts are guidance only; they do not execute
tools.

## VB6 form/designer compatibility

Retail VB6 does not expose `VBComponents.Import` on its automation `IDispatch`
interface. The add-in uses the documented `VBComponent.Designer` ->
`VBForm.VBControls` path for live designer control edits. Form/component file
creation falls back to writing the component source and updating the `.vbp`
directly; those results include `requiresReload: true`.

VB6 reports native form components with `VBComponent.Type = 5` (`VB.Form`),
while the VBA type library documents `vbext_ct_MSForm = 3`. Both values are
treated as forms by export, extension mapping, and component creation.

## Authentication

Every endpoint except `/health` and browser CORS preflight requires:

    Authorization: Bearer <token>

or:

    X-MCP-Token: <token>

`?token=<token>` is accepted only when `allowQueryToken=1`. Query-string tokens
can leak through proxy logs and should normally be disabled.

The token is compared in constant time. It is generated with
`SystemFunction036` (RtlGenRandom) on first configuration creation. The
generated token is stored in `mcpaddin.ini`.

## HTTP behavior

- HTTP/1.0 and HTTP/1.1
- Keep-alive and connection-close semantics
- `Content-Length` and chunked request bodies
- `Expect: 100-continue`
- Per-request and per-connection limits
- CORS preflight and configurable allow-origin
- TLS through SChannel when HTTPS is enabled

TLS uses a PFX/PKCS#12 file or a certificate/private key in a Windows
certificate store. On XP, only the TLS versions implemented by that SChannel
are available.
