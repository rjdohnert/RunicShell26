@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "TARGET_DIR=%SystemRoot%\System32"
set "RSH_FILE=%TARGET_DIR%\rsh.exe"
set "LICENSE_FILE=%TARGET_DIR%\License.txt"

cd /d "%PROJECT_ROOT%"

call :ensure_admin %*
if errorlevel 1 exit /b 1

echo.
echo ================================================
echo RunicShell26 Uninstall
echo ================================================
echo This will remove:
echo   %RSH_FILE%
echo   %LICENSE_FILE%
echo.
set /p CONFIRM=Type YES to continue uninstall: 
if /I not "%CONFIRM%"=="YES" (
    echo Uninstall canceled.
    exit /b 1
)

set "FAILED=0"

if exist "%RSH_FILE%" (
    del /F /Q "%RSH_FILE%" >nul 2>&1
    if exist "%RSH_FILE%" (
        echo ERROR: Failed to remove %RSH_FILE%
        set "FAILED=1"
    ) else (
        echo Removed %RSH_FILE%
    )
) else (
    echo Not found %RSH_FILE%
)

if exist "%LICENSE_FILE%" (
    del /F /Q "%LICENSE_FILE%" >nul 2>&1
    if exist "%LICENSE_FILE%" (
        echo ERROR: Failed to remove %LICENSE_FILE%
        set "FAILED=1"
    ) else (
        echo Removed %LICENSE_FILE%
    )
) else (
    echo Not found %LICENSE_FILE%
)

if "%FAILED%"=="1" (
    echo Uninstall completed with errors.
    exit /b 1
)

echo Uninstall completed successfully.
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
