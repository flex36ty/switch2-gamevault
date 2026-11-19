@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /W4 test_plan.cpp /Fe:build-kaihen\test-plan.exe /Fo:build-kaihen\test-plan.obj
if errorlevel 1 exit /b 1
build-kaihen\test-plan.exe
