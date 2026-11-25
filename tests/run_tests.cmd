@echo off
rem Builds and runs the unit tests with MSVC. Run it from a Visual Studio
rem developer prompt (cl.exe on PATH).

setlocal
cd /d "%~dp0"
if not exist bin mkdir bin

cl /nologo /W4 /I..\inc /Fobin\ /Febin\test_crt_auto.exe test_crt_auto.c ..\src\crt_auto.c || exit /b 1
bin\test_crt_auto.exe || exit /b 1

cl /nologo /W4 /I..\inc /Fobin\ /Febin\test_display_settings.exe test_display_settings.c ..\src\display_settings.c ..\src\crt_auto.c || exit /b 1
bin\test_display_settings.exe || exit /b 1

cl /nologo /W4 /I..\inc /Fobin\ /Febin\test_vbeam.exe test_vbeam.c ..\src\vbeam.c winmm.lib || exit /b 1
bin\test_vbeam.exe || exit /b 1

cl /nologo /W4 /I..\inc /Fobin\ /Febin\test_settings.exe test_settings.c ..\src\settings.c ..\src\crt_auto.c || exit /b 1
bin\test_settings.exe || exit /b 1
