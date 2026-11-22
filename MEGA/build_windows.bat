@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo === MEGA // Zero-Dependency Windows Build & Launch ===

set "GPP="
where g++ >nul 2>nul
if %errorlevel%==0 (
    for /f "delims=" %%I in ('where g++') do (
        set "GPP=%%I"
        goto :found_gpp
    )
)

for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do (
    if exist "%%~fD\mingw64\bin\g++.exe" (
        set "GPP=%%~fD\mingw64\bin\g++.exe"
        set "PATH=%%~fD\mingw64\bin;%PATH%"
        goto :found_gpp
    )
)

for %%P in (
    "C:\msys64\mingw64\bin"
    "C:\msys64\ucrt64\bin"
    "C:\mingw64\bin"
    "C:\WinLibs\mingw64\bin"
) do (
    if exist "%%~P\g++.exe" (
        set "GPP=%%~P\g++.exe"
        set "PATH=%%~P;%PATH%"
        goto :found_gpp
    )
)

echo Compiler not found locally. Installing WinLibs GCC automatically via winget...
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --accept-package-agreements --accept-source-agreements
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do (
    if exist "%%~fD\mingw64\bin\g++.exe" (
        set "GPP=%%~fD\mingw64\bin\g++.exe"
        set "PATH=%%~fD\mingw64\bin;%PATH%"
        goto :found_gpp
    )
)

echo.
echo [ERROR] Could not locate or install g++.exe automatically.
pause
exit /b 1

:found_gpp
echo Using compiler: "%GPP%"
if not exist "build" mkdir "build"

echo Compiling MEGA (no external downloads required)...
"%GPP%" -std=c++20 -O2 -Isrc ^
    src/main.cpp ^
    src/core/Platform.cpp ^
    src/net/NetSession.cpp ^
    src/render/GlApi.cpp ^
    src/render/Shader.cpp ^
    src/render/Framebuffer.cpp ^
    src/render/TextureAtlas.cpp ^
    src/render/WorldMesher.cpp ^
    src/render/BillboardRenderer.cpp ^
    src/render/RobotRenderer.cpp ^
    src/render/HudRenderer.cpp ^
    src/world/WorldGrid.cpp ^
    src/world/LightBaker.cpp ^
    src/world/gen/MegaGenerator.cpp ^
    src/game/Player.cpp ^
    src/game/GameWorld.cpp ^
    src/game/Inventory3D.cpp ^
    src/game/LockpickMinigame.cpp ^
    src/audio/AudioEngine.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -lopengl32 -lws2_32 -lwinmm -lgdi32 -luser32 ^
    -o build\mega.exe

if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

if exist "loot_tables.cfg" copy /Y "loot_tables.cfg" "build\loot_tables.cfg" >nul
echo Build succeeded: build\mega.exe
exit /b 0
