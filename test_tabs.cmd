@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist console\build-kaihen mkdir console\build-kaihen
cl /nologo /std:c++17 /EHsc test_tabs.cpp /Fe:console\build-kaihen\test-tabs.exe /Fo:console\build-kaihen\test-tabs.obj
if errorlevel 1 exit /b 1
console\build-kaihen\test-tabs.exe
