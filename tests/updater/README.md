# Updater stability regression

Run `build-run.cmd` with the configured Qt 6.9.3 / MSVC 2022 kit, or run
`extract-updater.ps1`, build `updater-regression.pro` with the matching Qt kit,
then launch `updater-regression` with `QT_QPA_PLATFORM=offscreen`.

The extraction step compiles production `closeUpdateDownload`,
`stopUpdateDownload` and `downloadUpdateInstaller` methods verbatim, except for
one explicit redirection of the download directory into a `QTemporaryDir`.
Minimal controller/window fixtures replace unrelated application dependencies.
The real Qt network requests, progress dialogs, retry timers, file helpers and
background hash/copy work are used. A loopback HTTP server serves generated
bytes. No remote service, physical device, user document or installed app is
used. Installer launch is a test stub; completion prompts decline installation.

Coverage: normal completion; reuse of a verified installer; Range resume;
unsupported Range and fallback; complete partial file/HTTP 416; hash mismatch
with preservation of the prior installer; duplicate download requests;
cancellation during transfer and retry delay; controller destruction during
active transfer and retry; HTTP error bodies never contaminating partial files;
read/copy failures; canceled file work; atomic replacement. The Windows test
executable reserves a 1 MiB stack, matching the application crash constraint.

This is an updater-method integration test, not the full OpenBoard UI, actual
GitHub/Baidu networks, UAC elevation or an installation end-to-end test.

`sources-regression.pro` also compiles the unchanged production metadata
request/response methods extracted by `extract-metadata.ps1`. A controlled Qt
network manager supplies responses without external traffic. Coverage includes
website-only success (no GitHub dependency), old-profile source ordering,
website failure/fallback, actual JSON and SHA validation, origin/redirect
restrictions, redirect loops, response-size limits, repeated clicks and recovery
after all sources fail. The shared URL policy is the production header.
