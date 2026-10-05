@echo off
setlocal
cd /d "%~dp0"

if "%~1"=="" (
    echo Usage: OpenComponent.bat ^<component-folder^>
    echo The folder name is the component name. It must contain that name as .xml, .h, and .cpp.
    exit /b 1
)

set "FOLDER=%~f1"
if "%FOLDER:~-1%"=="\" set "FOLDER=%FOLDER:~0,-1%"
if not exist "%FOLDER%\" (
    echo Folder not found: %FOLDER%
    exit /b 1
)

for %%I in ("%FOLDER%") do set "NAME=%%~nxI"
set "XML=%FOLDER%\%NAME%.xml"
set "HEADER=%FOLDER%\%NAME%.h"
set "SOURCE=%FOLDER%\%NAME%.cpp"

if not exist "%XML%" (
    echo Missing %NAME%.xml
    exit /b 1
)
if not exist "%HEADER%" (
    echo Missing %NAME%.h
    exit /b 1
)
if not exist "%SOURCE%" (
    echo Missing %NAME%.cpp
    exit /b 1
)

set "DESIGNER=%~dp0build\RoboUIDesigner.exe"
if not exist "%DESIGNER%" (
    call "%~dp0BuildDesigner.bat"
    if errorlevel 1 exit /b 1
)

start "" "%DESIGNER%" "%FOLDER%"
exit /b 0
