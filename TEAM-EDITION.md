# OpenBoard 车厘子定制版

This build is based on OpenBoard 1.7.7 and contains three Windows-focused
changes for classroom screen recording:

1. Board mode opens in a normal resizable window on Windows.
2. The existing podcast pause/resume action is exposed on the recording
   palette.
3. Podcast recordings are delivered as MP4 (H.264/AAC). Recording continues
   to use the stable Windows Media capture backend internally and is converted
   by the bundled FFmpeg executable when recording stops. The temporary WMV is
   deleted only after a successful MP4 conversion, so a failed conversion does
   not destroy the recording.

OpenBoard remains licensed under GPLv3 with its OpenSSL linking exception; see
`LICENSE` and `COPYRIGHT`. FFmpeg is distributed separately under its own
license; the installer must include the corresponding FFmpeg license and
source-code offer appropriate for the bundled binary.
