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

## Undo display synchronization regression

`undo_display_regression.cpp` and `build-undo-display.ps1` add a separate
17-scenario suite for completed undo/redo actions. In an x64 developer shell:

```powershell
& .\tests\input\build-undo-display.ps1 -SourceRoot . -BuildDir .\tests\input\build\undo-display -QMake C:\Qt\6.9.3\msvc2022_64\bin\qmake.exe -MakeTool nmake.exe
$env:PATH = 'C:\Qt\6.9.3\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
& .\tests\input\build\undo-display\undo_display_regression.exe
```

This suite extracts the unchanged production item-command undo/redo bodies,
controller action connections, completed-action refresh helpers and live-stroke
repaint method. Real Qt actions/buttons, undo history, item groups, two views and
backing-store pixels are exercised at 50%, 100%, 125% and 200% zoom. It reads
pixels immediately after the action, before processing another input event, and
also checks repeated undo/redo, subsequent distant writing, safe empty history
and no synchronous full-view history repaint when pushing a newly drawn stroke.

`NoViewportUpdate` deliberately suppresses inferred scene damage. It is fault
injection, not a reproduction of the exact physical tablet/Windows driver
sequence. Only widget/layer/delegate metadata are inert adapters. A separate
full-client fixture verifies actual Ctrl+Z/Ctrl+Y and synthetic tablet events in
an isolated document profile. This suite does not record audio or video.

## Pointer-anchored image paste (full client)

`paste_position_native.cpp` links the actual built client objects, replacing
only its executable entry point. It does not substitute clipboard, scene,
image, event-filter, controller or undo implementations. In an x64 MSVC shell,
after building the current client and deploying its matching runtime:

```powershell
& .\tests\input\build-paste-position.ps1 -ClientMakefile .\Makefile.Release -RuntimeDirectory .\build\win32\release\product
```

Pass the actual generated **release** makefile and deployed runtime directory;
the example paths are placeholders. The script uses the makefile's compiler
flags, objects and libraries, with Unicode-safe MSVC response files. Rebuild
the client first after source/header changes: stale objects invalidate this
integration test. The script compiles and runs the fixture, returning nonzero
on any assertion, build or profile-isolation failure.

Each run creates a new profile below `tests/input/build/paste-position/`, uses
the process-local offscreen clipboard and synthetic images/pen events, and
refuses any other platform or data directory. No installed application, real
clipboard, user document, microphone, camera or recording is used. Offscreen
Windows fonts are loaded into the test process before layout; the fixture
waits for the actual application event loop rather than running inside an
initialization-time `processEvents` call.

Coverage: 69 assertions, including all three page modes at 50%, 100%, 125% and
200% view zoom with a distant pan; actual Ctrl+V and menu-action dispatch;
canvas entry without a click; menu/control hover retaining the canvas anchor;
synthetic subpixel pen hover; resize/clamping and post-hover pan; internal
single/multiple image copy with the production rotation transform; unchanged
Duplicate behavior; large file image fitting; SVG paste; image-under-ink and
selection layering; explicit drag/drop position; and paste undo/redo location.
These checks are also runnable with `QT_SCALE_FACTOR=1.5`. They do not claim
physical tablet-driver, Windows compositor or network-download timing coverage.

## Shared shape editor and direct regional shading

`shape_fill_native.cpp` uses the same full-client runner:

```powershell
& .\tests\input\build-paste-position.ps1 -ClientMakefile .\Makefile.ShapeFill.Release -RuntimeDirectory .\build\win32\release\release197 -BuildDir .\tests\input\build\shape-fill -TestSource shape_fill_native.cpp -TestName shape-fill
```

These are example local build/runtime paths, not installed-app paths. Rebuild
the client before running. The generic runner's default remains the paste
suite; `-TestSource` and `-TestName` select another full-client entry point.

The shape suite tests the real dashed/filled geometry drawing tool, shared
picture-style frame and toolbar, corner resize, Shift rotation snapping,
lock, copy/delete, keyboard nudge, undo/redo and narrow/wide window resizing.
Vector shapes and stroke groups share controls without changing their stored
geometry, fill or drawing algorithm; ordinary stroke selection also inherits
the shared editor because legacy geometry and handwriting use the same group
type. Text, video, widgets and group-container editors retain their delegates.

Regional shading coverage includes closed boundaries, small gaps, refusal of
large/unbounded openings, nested holes, seed validation and bounded maximum
input; asynchronous view-coordinate detection and cancellation; mouse/pen
lasso; transient feedback excluded from Podcast/NonScreen rendering; direct
commit on mouse/pen release or completed bucket detection, with no confirmation
panel; single-step undo/redo; below-ink placement; SVG save/reopen; and menu
presets/default 25% opacity plus persisted setting restoration. Detection runs
off the GUI thread after a capped 1024×1024 content render. No recorder code is
replaced, no microphone/camera is opened and no real screen recording occurs.

Screenshots are actual offscreen Qt client renders. Since that platform has no
OS compositor, the full-client menu capture paints the actual popup widgets at
their real global offsets over the actual main-window render. It is not an OS
screenshot and is not evidence of every Windows theme/driver combination.

## Continuous lasso shading interaction

`lasso_interaction_native.cpp` uses the same full-client runner with
`-TestSource lasso_interaction_native.cpp -TestName lasso-interaction` and a
separate `-BuildDir`. Set `OPENBOARD_FILL_TEST_OUTPUT` to an existing directory
for its full-client captures. Rebuild the production client objects first.

The updated workflow defaults to lasso, names the current mode on the command,
keeps it selected after each fill, and makes its QAction exclusive with the
existing pen/shape tools. Esc or selecting another tool exits; ordinary undo,
redo, colour controls and shading presets do not silently switch tools.
Migration from the earlier prototype defaults to lasso once, then remembers
explicit mode choices. Preset opacity, colour and gap values remain intact.

Coverage includes actual menu mouse-release and split-label arrow paths,
120-sample loops at 50/100/200% zoom with distant pan, consecutive loops,
mouse-click undo and Ctrl+Z, exact previous-tool restoration (legacy QAction
IDs are deliberately not treated as tool enums), synthetic pen release,
transient dashed closure feedback, full creation labels and native overflow.
Captures include 1440/1224/1024 logical widths and are exercised at 100% and
150% display scales. Menus are actual Qt widgets composed at their declared
viewport anchors because the offscreen platform lacks an OS compositor.
This remains synthetic isolated testing, not physical tablet-driver coverage.

### Native-window mouse-release regression

`lasso_window_events_native.cpp` must also run via the full-client runner,
with `-TestSource lasso_window_events_native.cpp -TestName lasso-window`.
It delivers the full mouse stream to the real `QWidgetWindow` handle, allowing
Qt to route it to the viewport. The older viewport-only fixture skipped this
transport layer and therefore missed a confirmed release-discard bug.

Before the fix this route shows a lasso preview but produces zero content and
zero history entries on release. After the fix it verifies one SVG, one undo
step, visibly shaded center pixels, no remaining preview, and one-step undo.
Both 100% and 150% display scales are checked. This exercises actual Qt window
dispatch with synthetic events on offscreen; it is not physical SendInput or
a claim of Windows/tablet hardware coverage.

### Point-by-point polygon shading

`polygon_shade_native.cpp` uses the full-client runner with
`-TestSource polygon_shade_native.cpp -TestName polygon-shade`, a separate
`-BuildDir` and `OPENBOARD_FILL_TEST_OUTPUT`. Rebuild client objects first.
The matrix in workspace `diagnostics/openboard-polygon-shade-20261110`
runs at 100% and 150% display scales with isolated profiles.

The 58 assertions per scale cover real QWidgetWindow mouse dispatch to the
viewport, intermediate releases, fixed vertices versus hover feedback,
start-point snapping at 50/100/200% zoom and distant pan, double-click,
right-click and Enter completion, Backspace, pending Ctrl+Z and the actual
Undo control, draft/idle Escape, degenerate geometry, one-object undo/redo,
SVG save/reopen and explicit mode preference. Synthetic tablet taps must
not leak handwriting. Transient vertices and edges must not appear in
Podcast/NonScreen renders. Narrow 1224/1024 layouts and the three-mode menu
are captured alongside draft, closure-cue and committed states at 1440.
The existing lasso-window regression remains required: dispatching only to
the viewport would miss the previously confirmed QWindow release bug.
These are synthetic offscreen Qt checks, not physical mouse/tablet or
long-recording verification. No microphone or camera is opened.

### Desktop recording icon cue

`desktop_record_icon_native.cpp` uses the full-client runner with
`-TestSource desktop_record_icon_native.cpp -TestName desktop-record-icon`.
Workspace `diagnostics/openboard-desktop-record-icon-20261110/run-matrix.cmd`
checks 100% and 150% display scales in isolated offscreen profiles.

The 58 assertions per scale verify red/muted recording-dot rendering at
16/24/32/48 sizes, DPR 1/2, Normal/Active/Disabled and Off/On states; the actual
Desktop QAction and visible toolbar button; unchanged generic monitor and
main recording icons; and unchanged action label, tooltip, check/enabled
state and document history when icons are refreshed. Full-client 1440/2325
renders and actual button grabs are saved with a separately declared icon-state
plate. These tests never trigger Desktop or start recording, microphone or
camera. This is a static functionality cue, not a live recording indicator.

### Vertical toolbar overflow

`toolbar_overflow_native.cpp` uses the full-client runner with
`-TestSource toolbar_overflow_native.cpp -TestName toolbar-overflow`.
Workspace `diagnostics/openboard-overflow-list-20261110/run-matrix.cmd` checks
100% and 150% scales. The real More extension opens a vertical menu without
changing toolbar height or canvas geometry. Coverage includes real window
mouse dispatch, Enter/Space/Down/Escape, narrow/wide resize, full labels,
original ordering, visible/disabled/check state, single original-action
invocation, existing submenus, compound button proxies and two-way slider
forwarding. The original QWidgetAction widgets stay on the toolbar. Shortcut
text is displayed without registering a duplicate shortcut.

### Center-radius circles and straight-line endpoints

`circle_line_native.cpp` uses the full-client runner with
`-TestSource circle_line_native.cpp -TestName circle-line`.
Workspace `diagnostics/openboard-circle-line-20261110/run-matrix.cmd` checks
100% and 150% scales. Circle previews and commits keep the press point as the
center and use pointer distance as radius, including horizontal/vertical
drags. Ellipse remains a separate bounding-box geometry.

Straight lines use two endpoint handles rather than an image-style rectangle.
Coverage includes real window dragging, a fixed opposite end, preserved
transverse width, one-step undo/redo, zoom/pan, transformed parent math,
keyboard nudging, Shift snapping, Escape cancellation, no-op/coincident input,
lock, numeric endpoint coordinates, copy and actual SVG save/reopen.
Parent-transform checks use a selectable child of a plain transform holder;
they do not claim editing an individual member while a whole group is selected.
Legacy equal-width collinear nominal lines are recognized conservatively;
non-collinear handwriting is not. Controls are excluded from Podcast/NonScreen
renders. This does not start or validate an encoded recording.

### Default borderless whiteboard

`page_default_native.cpp` uses the full-client runner with
`-TestSource page_default_native.cpp -TestName page-default`.
Set `OPENBOARD_PAGE_DEFAULT_CASE` to `fresh`, `upgrade`, `remember-wide` or
`remember-regular`; workspace `diagnostics/openboard-default-page-20261110/run-tests.cmd`
runs all four with separate generated profiles. The fixture seeds settings
only inside the runner's exact isolated profile, before creating the client.
It verifies one-time migration, later explicit preferences, actual page-menu
state, new documents/pages, page-mode undo/redo, per-page SVG restoration,
legacy SVG without a mode attribute and the real PDF import adaptor with a
synthetic one-page PDF. Existing documents are never batch-migrated. New blank
pages default to borderless, while imported PDF/PPT paper formats remain fixed.

### Shape hover and thin-geometry selection

`shape_hover_native.cpp` uses the full-client runner with
`-TestSource shape_hover_native.cpp -TestName shape-hover`.
Workspace `diagnostics/openboard-shape-hover-20261110/run-matrix.cmd` checks
100% and 150% display scaling with actual QWidgetWindow mouse dispatch.
Selector hover and fallback clicking share a six-logical-pixel viewport probe;
the stored shape and its precise scene silhouette remain unchanged.
The suite checks lines, circles and hollow non-line outlines at 50/100/200%
view zoom with distant pan, single-pixel movement, near-versus-far targets,
selection and drag/undo/redo, lock, Ctrl selection, foreground-picture occlusion,
canvas-to-toolbar cursor ownership and drawing-tool cursors. Endpoint cross
cursors remain specific, while selected shape bodies use the pointing hand.
Output renders are compared before/after hover to catch visible side effects.

Widget grabs omit cursors. These QA PNGs explicitly composite the real Windows
hand/cross cursor bitmap at the synthetic event position only after checking
the actual Qt cursor enum. They are cursor-composited offscreen client renders,
not OS screenshots or physical mouse/tablet evidence. No recording, microphone,
camera, installed profile or real clipboard is used. The small entry-handler
unit adapter leaves this full hit-test to the complete-client suite.

### Release notes and feedback routing

`release_hints_native.cpp` checks the actual rebuilt client version, the
upgrade-only forced display with recurring hints disabled, persisted seen
version, exact title/version/feedback URL and preserved Yach card. Generate
`tests/input/build/release200-hints/release_hints_native.moc` with the matching
Qt `moc` before selecting this fixture in the full-client runner. Its QObject
receiver intercepts HTTPS/Yach desktop-service requests, so no browser or
external form is opened. The custom-protocol card is clicked through real
renderer input, because a script-only click lacks Chromium's user gesture.
