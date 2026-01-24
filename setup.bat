@echo off
echo ==========================================
echo       Lychrel 196 Setup Script
echo ==========================================

REM Check if Python is available
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Python is not installed or not in PATH.
    pause
    exit /b
)

echo Creating virtual environment (.venv)...
if not exist ".venv" (
    python -m venv .venv
) else (
    echo Virtual environment already exists.
)

echo Activating virtual environment...
call .venv\Scripts\activate

echo Installing dependencies (gmpy2)...
pip install gmpy2

echo.
echo ==========================================
echo       Setup Complete!
echo ==========================================
echo You can now run the program using run.bat
echo.
pause
