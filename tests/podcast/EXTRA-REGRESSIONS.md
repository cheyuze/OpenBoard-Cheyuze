# Camera and source-switch regressions

These additional projects follow the existing `common.pri` source-root and Qt
configuration. No paths to a developer's staging directory are embedded.
The normal microphone-buffer test also checks that silent Int16, Int32, Float
and UInt8 PCM all report zero activity before running its existing buffer tests.

From an x64 Visual Studio Developer Command Prompt with `QT_ROOT` set:

```bat
mkdir build\camera-lifecycle
cd build\camera-lifecycle
"%QT_ROOT%\bin\qmake.exe" ..\..\camera-lifecycle.pro
nmake /nologo
set "PATH=%QT_ROOT%\bin;%PATH%"
bin\camera-lifecycle-test.exe
cd ..\..

mkdir build\source-switch
cd build\source-switch
"%QT_ROOT%\bin\qmake.exe" ..\..\source-switch.pro
nmake /nologo
bin\source-switch-test.exe
```

The source root defaults to the repository root; `OPENBOARD_ROOT` can point to a
different checkout just as in the other podcast regression projects.
Use a new build directory or `nmake /A` when changing `OPENBOARD_ROOT` for a
before/after comparison so generated source and objects cannot be reused from
the previous checkout.

`camera-lifecycle.pro` builds the actual preview implementation with synthetic
video frames and error signals. Its Qt application uses the offscreen platform;
no preview is shown and neither `startCamera()` nor `QCamera::start()` is called.
It verifies that late frames/errors from stopped sources cannot restore an old
image or stop a replacement camera, current errors close once, and closing a
preview clears interrupted dragging. Its single-translation-unit private-state
injection matches the existing microphone harness's MSVC-compatible approach.
Ensure the Qt offscreen platform plugin is available in the selected kit.

`source-switch.pro` requires Windows PowerShell for a small build-time extractor.
It compiles the production `setSourceWidget()` method verbatim against minimal
Qt board/application dependencies. Its tests use real QObject timers and
offscreen widgets, without opening OpenBoard or starting a recorder. It checks
Recording and Paused transitions back to the whiteboard, a timer tick after
resume, same-source no-op behavior and stopped source changes. A source layout
change that prevents extraction fails the build rather than silently testing an
outdated method copy. This bounded test does not verify full UI recording,
screen capture or physical device behavior.

Both projects return a nonzero exit status on failure.
