@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0" || exit /b 1
set "PATH=C:\Qt\6.9.3\msvc2022_64\bin;%PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -File extract-updater.ps1 || exit /b 1
qmake updater-regression.pro || exit /b 1
nmake /nologo || exit /b 1
set "QT_QPA_PLATFORM=offscreen"
updater-regression.exe
exit /b %ERRORLEVEL%
