@echo off
setlocal

set "DLL=%~dp0bin\MCPAddin.dll"
if not exist "%DLL%" (
    echo ERROR: %DLL% was not found.
    echo Run build.cmd first.
    exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" set "REGSVR=%SystemRoot%\SysWOW64\regsvr32.exe"

"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
    echo ERROR: COM registration failed.
    echo Run this script from an account allowed to write HKEY_CURRENT_USER\Software\Classes.
    exit /b 1
)

echo Installed: %DLL%
echo.
echo The first load creates:
echo   %APPDATA%\MCPAddin\mcpaddin.ini
echo   %APPDATA%\MCPAddin\mcpaddin.log
echo.
echo Start VB6, then use Add-Ins ^> Add-In Manager to load
echo   "VB6 MCP Add-in (HTTP/HTTPS)".
echo.
echo To install for every user, run this script as an administrator.
echo To allow remote connections through Windows Firewall, open the configured ports
echo manually. The default HTTP port is 8765 and HTTPS is 8766.
exit /b 0
