@echo off
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEV="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSDEV=%%i\Common7\Tools\VsDevCmd.bat"
    )
)
if not defined VSDEV set "VSDEV=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEV%" (
    echo Visual Studio C++ tools were not found.
    exit /b 1
)

call "%VSDEV%" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1

set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
where cmake >nul 2>&1
if exist "%CMAKE%" (
    set "CMAKE_EXE=%CMAKE%"
) else (
    set "CMAKE_EXE=cmake"
)

if not exist "build\CMakeCache.txt" (
    "%CMAKE_EXE%" -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)

"%CMAKE_EXE%" --build build --target RoboUIDesigner --config Release
if errorlevel 1 exit /b 1
"%CMAKE_EXE%" --build build --target myui --config Release
exit /b %errorlevel%
