@echo off
REM Build SoundOverlay.exe with MSVC (cl.exe).
REM Run from a Visual Studio "x64 Native Tools Command Prompt".

setlocal
set OUT=SoundOverlay.exe

where cl >nul 2>&1
if errorlevel 1 (
    echo.
    echo cl.exe not found on PATH. Open a "Developer Command Prompt for VS"
    echo or run vcvars64.bat first, then re-run build.bat.
    exit /b 1
)

if not exist build mkdir build
pushd build

cl /nologo /O2 /W3 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /I.. ^
   ../main.c ../overlay.c ../audio.c ../detector.c ../fft.c ../profiles.c ../settings.c ^
   /Fe%OUT% /link /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib ole32.lib oleaut32.lib ^
   avrt.lib comctl32.lib uuid.lib shell32.lib
if errorlevel 1 (
    popd
    echo.
    echo *** Build failed. ***
    exit /b 1
)

echo.
echo Built: build\%OUT%
popd
endlocal
