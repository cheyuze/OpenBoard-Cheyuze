# OpenBoard 车厘子定制版

Version 1.9.0 is based on OpenBoard 1.7.7, with Windows-focused changes for
classroom screen recording and whiteboard stability:

1. Board mode opens in a normal resizable window on Windows.
2. The existing podcast pause/resume action is exposed on the recording
   palette.
3. Podcast recordings are encoded directly as MP4 (H.264/AAC) through the
   bundled FFmpeg shared libraries. Recording uses a monotonic clock, excludes
   paused time, and checks encoding and file-writing errors. A failed recording
   keeps available temporary files; this is not a guarantee of full recovery.
4. Version 1.9.0 fixes update-download completion crashes and strengthens
   cancellation, document import, page operations, drawing previews, camera
   lifecycle handling and capture-source switching. See RELEASE_NOTES.md and
   the regression suites under tests/ for coverage and remaining limits.

OpenBoard remains licensed under GPLv3 with its OpenSSL linking exception; see
`LICENSE` and `COPYRIGHT`. FFmpeg is distributed separately under its own
license; the installer must include the corresponding FFmpeg license and
source-code offer appropriate for the bundled binary.
