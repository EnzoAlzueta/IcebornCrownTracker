@echo off
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CROWN_VS=%%i"
if not defined CROWN_VS exit /b 1
call "%CROWN_VS%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
set "CROWN_OUTPUT=build"
if "%ICEBORN_CROWN_DEV2%"=="1" set "CROWN_OUTPUT=build-dev2"
if not exist "%CROWN_OUTPUT%" mkdir "%CROWN_OUTPUT%"
pushd "%CROWN_OUTPUT%" || exit /b 1
cl /nologo /W4 /WX /MT /O2 /EHsc /std:c++20 /utf-8 /LD ..\src\crown_module.cpp /Fe:crown_tracker.dll || exit /b 1
popd
set "ICEBORN_CROWN_TEST_DLL=%CROWN_OUTPUT%\crown_tracker.dll"
python -B -m unittest discover -s tests -v
exit /b %errorlevel%
