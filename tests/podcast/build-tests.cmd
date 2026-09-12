@echo off
setlocal
rem Run from an x64 Visual Studio Developer Command Prompt matching the Qt kit.
where cl.exe >nul 2>nul
if errorlevel 1 (
    echo Open an x64 Visual Studio Developer Command Prompt before running this script.
    exit /b 1
)
set "PODCAST_QMAKE=qmake.exe"
if defined QT_ROOT set "PODCAST_QMAKE=%QT_ROOT%\bin\qmake.exe"
set "PODCAST_TEST_DIR=%~dp0"
if not "%~1"=="" (
    if /I not "%~1"=="recording-clock" if /I not "%~1"=="microphone-buffer" if /I not "%~1"=="encoder-regression" (
        echo Unknown project. Choose recording-clock, microphone-buffer, or encoder-regression.
        exit /b 1
    )
    call :build "%~1"
    if errorlevel 1 exit /b 1
    exit /b 0
)
call :build recording-clock || exit /b 1
call :build microphone-buffer || exit /b 1
call :build encoder-regression || exit /b 1
echo All podcast regression harnesses built successfully.
exit /b 0

:build
if not exist "%PODCAST_TEST_DIR%build\%~1" mkdir "%PODCAST_TEST_DIR%build\%~1" || exit /b 1
pushd "%PODCAST_TEST_DIR%build\%~1" || exit /b 1
"%PODCAST_QMAKE%" "%PODCAST_TEST_DIR%%~1.pro"
if errorlevel 1 (popd & exit /b 1)
nmake /nologo
if errorlevel 1 (popd & exit /b 1)
popd
exit /b 0
