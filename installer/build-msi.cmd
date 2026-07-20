@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

if not exist "..\rsh.exe" (
    echo ERROR: Missing ..\rsh.exe. Build rsh.exe before creating MSI.
    exit /b 1
)

where wix.exe >nul 2>&1
if %errorlevel%==0 (
    echo Building MSI with wix.exe...
    wix build -arch x64 -ext WixToolset.UI.wixext -o RunicShell26-x64.msi rjdsh26.wxs
    if errorlevel 1 exit /b 1
    echo MSI created: %SCRIPT_DIR%RunicShell26-x64.msi
    exit /b 0
)

where candle.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: No supported WiX toolchain found.
    echo        Install WiX v4+ (wix.exe) or WiX v3 (candle.exe/light.exe).
    exit /b 1
)

where light.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: light.exe not found. Install WiX Toolset v3 and add it to PATH.
    exit /b 1
)

echo Building WiX object with candle.exe...
candle.exe -nologo -arch x64 -ext WixUIExtension rjdsh26.wxs
if errorlevel 1 exit /b 1

echo Linking MSI with light.exe...
light.exe -nologo -ext WixUIExtension -cultures:en-us -o RunicShell26-x64.msi rjdsh26.wixobj
if errorlevel 1 exit /b 1

echo MSI created: %SCRIPT_DIR%RunicShell26-x64.msi
exit /b 0
