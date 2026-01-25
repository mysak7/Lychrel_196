@echo off
setlocal enabledelayedexpansion

if not exist p196_standalone.exe (
    echo p196_standalone.exe not found. Attempting to compile...
    call compile_p196.bat
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
rem Format: dump.196.ITERATION
rem We sort by date because sorting by filename number in batch is hard
for /f "delims=" %%I in ('dir dump.196.* /B /O:D') do set LATEST_DUMP=%%I

if "%LATEST_DUMP%"=="" (
    echo Failed to find dump file.
    exit /b 1
)

echo Resuming from %LATEST_DUMP%

echo Starting p196_standalone...
echo Press Ctrl+C to stop. State is saved automatically every 1M digits.
echo.

rem Run p196_standalone
rem -i: input file
rem -d: dump interval (in seconds, 0 = disable)
rem -m: maintenance interval (0 = disable)
rem -M: max run time (0 = infinite)
rem -D: dump every N digits (1000000)

p196_standalone.exe -i %LATEST_DUMP% -d 0 -m 0 -M 0 -D 1000000
