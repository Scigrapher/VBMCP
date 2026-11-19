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
    echo Usage: uninstall.cmd [--machine^|--user]
    exit /b 2
)
shift
goto parse

:parsed
if not exist "%DLL%" (
    echo ERROR: %DLL% was not found.
    exit /b 1
)

tasklist /FI "IMAGENAME eq VB6.EXE" 2>nul | find /I "VB6.EXE" >nul
if not errorlevel 1 (
    echo Close Visual Basic 6 before uninstalling the add-in.
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
        echo Requesting elevation to remove the machine-wide COM registration...
        "%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%elevate.ps1" ^
            -FilePath "%~f0" -Arguments "--machine --from-elevation"
        if errorlevel 1 goto ElevationFailed
    )
)

REM Remove this account's per-user registration as well. In the elevated-child
REM path, this runs after machine cleanup and covers the invoking user.
"%REGSVR%" /s /u "%DLL%"
if errorlevel 1 (
    echo ERROR: COM unregistration failed.
    exit /b 1
)

call :VerifyUnregistered
if errorlevel 1 exit /b 1

echo Unregistered MCPAddin.
echo Configuration and logs under %APPDATA%\MCPAddin were left intact.
exit /b 0

:NeedAdmin
echo ERROR: Administrator rights are required to remove the machine-wide
echo 32-bit COM registration.
exit /b 5

:ElevationFailed
echo ERROR: Elevated unregistration failed.
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

:VerifyUnregistered
setlocal EnableExtensions
set "REG32=/reg:32"
if /I "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set "REG32="

if /I "%SCOPE%"=="machine" (
    reg query "HKLM\SOFTWARE\Classes\CLSID\{7B1B6F2E-6F65-4E5F-A5B2-4B19E1B8C001}" /ve %REG32% >nul 2>&1
    if not errorlevel 1 (
        echo ERROR: Machine-wide COM registration is still present.
        endlocal
        exit /b 1
    )
)

reg query "HKCU\Software\Microsoft\Visual Basic\6.0\Addins\MCPAddin.Connect" /v LoadBehavior %REG32% >nul 2>&1
if not errorlevel 1 (
    echo ERROR: The current user's VB6 Add-In Manager entry is still present.
    endlocal
    exit /b 1
)

endlocal
exit /b 0
