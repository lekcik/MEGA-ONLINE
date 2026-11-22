@echo off
setlocal enabledelayedexpansion
echo === Building MEGA (1v1 Zero-Dep P2P Edition) for Windows ===
echo SDL3 is automatically downloaded and statically linked via CMake.
echo No manual SDL3, libepoxy, or vcpkg setup required!
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake was not found in PATH.
    echo Install CMake from https://cmake.org/download/ and ensure a C++20 compiler
    echo ^(Visual Studio Build Tools / VS Community OR MinGW-w64 g++^) is installed.
    exit /b 1
)

where cl >nul 2>nul
if %errorlevel%==0 (
    echo Using MSVC / Visual Studio toolchain
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :fail
    cmake --build build --config Release
    if errorlevel 1 goto :fail
    echo.
    if exist "build\Release\mega.exe" (
        echo Build complete! Run: build\Release\mega.exe
    ) else (
        echo Build complete! Run: build\mega.exe
    )
    goto :eof
)

where g++ >nul 2>nul
if %errorlevel%==0 (
    echo Using MinGW g++ toolchain
    cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release 2>nul || cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :fail
    cmake --build build
    if errorlevel 1 goto :fail
    echo.
    echo Build complete! Run: build\mega.exe
    goto :eof
)

echo Trying default CMake Visual Studio generator...
cmake -B build
if errorlevel 1 goto :no_compiler
cmake --build build --config Release
if errorlevel 1 goto :fail
echo.
echo Build complete! Run: build\Release\mega.exe
goto :eof

:no_compiler
echo.
echo [ERROR] No C++20 compiler detected.
echo Install either:
echo   1. Visual Studio Build Tools ^(Desktop development with C++^) from https://visualstudio.microsoft.com/downloads/
echo   OR
echo   2. WinLibs / MinGW-w64 g++ + CMake
exit /b 1

:fail
echo Build failed! Check CMake/compiler output above.
exit /b 1
