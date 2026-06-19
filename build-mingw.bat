@echo off
REM Alternative build using mingw-w64 (GCC for Windows).

setlocal
where gcc >nul 2>&1
if errorlevel 1 (
    echo.
    echo gcc not found on PATH. Install mingw-w64 or use build.bat instead.
    exit /b 1
)

if not exist build mkdir build

gcc -O2 -Wall -Wextra -municode -mwindows ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN ^
    main.c overlay.c audio.c detector.c fft.c profiles.c settings.c ^
    -o build\SoundOverlay.exe ^
    -luser32 -lgdi32 -lole32 -loleaut32 -lavrt -lcomctl32 -luuid -lshell32

if errorlevel 1 (
    echo.
    echo *** Build failed. ***
    exit /b 1
)

echo.
echo Built: build\SoundOverlay.exe
endlocal
