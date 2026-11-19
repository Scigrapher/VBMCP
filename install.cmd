@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "DLL=%ROOT%bin\MCPAddin.dll"
set "SCOPE=machine"
set "ELEVATION_ATTEMPTED=0"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--machine" (
    set "SCOPE=machine"
) else if /I "%~1"=="--user" (
    set "SCOPE=user"
) else if /I "%~1"=="--per-user" (
    set "SCOPE=user"
) else if /I "%~1"=="--from-elevation" (
    set "ELEVATION_ATTEMPTED=1"
) else (
    echo ERROR: Unknown option: %~1
    echo Usage: install.cmd [--machine^|--user]
    exit /b 2
)
shift
goto parse

:parsed
if not exist "%DLL%" (
    echo ERROR: %DLL% was not found.
    echo Run build.cmd first.
    exit /b 1
)

set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" set "REGSVR=%SystemRoot%\SysWOW64\regsvr32.exe"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" set "POWERSHELL=powershell.exe"

if /I "%SCOPE%"=="machine" (
    call :IsAdmin
    if errorlevel 1 (
        if "%ELEVATION_ATTEMPTED%"=="1" goto NeedAdmin
        if not exist "%ROOT%elevate.ps1" goto NeedAdmin
        echo Administrator rights are required for the machine-wide COM registration
        echo used by an elevated VB6 IDE. Requesting elevation...
        "%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%elevate.ps1" ^
            -FilePath "%~f0" -Arguments "--machine --from-elevation"
        if errorlevel 1 goto ElevationFailed
    )
)

REM Register this account's per-user entry as well. In the elevated-child path,
REM this runs after machine registration and covers the original invoking user.
"%REGSVR%" /s "%DLL%"
if errorlevel 1 (
    echo ERROR: COM registration failed.
    exit /b 1
)

call :VerifyRegistration
if errorlevel 1 exit /b 1

echo Installed: %DLL%
if /I "%SCOPE%"=="machine" (
    echo Scope: all users ^(32-bit machine registration^)
) else (
    echo Scope: current user only
    echo WARNING: Elevated VB6 does not load per-user COM registrations.
)
echo.
echo The first load creates:
echo   %APPDATA%\MCPAddin\mcpaddin.ini
echo   %APPDATA%\MCPAddin\mcpaddin.log
echo.
echo Restart VB6, then use Add-Ins ^> Add-In Manager to load
echo   "VB6 MCP Add-in (HTTP/HTTPS)".
echo.
echo To allow remote connections through Windows Firewall, open the configured ports
echo manually. The default HTTP port is 8765 and HTTPS is 8766.
exit /b 0

:NeedAdmin
echo ERROR: Administrator rights are required to register the 32-bit COM class
echo machine-wide. Elevated VB6 cannot load a per-user COM registration.
exit /b 5

:ElevationFailed
echo.
echo Installation was not completed for elevated VB6. Per-user COM registration
echo is present, but elevated VB6 cannot load it.
exit /b 5

:IsAdmin
set "IS_ADMIN=0"
fsutil dirty query %SystemDrive% >nul 2>&1
if not errorlevel 1 set "IS_ADMIN=1"
if "%IS_ADMIN%"=="0" (
    net session >nul 2>&1
    if not errorlevel 1 set "IS_ADMIN=1"
)
if "%IS_ADMIN%"=="1" exit /b 0
exit /b 1

:VerifyRegistration
setlocal EnableExtensions
set "REG32=/reg:32"
if /I "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set "REG32="

if /I "%SCOPE%"=="machine" (
    reg query "HKLM\SOFTWARE\Classes\CLSID\{7B1B6F2E-6F65-4E5F-A5B2-4B19E1B8C001}\InprocServer32" /ve %REG32% >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Machine-wide 32-bit COM registration was not created.
        echo Run install.cmd from an elevated command prompt.
        endlocal
        exit /b 1
    )
) else (
    reg query "HKCU\Software\Classes\CLSID\{7B1B6F2E-6F65-4E5F-A5B2-4B19E1B8C001}\InprocServer32" /ve %REG32% >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Per-user 32-bit COM registration was not created.
        endlocal
        exit /b 1
    )
)

reg query "HKCU\Software\Microsoft\Visual Basic\6.0\Addins\MCPAddin.Connect" /v LoadBehavior %REG32% >nul 2>&1
if errorlevel 1 (
    echo ERROR: The VB6 Add-In Manager entry was not created for the current user.
    endlocal
    exit /b 1
)

endlocal
exit /b 0
