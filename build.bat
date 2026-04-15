@echo off
:: ============================================================
:: build.bat – Compile Sound Overlay to a standalone Windows EXE
::
:: Requirements:
::   pip install -r requirements.txt
::
:: Output:
::   dist\SoundOverlay.exe   (single-file, no console window)
:: ============================================================

echo.
echo  Building Sound Detection Game Overlay...
echo.

pyinstaller ^
    --onefile ^
    --noconsole ^
    --name SoundOverlay ^
    --icon NONE ^
    --add-data "config.py;." ^
    main.py

echo.
if exist dist\SoundOverlay.exe (
    echo  [OK] Build successful!
    echo       dist\SoundOverlay.exe
) else (
    echo  [ERROR] Build failed. Check output above.
)
echo.
pause
