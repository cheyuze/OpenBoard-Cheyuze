# Recording stability regression tests

These harnesses compile production recording code directly. They generate their
own images and audio; they do not open a microphone or camera, capture the desktop,
modify documents, or launch OpenBoard. Run them after changes to recording timing,
buffering, codecs, pausing, stopping, or error handling.

## Requirements and build

Use the same Qt kit, compiler architecture, and shared FFmpeg development package
as the application. The Windows release uses Qt 6 / MSVC x64. The microphone test
requires Qt Multimedia; the encoder test requires the FFmpeg include files,
import libraries, and runtime libraries.

Open an **x64 Visual Studio Developer Command Prompt**, then run from this folder:

```bat
set "QT_ROOT=C:\path\to\Qt\msvc2022_64"
set "FFMPEG_ROOT=C:\path\to\ffmpeg-shared"
build-tests.cmd
run-tests.cmd --short
```

`QT_ROOT` is optional when the matching `qmake` and Qt runtime libraries are
already on `PATH`. `FFMPEG_ROOT` defaults to the repository's
`thirdparty/ffmpeg/ffmpeg-9.0.1-full_build-shared` folder. `OPENBOARD_ROOT` defaults
to `../..` relative to this directory and can be overridden to verify another
source checkout. Build products stay in `build/`; generated recordings and JSON
reports stay in `output-*`, all ignored by Git.

The `.pro` files can also be built individually with the platform's `qmake` and
make tool in separate build directories. Pass `OPENBOARD_ROOT=...` and
`FFMPEG_ROOT=...` as qmake arguments or environment variables. Put the matching
Qt/FFmpeg runtime libraries on the platform's library search path before running.
For example, from a repository-root shell with those variables set:

```sh
mkdir -p tests/podcast/build/recording-clock
cd tests/podcast/build/recording-clock
qmake ../../recording-clock.pro
make
./bin/recording-clock-test
```

## Suites

- `recording-clock.pro`: verifies the 64-bit monotonic recording clock at the
  former 11:55.8 overflow boundary, 13 minutes, and 40 days. Tests pause-to-stop,
  repeated pauses, resume, reset, and 10,000 transitions using injected elapsed
  times rather than sleeping or changing the system clock.
- `microphone-buffer.pro`: exercises the real microphone buffering implementation
  with `QBuffer` input. Checks sample alignment, bounded chunks, partial tail
  flushing, duplicate ready notifications, stopped-input guards, independent
  instances, uninitialized start, and destroyed device pointers. It includes the
  implementation and generated moc in one translation unit so deliberate private
  state injection remains compatible with MSVC symbols.
- `encoder-regression.pro`: compiles the real abstract encoder and FFmpeg encoder.
  Only the physical microphone backend is replaced with deterministic PCM input;
  resampling, FIFO, worker threads, H.264/AAC encoding, MP4 muxing, and completion
  signals are production code. Each generated file is decoded and checked for
  expected frame counts, changing frames, monotonic timestamps and audio duration.

## Recording runs

```bat
run-tests.cmd --short
run-tests.cmd
run-tests.cmd --extended
run-tests.cmd --realtime
```

`--short` runs the quick lifecycle, pause/resume, audio-tail, audio-counter,
resampling, and failure regressions. The default additionally encodes an
accelerated 20-minute timeline at 30 FPS and an 8-minute timeline at 60 FPS.
`--extended` exercises missing video/audio packet allocations, bounded video and
audio queues, autonomous failure completion, synchronous shutdown without event
processing, and a 60-second 1920x1080 / 30 FPS timeline. `--realtime`
records changing video and a tone at wall-clock pace for 15 minutes, crossing the
reported freeze time. Use only one mode option per run.

The wall-clock run also records periodic process-memory and queue measurements on
Windows. Those figures help spot growth within the run; a memory sample alone is
not proof that every allocation has been released.

The Windows helper also runs the clock and microphone suites before the selected
encoder suite. Every failure returns a nonzero exit code. Encoder results include
observable metrics in `output-regression/results.json`,
`output-extended/results.json`, or `output-realtime/results.json`. Fault tests
intentionally cause errors only in their own encoder instances and output files.

Synthetic tests cannot establish the reliability of a physical microphone, camera,
screen capture, graphics driver, full application UI, or installed package. Those
need a separate end-to-end recording check on the target machine. Passing a
15-minute run also does not guarantee every recording duration or disk condition;
keep the long-duration and fault suites as regressions when changing this core
feature.
