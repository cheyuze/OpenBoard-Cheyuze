@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d C:\OpenBoard-src\OpenBoard-1.7.7 || exit /b 1
"C:\Qt\6.9.3\msvc2022_64\bin\qmake.exe" OpenBoard.pro CONFIG+=release || exit /b 1
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\nmake.exe" -f Makefile.Release install
endlocal
