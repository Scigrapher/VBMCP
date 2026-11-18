# Windows XP deployment

## Supported baseline

The binary is x86 and is linked with an OS/subsystem version of 5.01. The code
uses Windows XP SP2 APIs and later service-pack behavior only where required
(`EncodePointer`/`DecodePointer`). Windows XP SP3 is the recommended baseline.

The build performs an import-table check and rejects any known post-XP
`kernel32.dll` imports. The static Visual C++ runtime's Vista-era primitives
are supplied by `src/compat_xp.cpp`.

## Install

1. Copy the `MCPAddin` directory to the VB6 machine. Keep `bin\MCPAddin.dll`.
2. Open a command prompt.
3. Run:

       install.cmd

   This registers the COM class under the current user's
   `HKCU\Software\Classes` and writes the VB6 add-in entry under
   `HKCU\Software\Microsoft\Visual Basic\6.0\Addins\MCPAddin.Connect`.
   Administrator rights are only needed if you want a machine-wide
   registration and firewall changes.
4. Start VB6. Open **Add-Ins > Add-In Manager**, select
   **VB6 MCP Add-in (HTTP/HTTPS)** and choose **Loaded/Unloaded** and
   **Load on Startup**.
5. The first load creates `%APPDATA%\MCPAddin\mcpaddin.ini`. Read the generated
   `[auth] token` value.

If registration fails, make sure you used the 32-bit `regsvr32.exe`. On 64-bit
Windows the installer selects `%WINDIR%\SysWOW64\regsvr32.exe`; on XP it uses
`%WINDIR%\System32\regsvr32.exe`.

## Firewall

The server does not change firewall policy. On XP SP2+:

    netsh firewall add portopening TCP 8765 "MCPAddin HTTP"

For HTTPS, also open `8766` if that is the configured port.

Do not expose the server to untrusted networks. A strong bearer token is
generated on first load. If you deliberately clear it and set
`allowUnauthenticatedRemote=1`, anyone who can reach the port can control the
IDE.

## HTTPS on XP

Windows XP's SChannel implementation can host HTTPS through the add-in, but it
only provides the TLS/SSL versions supported by that OS (typically TLS 1.0 and,
depending on configuration, SSL 3.0). Current Java, Python, Go, Node, and .NET
clients normally reject those old protocol versions by default.

There are two practical options:

1. Use HTTP with a strong bearer token on a trusted network/VPN.
2. Terminate modern TLS at a reverse proxy, stunnel, VPN gateway, or load
   balancer and forward plain HTTP to the add-in over a private network.

On a newer Windows machine, the add-in's built-in SChannel server can negotiate
modern TLS with the same configuration. To generate a development PFX:

    powershell -NoProfile -ExecutionPolicy Bypass -File make-cert.ps1 `
        -OutFile C:\MCPAddin\server.pfx -Password change-me

Then set:

    [tls]
    certificateFile=C:\MCPAddin\server.pfx
    certificatePassword=change-me

and set `enableHttps=1` in `[server]`.

For a certificate in the Windows store, populate `certificateThumbprint` and
optionally `certificateStore` (default `MY`). The private key must be readable
by the VB6 user.

## Command-line builds on XP

The default `project_build` path uses the live IDE's `MakeCompiledFile`
automation method. This keeps the build in the already-running IDE.

`method="commandline"` runs:

    VB6.EXE /make "project.vbp" /out "compiler.log"

The add-in finds `VB6.EXE` from the `vb6Exe` tool argument, the `[ide] vb6Exe`
configuration value, `VB6EXE` environment variable, `PATH`, and the
`ProductDir` value under:

    HKLM\SOFTWARE\Microsoft\VisualStudio\6.0\Setup\Microsoft Visual Basic

Command-line builds provide a compiler log and avoid an error dialog in the
live IDE, but opening a second VB6 compiler instance while the IDE is busy can
be inappropriate for some projects. Use the default IDE method in that case.
