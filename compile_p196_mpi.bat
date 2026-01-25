@echo off
setlocal

rem Check for cl.exe
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo cl.exe not found. Please run this from a Visual Studio Developer Command Prompt.
    exit /b 1
)

rem Check for MS-MPI
if "%MSMPI_INC%"=="" (
    echo MSMPI_INC environment variable not set. Please install Microsoft MPI SDK.
    exit /b 1
)
if "%MSMPI_LIB64%"=="" (
    echo MSMPI_LIB64 environment variable not set. Please install Microsoft MPI SDK.
    exit /b 1
)

rem Remove trailing backslashes from MSMPI paths to avoid quoting issues
if "%MSMPI_INC:~-1%"=="\" set "MSMPI_INC=%MSMPI_INC:~0,-1%"
if "%MSMPI_LIB64:~-1%"=="\" set "MSMPI_LIB64=%MSMPI_LIB64:~0,-1%"

if not exist p196_standalone (
    echo p196_standalone directory missing!
    exit /b 1
)

echo Compiling make_seed...
cl /nologo /O2 /I p196_standalone p196_standalone/make_seed.c p196_standalone/isf.c /Fe:make_seed.exe
if %errorlevel% neq 0 (
    echo Compilation of make_seed failed!
    exit /b 1
)

echo Compiling p196_mpi (MS-MPI version)...
rem Note: We define ALIGN_AVX2 and others as before.
rem We include MSMPI_INC *before* p196_standalone to ensure we get the real mpi.h
rem We exclude mpi_stub.c and link against msmpi.lib

cl /nologo /O2 /arch:AVX2 /wd4477 /D AVX2 /D ALIGN_AVX2 /D PREFETCH_DISTANCE=256 /D STREAMING_STORES /D PREFETCH_LOAD_TYPE=_MM_HINT_T0 /I "%MSMPI_INC%" /I p196_standalone p196_standalone/p196_mpi.c p196_standalone/isf.c p196_standalone/mydump.c p196_standalone/XGetopt.cpp /link /LIBPATH:"%MSMPI_LIB64%" msmpi.lib /out:p196_mpi.exe

if %errorlevel% neq 0 (
    echo Compilation of p196_mpi failed!
    exit /b 1
)

echo Build successful. p196_mpi.exe created.
