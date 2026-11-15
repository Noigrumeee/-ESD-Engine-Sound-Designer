@echo off
rem Downloads the third-party sources into external\ (exact versions used by the project).
rem Needs git and curl (both ship with Windows 10/11 + Git for Windows).
setlocal
cd /d "%~dp0"
if not exist external mkdir external
cd external
if not exist JUCE   git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE || exit /b 1
if not exist Catch2 git clone --depth 1 --branch v3.7.1 https://github.com/catchorg/Catch2.git Catch2 || exit /b 1
if not exist json   git clone --depth 1 --branch v3.11.3 https://github.com/nlohmann/json.git json || exit /b 1
if not exist sqlite (
  curl -sSLf -o sqlite.zip https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip || exit /b 1
  tar -xf sqlite.zip || exit /b 1
  ren sqlite-amalgamation-3490100 sqlite
  del sqlite.zip
)
echo Dependencies ready in external\
