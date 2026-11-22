@echo off
setlocal
cd /d "%~dp0"

if not exist "build\mega.exe" (
    call "%~dp0build_windows.bat"
    if errorlevel 1 exit /b 1
)

if exist "loot_tables.cfg" copy /Y "loot_tables.cfg" "build\loot_tables.cfg" >nul
start "" "%~dp0build\mega.exe" %*
