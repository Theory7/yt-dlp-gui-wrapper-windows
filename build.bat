@echo off
rem Build yt-dlp GUI with the GCC toolchain that is on PATH.
setlocal enabledelayedexpansion
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [error] g++ was not found on PATH.
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
    echo [error] windres was not found on PATH ^(part of the GCC/binutils install^).
    exit /b 1
)

if not exist build mkdir build

echo [1/2] compiling resources
windres -I res res\app.rc -O coff -o build\app.res
if errorlevel 1 exit /b 1

echo [2/2] compiling and linking
g++ -std=c++17 -O2 -municode -mwindows ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -fno-rtti ^
    -static -static-libgcc -static-libstdc++ ^
    -s ^
    -specs=res\no-default-manifest.spec ^
    src\main.cpp src\ui.cpp src\options.cpp src\tools.cpp src\util.cpp src\json.cpp src\fluent.cpp ^
    build\app.res ^
    -o build\yt-dlp-gui.exe ^
    -lcomctl32 -luxtheme -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lgdi32 -luser32 -ladvapi32 -lcomdlg32
if errorlevel 1 (
    echo [error] build failed
    exit /b 1
)

echo.
echo Built build\yt-dlp-gui.exe
for %%F in (build\yt-dlp-gui.exe) do echo Size: %%~zF bytes
exit /b 0
