@echo off
rem ============================================================
rem  TUF FIGHTER - one click build (needs Visual Studio + CMake)
rem ============================================================
cd /d "%~dp0"
cmake -S . -B build
if errorlevel 1 goto :fail
cmake --build build --config Release --parallel
if errorlevel 1 goto :fail
echo.
echo Build finished: TUF_Fighter.exe
pause
exit /b 0
:fail
echo.
echo BUILD FAILED - see the messages above.
pause
exit /b 1
