@echo off
setlocal

set "DLL=%~dp0bin\MCPAddin.dll"
if not exist "%DLL%" (
    echo ERROR: %DLL% was not found.
    exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" set "REGSVR=%SystemRoot%\SysWOW64\regsvr32.exe"

tasklist /FI "IMAGENAME eq VB6.EXE" 2>nul | find /I "VB6.EXE" >nul
if not errorlevel 1 (
    echo Close Visual Basic 6 before uninstalling the add-in.
    exit /b 1
)

"%REGSVR%" /s /u "%DLL%"
if errorlevel 1 (
    echo ERROR: Unregistration failed.
    exit /b 1
)

echo Unregistered MCPAddin.
echo Configuration and logs under %APPDATA%\MCPAddin were left intact.
exit /b 0
