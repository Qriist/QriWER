@echo off
setlocal enabledelayedexpansion
cd /d %~dp0\..

set "VARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VARS%" (
    echo ERROR: vcvars64.bat not found at %VARS%
    exit /b 2
)

echo [1/3] Calling vcvars64.bat ...
call "%VARS%" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed
    exit /b 2
)

echo [2/3] Compiling qriwer_shim.cpp ...
set "INC_RFCPP=%CD%\resources\rapidfuzz-cpp"
set "INC_SRC=%CD%\src"
set "OUT_DLL=%CD%\QriWER.dll"
set "PDB=%CD%\QriWER.pdb"

cl.exe ^
  /nologo ^
  /std:c++17 ^
  /O2 ^
  /MD ^
  /W4 ^
  /EHsc ^
  /permissive- ^
  /utf-8 ^
  /I "%INC_RFCPP%" ^
  /I "%INC_SRC%" ^
  /Fo"%CD%\build\\" ^
  src\qriwer_shim.cpp ^
  /link ^
    /DLL ^
    /OUT:"%OUT_DLL%" ^
    /PDB:"%PDB%" ^
    /IMPLIB:"%CD%\QriWER.lib"
if errorlevel 1 (
    echo ERROR: cl.exe failed
    exit /b 1
)

echo [3/3] Verifying exports ...
dumpbin /exports "%OUT_DLL%" 2>&1 | findstr /i "qriwer_compare qriwer_version"
if errorlevel 1 (
    echo WARNING: expected exports not detected by findstr
    exit /b 3
)

echo.
echo OK: %OUT_DLL%
exit /b 0
