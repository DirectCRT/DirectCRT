@echo off
rem Builds and runs the unit tests with MSVC. Run it from a Visual Studio
rem developer prompt (cl.exe on PATH).

setlocal
cd /d "%~dp0"
if not exist bin mkdir bin

cl /nologo /W4 /I..\inc /Fobin\ /Febin\test_crt_auto.exe test_crt_auto.c ..\src\crt_auto.c || exit /b 1
bin\test_crt_auto.exe || exit /b 1
