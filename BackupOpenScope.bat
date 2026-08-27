@echo off
setlocal

rem OpenScope source snapshot
rem Put this BAT file in the repository root.
rem Output: YYYYMMDD_HHMMSS.zip in the repository root.

cd /d "%~dp0"

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%I"
set "ZIP=%CD%\%STAMP%.zip"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = (Get-Location).Path;" ^
  "$items = @();" ^
  "$dirs = @('src','cmake','resources','assets','romsets');" ^
  "foreach ($d in $dirs) { $p = Join-Path $root $d; if (Test-Path $p) { $items += $p } };" ^
  "$files = @('CMakeLists.txt','vcpkg.json','vcpkg-configuration.json','README_0.8.4.md');" ^
  "foreach ($f in $files) { $p = Join-Path $root $f; if (Test-Path $p) { $items += $p } };" ^
  "$items += @(Get-ChildItem -Path $root -File -Filter '*.cmake' -ErrorAction SilentlyContinue | ForEach-Object FullName);" ^
  "if ($items.Count -eq 0) { throw 'Nothing found to archive.' };" ^
  "Compress-Archive -Path $items -DestinationPath '%ZIP%' -CompressionLevel Optimal -Force"

if errorlevel 1 (
    echo.
    echo ERROR: ZIP maken mislukt.
    pause
    exit /b 1
)

echo.
echo Gemaakt:
echo %ZIP%
pause
