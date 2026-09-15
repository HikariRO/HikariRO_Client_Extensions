@echo off
setlocal
cd /d "%~dp0"

set "SRC=%~dp0..\src\ddraw"

if defined VSCMD_ARG_TGT_ARCH goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Build Tools were not found.
    echo Install the Desktop development with C++ workload.
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
    echo The Visual Studio C++ x86 toolchain was not found.
    pause
    exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b 1

:build
cl /nologo /O2 /LD /MT /W3 /I"%SRC%" "%SRC%\ddraw.c" "%SRC%\miniz.c" "%SRC%\miniz_tdef.c" "%SRC%\miniz_tinfl.c" "%SRC%\miniz_zip.c" /link /DEF:"%SRC%\ddraw.def" /OUT:"%~dp0..\ddraw.dll" user32.lib gdi32.lib msimg32.lib ws2_32.lib
if errorlevel 1 (
    echo.
    echo DLL build failed.
    pause
    exit /b 1
)

echo.
echo Created: %~dp0..\ddraw.dll
pause
