@echo off
chcp 65001 >nul
setlocal
if not defined DOCUMENT_TEST_VCVARS set "DOCUMENT_TEST_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined DOCUMENT_TEST_QT set "DOCUMENT_TEST_QT=C:\Qt\6.9.3\msvc2022_64"
call "%DOCUMENT_TEST_VCVARS%" >nul
if errorlevel 1 exit /b 1
set "DOCUMENT_TEST_SOURCE=%~dp0..\.."
if not "%~1"=="" set "DOCUMENT_TEST_SOURCE=%~1"
for %%I in ("%DOCUMENT_TEST_SOURCE%") do set "DOCUMENT_TEST_SOURCE=%%~fI"
for %%I in ("%DOCUMENT_TEST_SOURCE%\..\OpenBoard-ThirdParty") do set "DOCUMENT_TEST_THIRDPARTY=%%~fI"
if not "%~2"=="" set "DOCUMENT_TEST_THIRDPARTY=%~2"
set "PATH=%DOCUMENT_TEST_QT%\bin;%DOCUMENT_TEST_SOURCE%\vcpkg_installed\x64-windows\bin;%PATH%"
set "QT_QPA_PLATFORM=offscreen"
set "DOCUMENT_TEST_DIRECTORY=%~dp0"
if not exist "%~dp0build" mkdir "%~dp0build"
cd /d "%~dp0build"
qmake "%~dp0document-regression.pro" "REPO=%DOCUMENT_TEST_SOURCE%" "THIRDPARTY=%DOCUMENT_TEST_THIRDPARTY%"
if errorlevel 1 exit /b 1
nmake /nologo
if errorlevel 1 exit /b 1
release\document-regression.exe -o "%~dp0results.txt",txt
exit /b %errorlevel%
