@echo off
setlocal

set "W64DEVKIT=D:\tools\w64devkit"
set "PATH=%W64DEVKIT%\bin;%PATH%"
set "DEPS_PREFIX=%~dp0build\dependencies\windows-x86\install"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cmake\build-windows-dependencies.ps1" -W64DevkitRoot "%W64DEVKIT%"
if errorlevel 1 exit /b %errorlevel%

"%W64DEVKIT%\bin\cmake.exe" --preset windows-xp-x86-release -DITOOL_ENABLE_UPX=ON -DITOOL_WX_ROOT="%DEPS_PREFIX%" -DITOOL_CRYPTOPP_ROOT="%DEPS_PREFIX%" %*
if errorlevel 1 exit /b %errorlevel%

"%W64DEVKIT%\bin\cmake.exe" --build --preset windows-xp-x86-release
if errorlevel 1 exit /b %errorlevel%

"%W64DEVKIT%\bin\ctest.exe" --test-dir "%~dp0build\windows-x86-release" --output-on-failure
if errorlevel 1 exit /b %errorlevel%

set /p ARTIFACT_NAME=<"%~dp0build\windows-x86-release\generated\artifact-name.txt"
echo.
echo Built: build\windows-x86-release\bin\%ARTIFACT_NAME%.exe
