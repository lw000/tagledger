@echo off
rem ============================================================
rem  cmake-debug.bat - Configure and build tagledger in Debug
rem  Usage:   cmake-debug.bat [jobs]   (default jobs: 4)
rem  Output:  build\tagledger.exe, build\tagledger_tests.exe
rem ============================================================
setlocal
cd /d "%~dp0"

set JOBS=%1
if "%JOBS%"=="" set JOBS=4

echo [1/2] Configuring build type: Debug ...
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [2/2] Building with %JOBS% parallel jobs ...
cmake --build build -j %JOBS%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo Build OK (Debug). Output: build\tagledger.exe, build\tagledger_tests.exe
endlocal
