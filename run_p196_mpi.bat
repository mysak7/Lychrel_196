@echo off
setlocal enabledelayedexpansion

if not exist p196_mpi.exe (
    echo p196_mpi.exe not found. Attempting to compile...
    call compile_p196_mpi.bat
    if errorlevel 1 exit /b 1
)

rem Find latest dump file
set "LATEST_DUMP="

rem If no dump file, create seed
if not exist dump.196.* (
    echo No dump file found. Creating initial seed...
    if not exist make_seed.exe (
         echo make_seed.exe not found!
         exit /b 1
    )
    make_seed.exe
)

rem Find the latest dump file by iteration number
for /f "delims=" %%I in ('dir dump.196.* /B /O:D') do set LATEST_DUMP=%%I

if "%LATEST_DUMP%"=="" (
    echo Failed to find dump file.
    exit /b 1
)

echo Resuming from %LATEST_DUMP%

echo Starting p196_mpi with 4 processes...
echo Press Ctrl+C to stop.
echo.

rem Run p196_mpi using mpiexec
rem Check if mpiexec exists
where mpiexec >nul 2>nul
if %errorlevel% neq 0 (
    echo mpiexec not found. Please install Microsoft MPI (MS-MPI).
    exit /b 1
)

mpiexec -n 4 p196_mpi.exe -i %LATEST_DUMP% -d 0 -m 0 -M 0 -D 1000000
