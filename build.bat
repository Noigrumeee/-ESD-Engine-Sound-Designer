@echo off
rem Builds Engine Sound Designer (Release) with MSVC + Ninja.
rem Usage: build.bat [Release|Debug]
rem Extra CMake arguments: set ESD_CMAKE_ARGS=-DESD_BUILD_APP=OFF  (batch splits arguments on '=')
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
  echo Visual Studio 2022 C++ build tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

cd /d "%~dp0"
"%CMAKE%" -S . -B build\%CONFIG% -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=%CONFIG% %ESD_CMAKE_ARGS% || exit /b 1
"%CMAKE%" --build build\%CONFIG% || exit /b 1
echo.
echo Build OK: build\%CONFIG%
