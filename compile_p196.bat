@echo off
setlocal

rem Check for cl.exe
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo cl.exe not found. Please run this from a Visual Studio Developer Command Prompt.
    exit /b 1
)

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

echo Compiling p196_standalone...
rem AVX2 is essential here.
rem Added PREFETCH_LOAD_TYPE define and suppressed warning C4477.
cl /nologo /O2 /arch:AVX2 /wd4477 /D AVX2 /D ALIGN_AVX2 /D PREFETCH_DISTANCE=256 /D STREAMING_STORES /D PREFETCH_LOAD_TYPE=_MM_HINT_T0 /I p196_standalone p196_standalone/p196_mpi.c p196_standalone/mpi_stub.c p196_standalone/isf.c p196_standalone/mydump.c p196_standalone/XGetopt.cpp /Fe:p196_standalone.exe
if %errorlevel% neq 0 (
    echo Compilation of p196_standalone failed!
    exit /b 1
)

echo Build successful.
