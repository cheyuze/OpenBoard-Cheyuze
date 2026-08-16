@echo off
setlocal

set "PROJECT_ROOT=%~dp0\..\.."
set "QT_ROOT=C:\Qt\6.9.3\msvc2022_64"
set "QT_BIN=%QT_ROOT%\bin"
set "QT_DIR=%QT_ROOT%"
set "INNO_EXE=C:\Users\cheyu\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

call "%VCVARS%" || exit /b 1
set "PATH=%QT_BIN%;%PATH%"

cd /d "%PROJECT_ROOT%" || exit /b 1
if /I "%~1"=="clean" if exist build\win32\release rmdir /s /q build\win32\release

"%QT_BIN%\qmake.exe" OpenBoard.pro CONFIG+=release || exit /b 1
"%QT_BIN%\lrelease.exe" OpenBoard.pro || exit /b 1
nmake release-install || exit /b 1

copy /y thirdparty\ffmpeg\ffmpeg.exe build\win32\release\product\ffmpeg.exe || exit /b 1
"%QT_BIN%\windeployqt.exe" --release --compiler-runtime --no-translations build\win32\release\product\OpenBoard.exe || exit /b 1

set "QT_DIR=%QT_ROOT%"
set "QT_BIN=%QT_BIN%"
"%INNO_EXE%" release_scripts\windows\OpenBoard.iss /F"OpenBoard_Team_Edition_1.7.7" || exit /b 1

echo Build complete.
endlocal
