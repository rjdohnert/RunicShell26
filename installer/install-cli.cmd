@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "TARGET_DIR=%SystemRoot%\System32"

cd /d "%PROJECT_ROOT%"

if not exist "%PROJECT_ROOT%\rsh.exe" (
    echo ERROR: rsh.exe not found in %PROJECT_ROOT%
    exit /b 1
)

if not exist "%PROJECT_ROOT%\LICENSE.txt" (
    echo ERROR: LICENSE.txt not found in %PROJECT_ROOT%
    exit /b 1
)

call :ensure_admin %*
if errorlevel 1 exit /b 1

echo.
echo ================================================
echo RunicShell26 License Agreement
echo ================================================
echo.
more "%PROJECT_ROOT%\LICENSE.txt"
echo.
set /p ACCEPT=Type YES to accept the license and continue installation: 
if /I not "%ACCEPT%"=="YES" (
    echo License was not accepted. Installation canceled.
    exit /b 1
)

echo.
echo Installing to %TARGET_DIR% ...
copy /Y "%PROJECT_ROOT%\rsh.exe" "%TARGET_DIR%\rsh.exe" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy rsh.exe to %TARGET_DIR%
    exit /b 1
)

copy /Y "%PROJECT_ROOT%\LICENSE.txt" "%TARGET_DIR%\License.txt" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy LICENSE.txt to %TARGET_DIR%\License.txt
    exit /b 1
)

echo Installation completed successfully.
echo Installed files:
echo   %TARGET_DIR%\rsh.exe
echo   %TARGET_DIR%\License.txt
exit /b 0

:ensure_admin
net session >nul 2>&1
if %errorlevel%==0 exit /b 0

if /I "%~1"=="elevated" (
    echo ERROR: Administrator privileges are required.
    exit /b 1
)

echo Requesting administrator privileges...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%PROJECT_ROOT%' -ArgumentList 'elevated'"
exit /b 1
