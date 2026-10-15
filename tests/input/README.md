# Input, polygon, zoom and eraser-preview regression tests

Three small Windows/Qt regression suites compile selected **current production
method bodies** against real Qt widgets, graphics scenes and events. No installed
OpenBoard binary is launched and no user document is opened or changed.

## Run

Prerequisites: matching x64 MSVC developer environment, Qt 6 with Widgets, qmake,
and nmake. Tested with Qt 6.9.3 and VS 2022 Build Tools. Open an **x64 Native Tools
Command Prompt for VS 2022** and run from the repository root:

```bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\input\run-tests.ps1 -QMake C:\Qt\6.9.3\msvc2022_64\bin\qmake.exe
```

Qt does not have to be installed at that example path. If qmake is already on
PATH, omit `-QMake`. The runner queries qmake for its matching runtime and platform
plugin directories, uses Qt's offscreen platform, then restores the caller's
environment. It returns a failing exit status on extraction/build/test failures.

By default, production code is read from the repository containing this tests
directory. To test a separate checkout, pass an explicit source root:

```powershell
& .\tests\input\run-tests.ps1 -SourceRoot C:\work\OpenBoard -QMake C:\Qt\6.9.3\msvc2022_64\bin\qmake.exe
```

Output lives only under `tests/input/build/<BuildTag>/`, ignored by the included
`.gitignore`. `-BuildTag current` is the default; choose another simple name for
side-by-side results. Do not commit this directory or generated `.inc`, qmake,
object, executable or debug files. Only the source/control files in this
directory (including this README) belong in the repository.

For a historical comparison, use `-BaselineRef d27a372 -BuildTag baseline`. That
reads source from Git without checkout or repository edits. Baseline failures
are expected and still produce a nonzero exit status. `-SourceRoot` must be a Git
checkout containing the specified revision when this option is used.

## Coverage

`input_state_regression.cpp` — 13 separate-process cases:

- Empty sidebar resize and absent-document callbacks do not dereference null.
- Document replacement retires pending long press and drag state.
- External drag/drop cannot activate internal page-reorder state.
- Valid document resize still arranges thumbnails.
- Lost middle-button release does not leave pan latched; held middle still pans.
- Canvas entry restores the selected tool cursor without requiring a click.
  Active pans/drags, passive display views and selection/interaction-mode
  graphics-item hover cursors are not overwritten.

`polygon_zoom_regression.cpp` — 13 cases:

- Clearing annotations/all content drops unfinished polygon outline, fill and
  vertices; pointer movement cannot rebuild the discarded polygon.
- Clearing only objects/background preserves the in-progress polygon.
- Clear keeps the existing committed-object undo payload and command count.
- Double-click/presets select the exact zoom despite a rounded slider value;
  normal changes and an already exact value retain their behavior.
- Actual Qt render calls exercise the production Screen/NonScreen/PdfExport
  filters. Previews stay on screen, but do not enter thumbnails or PDF output.
- SVG and page-copy selection omit only temporary preview objects.
- Production polygon commit makes content saveable/renderable again; isolated
  undo/redo adapters validate the resulting scene state.
- The polygon predicate does not filter another geometry's fill preview.

`eraser_preview_regression.cpp` — 13 cases:

- Suppress Qt's inferred scene updates (`NoViewportUpdate`) to deterministically
  expose missing explicit viewport damage. This is fault injection, not a claim
  to reproduce the exact intermittent Windows/tablet event sequence.
- Compare actual backing-store pixels without calling `grab()` (which would
  repaint and conceal residual pixels). Moving, hiding, shrinking, moving off
  canvas and queued rapid input must clean the old preview.
- Repeat mapped damage checks at 50%, 67%, 125% and 250% board zoom.
- Require local rather than full-screen damage and no synchronous painting
  during 10,000 queued input events. Disabled previews and detached views are safe.
- The production methods and optional explicit-invalidation helper are extracted
  unchanged; only settings/controller access is adapted. No erase/undo or
  recording implementation is substituted into this preview test.

The eraser suite can be compared with its pre-fix Git baseline using
`-BaselineRef 18df905 -BuildTag eraser-baseline`; stale-region cases must fail.
For high-DPI coverage, run the generated eraser executable with
`$env:QT_SCALE_FACTOR = '1.5'` in the same matching Qt environment (restore the
previous value afterwards).

Current audited production code: **39/39 pass**. Prior historical `d27a372`: input
suite 2/8 pass (including two null-dereference crashes), polygon/zoom suite 6/13
pass; those earlier comparisons concern the original 21 input/polygon cases.

## Boundaries and maintenance

- The input suite extracts sidebar handlers plus only the middle-pan portion of
  `UBBoardView::mouseMoveEvent` and the viewport-entry handler. The cursor setter
  is a call-recording adapter; the full native integration fixture separately
  checks actual cursor pixels, tablet hover ownership and physical-mouse controls.
  The full board view and later stylus branches are not compiled into this unit test.
- The polygon suite extracts clear/cancel, preview ownership/update, commit,
  drawItems/rootItem, zoom setters, and preset/double-click callback bodies.
  `rebuildPolygonPreview` is a call-count adapter, not a geometry implementation.
- The eraser suite does not assert that all Windows compositor/graphics driver
  or tablet timing issues are reproduced. It verifies cleanup independent of
  the implicit scene-damage mechanism and does not replace hardware testing.
- Document/controller/delegate/group metadata and undo classes are adapters.
  The tests check actual handler behavior, Qt rendering and undo command data;
  they do not validate the complete production undo implementation.
- SVG and page-copy tests compile their production item-filter branches, not
  the entire serializer or deep-copy implementation. These tests do not replace
  an end-to-end save/reopen, PDF export or real document duplication test.
- Hardware pen/tablet/touch input, native drag loops, installed-app appearance,
  long-running recording and general system themes are outside these suites.
- Extraction is signature-anchored and brace-balanced, not a C++ parser. If
  production function signatures/structure change, update the extraction
  anchors deliberately; do not substitute hand-written handler copies to make
  a failed extraction pass. The historical missing-preview-predicate adapter
  exists only to run the old baseline and preserves its unfiltered behavior.

The production source root is read-only to these scripts. Test compilation and
temporary generated handlers are confined to the ignored build directory.
