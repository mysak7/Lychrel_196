@echo off
if not exist ".venv" (
    echo Error: Virtual environment .venv not found.
    echo Please run setup.bat first.
    pause
    exit /b
)

echo Starting Lychrel 196 calculation...
echo Press Ctrl+C to stop safely.
echo.

call .venv\Scripts\activate
python main.py %1
pause
