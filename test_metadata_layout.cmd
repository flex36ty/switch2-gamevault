@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist console\build-kaihen mkdir console\build-kaihen
cl /nologo /W4 /TC test_metadata_layout.c /Fe:console\build-kaihen\test-metadata.exe /Fo:console\build-kaihen\test-metadata.obj
if errorlevel 1 exit /b 1
console\build-kaihen\test-metadata.exe
