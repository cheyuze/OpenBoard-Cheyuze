@echo off
setlocal
cd /d "%~dp0" || exit /b 1
if defined QT_ROOT set "PATH=%QT_ROOT%\bin;%PATH%"
if not defined OPENBOARD_ROOT set "OPENBOARD_ROOT=%~dp0..\.."
if not defined FFMPEG_ROOT set "FFMPEG_ROOT=%OPENBOARD_ROOT%\thirdparty\ffmpeg\ffmpeg-9.0.1-full_build-shared"
set "PATH=%FFMPEG_ROOT%\bin;%PATH%"
build\recording-clock\bin\recording-clock-test.exe || exit /b 1
build\microphone-buffer\bin\microphone-buffer-test.exe || exit /b 1
build\encoder-regression\bin\encoder-regression.exe %*
exit /b %ERRORLEVEL%
