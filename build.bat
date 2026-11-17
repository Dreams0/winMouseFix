@echo off
setlocal
cd /d "%~dp0"

set "GXX="
where g++ >nul 2>nul && set "GXX=g++"
if not defined GXX if exist "%LOCALAPPDATA%\winlibs\mingw64\bin\g++.exe" set "GXX=%LOCALAPPDATA%\winlibs\mingw64\bin\g++.exe"
if not defined GXX (
    echo [ERROR] g++ was not found.
    echo         Install MinGW-w64 ^(e.g. WinLibs^) and put g++ in PATH,
    echo         or extract it to %%LOCALAPPDATA%%\winlibs\mingw64.
    exit /b 1
)

for %%I in ("%GXX%") do set "TOOLBIN=%%~dpI"
set "WRES=%TOOLBIN%windres.exe"
if not exist "%WRES%" set "WRES=windres"
if not exist "res\app.ico" (
    echo [INFO] res\app.ico is missing; generating it.
    py -3 tools\make_icon.py res\app.ico || exit /b 1
)

if not exist build mkdir build
if not exist dist mkdir dist

"%WRES%" -I src -I res res\app.rc -O coff -o build\app.res || exit /b 1

"%GXX%" -std=c++20 -O2 -s -DNDEBUG -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 ^
    -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra ^
    -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va ^
    src\main.cpp src\gui.cpp src\hook.cpp src\smoother.cpp ^
    src\engine.cpp src\settings.cpp src\util.cpp ^
    build\app.res -o dist\winMouseFix.exe ^
    -luser32 -lgdi32 -ladvapi32 -lshell32 -lcomctl32 -lwinmm -lole32 || exit /b 1

if exist README.md copy /y README.md dist\README.md >nul

echo.
echo Built: dist\winMouseFix.exe
endlocal
