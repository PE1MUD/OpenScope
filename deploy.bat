@echo off
setlocal

cd /d "%~dp0"

set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"

if not exist "%VSDEVCMD%" (
    echo ERROR: Visual Studio Developer Command Prompt not found:
    echo %VSDEVCMD%
    pause
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64
if errorlevel 1 exit /b 1

echo.
echo === Configure Release ===
cmake --preset release
if errorlevel 1 goto :error

echo.
echo === Build Release ===
cmake --build out\build\release
if errorlevel 1 goto :error

echo.
echo === Clean deploy directory ===
if exist out\deploy (
    rmdir /s /q out\deploy
)

echo.
echo === Create deploy ===
cmake --install out\build\release --prefix out\deploy
if errorlevel 1 goto :error

echo.
echo === Copy ROM set definitions ===
if exist "romsets" (
    robocopy "romsets" "out\deploy\bin\romsets" /E /NFL /NDL /NJH /NJS /NP
    if errorlevel 8 goto :error
) else (
    echo WARNING: romsets directory not found.
)

echo.
echo === OpenScope deploy ready ===
echo %CD%\out\deploy\bin
echo.
pause
exit /b 0

:error
echo.
echo *** DEPLOY FAILED ***
echo.
pause
exit /b 1