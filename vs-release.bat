@echo off
rem ============================================================
rem  vs-release.bat - Configure and build tagledger with Visual
rem  Studio (MSVC) in Release. Output: build-vs\Release\
rem  Usage: vs-release.bat   (requires Visual Studio 2019 or newer)
rem ============================================================
setlocal
cd /d "%~dp0"

echo [1/2] Configuring (Visual Studio generator, Release) ...
cmake -S . -B build-vs -G "Visual Studio 16 2019" -A x64
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [2/2] Building (MSBuild parallel) ...
cmake --build build-vs --config Release -- /m /v:m /nologo
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo Build OK (Release). Output: build-vs\Release\tagledger.exe, tagledger_tests.exe
endlocal
