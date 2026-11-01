# Finished Tasks

Finished tasks from `.agents/01-plan.md`, one compact bullet each:
`- Txxx <title> - what was delivered, key files` (find the commit with `git log --grep Txxx`)

- T000 Task plan - split `plan.md` into small isolated tasks in `.agents/01-plan.md`, added commit and
  progress rules to `AGENTS.md`.
- T001 wxWidgets fetch script - `tools/fetch-wx.ps1` downloads wx 3.3.4 vc14x x64 Dev/ReleaseDLL/headers,
  checks pinned SHA-256, extracts via 7-Zip or System32 `tar` into `3rdparty/wxWidgets` through a staging
  dir; stamp file makes re-runs a no-op. Started `.codex/arch.md` and `.codex/map.md`.
- T002 CMake skeleton and build scripts - C++23 targets `icd_core`, `icd_platform`, `icd_services`,
  `IroncladDefrag` (empty frame), `icd-cli` (`--version`); `cmake/IcdWxWidgets.cmake` imported wx targets
  with per-config DLL copy; `build.bat [Debug|Release|All]` fetches wx and builds.
- T002a Quality gate: clang-tidy and Lizard - `quality.bat [changed|all]` with pinned clang-tidy 22.1.8 and
  Lizard 1.24.0 in `.venv-quality`; Ninja + cl compile database in `build/quality`; CCN and cognitive
  complexity <= 10, warnings as errors (`.clang-tidy`, `cmake/Check*.cmake`, `cmake/quality_scope.py`).
- T003 Application manifest and version resource - `res/IroncladDefrag.manifest` (requireAdministrator,
  Common Controls v6, PerMonitorV2, long paths), `res/IroncladDefrag.rc` (wx.rc + VERSIONINFO),
  generated `core/Version.h` from `src/core/Version.h.in`; `run-elevated.bat`.
- T004 Test framework - GoogleTest 1.18.0 via pinned `FetchContent`, `icd_tests` + CTest
  (`tests/CMakeLists.txt`, `tests/core/SmokeTest.cpp` with `formatBytes()` tests), `test.bat [Debug|Release|All]`.
- T005 Logging - `core::logging` (levels, literal-only `FieldKey` fields, one-line UTC records, thread-safe
  `Logger`, `globalLogger()`), `platform::FileSink` (rotating, `%LOCALAPPDATA%\IroncladDefrag\logs`) and
  `DebugOutputSink`, `services::initAppLogging()`; GUI and CLI log startup/shutdown. Tests for format,
  escaping, concurrency, rotation.
- T006 Cancellation and progress types - `core/job/CancelToken.*` (`CancelSource`/`CancelToken` on
  `std::stop_source`, parent-linked sources, `OperationCancelled`), `core/job/Progress.*` (`Progress`,
  `ProgressSink`, `ThrottledProgress` ~10 Hz with injectable clock); tests in `tests/core/JobTypesTest.cpp`.
- T007 Resource detection and caps - `core/resources/ResourceCaps.*` (`computeCaps()` with `CapReason`,
  `describeCaps()`), `platform/win/SystemResources.*` (`detectSystemResources()`: groups, affinity, job
  CPU/memory limits, cores, RAM; `LowMemorySignal`); startup log line `resources`; tests for caps edge cases.
- T008 Thread pool - `core/exec/Executor.*` (`ChunkedRange`, `Executor`, `InlineExecutor`, deterministic
  `parallelReduce`), `services/exec/ThreadPool.*` (per-worker deques with stealing, caller joins
  `parallelFor`, cancel/exception aware); tests prove bit-identical reductions for 1..31 threads and fast cancel.
- T009 Job runner and UI bridge - `services/job/JobRunner.*` (jobs on the pool, cancel, ~10 Hz progress,
  one `JobResult`), `services/AppServices.*` (caps + pool + runner), `services/job/FakeJob.*`,
  `ui/bridge/JobEvents.*` (`wxQueueEvent` bridge, detach on close); Debug menu run/cancel with status-bar
  progress. Phase 0 exit checked: window stays responsive (no ping timeouts), cancel is immediate.
- Phase 0 exit - clean clone + `test.bat` fetches wx, builds Debug and Release with clang-cl, 60 tests
  pass in both; GUI opens, the fake parallel job reports progress and cancels without blocking the UI.
  Also fixed: wx fetch under a PowerShell 7 parent (`PSModulePath`), progress going backwards.
- T010 Main window layout - section 11 menus as tables (`ui/MainMenu.*`, stubs disabled), shared
  `ui/Commands.*` ids with `enableCommand()`; drives area, map/bottom splitter; `ui/panels/BottomPanel.*`
  (Summary/Strategy/Plan/Progress/Log tabs, workflow buttons, the single status line + gauge, no status
  bar); `ui/panels/MapPlaceholder.*`; `core/format/Count.*` (`formatCount()`).
- T011 Drive list panel with fake drives - `core/model/DriveInfo.*` (drive model, badges, `canOptimize()`),
  `core/format/DriveText.*` and `Duration.*` (column texts, ages), `services/drives/IDriveProvider.h`,
  `FakeDriveProvider.*` (SSD, three NTFS HDDs, one exFAT) owned by `AppServices`; `ui/panels/DrivesPanel.*`
  (`wxDataViewCtrl`, space bar and TRIM renderers); selection drives the map caption, Refresh keeps it.
- T012 Workflow state machine - `services/workflow/Workflow.*` (table-driven states/actions/jobs, Quick
  stops at review; 12 tests), `ui/WorkflowBinding.*` (commands, fake jobs, results, enable state),
  `services/workflow/FakeResults.*`, Summary/Strategy/Plan pages, `core/job/PauseGate.*`,
  `core/job/StageClock.*` (elapsed, time left); fake job gained stage names, item labels, pause. Debug
  menu removed (Analyze runs the same fake parallel job). Clicked through on fake data.
- T013 Shortcuts and About box - `ui/Accelerators.*` (menu shortcut table applied to menu items; map keys
  of 7.4 on the focused map, zoom keys share the View commands), `ui/dialogs/AboutDialog.*` (version,
  build, re-detected processors and memory, active caps), `services::buildDescription()`,
  `AppServices::detectResources()`; View commands enabled as stubs once the map shows a pattern.
- Phase 1 exit / Gate G1 - workflow clicked through on fake data (Analyze, Quick, strategy change, plan,
  review, execute with confirm, pause/resume, cancel, TRIM), single status line, no duplicated panels;
  layout agreed by the user. Follow-up from the review: shortcut help view and tooltips (T034).
- T020 Snapshot data structures - `core/snapshot/*`: `FileTable` (SoA, 32-bit indexes, <= 64 B/file),
  `NamePool` (interned), `DirectoryTree` (parent indexes, paths with cycle guard), `ExtentIndex` (16-byte
  records sorted by LCN, per-file VCN order, fragment count rejoins split runs), `FreeRunList`,
  order-independent validating `SnapshotBuilder`; 11 tests.
- T021 LCN stripes - `core/snapshot/Stripes.*`: `StripeLayout` (aligned fixed stripes covering the volume
  once), `Stripes` views over extents and free runs, `clip()`, deterministic `mapStripes()` /
  `reduceStripes()`; tests for coverage and bit-identical parallel results.
- T022 Synthetic volume generator - `core/synth/*` (seeded `generateSyntheticVolume()`, own RNG; class mix,
  fill, fragmentation with interleaving, free patterns, $MFT, directory indexes), `snapshotMemoryBytes()`,
  32-byte `ExtentEntry`, `tools/bench/synth-bench.cpp`: 30 TB / 20 M files in 29 s, 55.7 M extents, peak
  5.6 GB (budget 8 GB).
- T023 Map world layout - `core/map/World.*`: `roundToNice()`, world from cluster count and screen aspect,
  LCN <-> cell, zoom levels and tile math, row segments; tests for round trips, edges, aspects, tiles.
- T024 LOD pyramid - `core/map/Pyramid.*`: quadtree of 8-byte bins over the world grid (bin = tile pixel;
  the plan's 1-D wording reinterpreted, see arch.md), base <= 32 M bins (16 x 16 cells at 30 TB), parallel
  fill per row band, 2x2 merge, dirty-range update; brute-force, parallel and update tests; 30 TB in 0.86 s
  on 8 threads, 320 MB.
- T025 Tile rasterizer - `core/map/TileRasterizer.*` (pyramid or extent tiles, grid lines), `Palette.*`
  (`OccupancyPalette`), shared `BinSummary.*` with per-extent map flags; golden, hand-volume and
  pyramid = extents tests; 30 TB: coarse <= 1.1 ms, fine (<= 1 cluster/px) <= 1.1 ms, level 3 3.4 ms
  (over the 2 ms fine target; recheck in T033).
- T026 Rendering spike - `tools/spike/map-render-spike` (4K canvas, 30 TB tiles, scripted zoom/pan): raw
  Direct2D on the HWND 0.29 ms/frame (3,390 fps), wx Direct2D renderer 55 ms (bitmaps re-created per
  context), GDI 1.9 ms (visible part only). Chosen: Direct2D on the HWND; recorded in arch.md.
- T027 Map control: tile cache and drawing - `ui/map/MapCanvas.*` (async tiles on the pool via a detachable
  mailbox, 256 MB LRU GPU cache, fit-to-view, no re-flow on resize), `ui/map/D2DRenderer.*`, `ui/panels/MapPanel.*`
  (replaces the placeholder), `core/map/MapData.*`, `core/util/LruCache.h`, `services/workflow/SyntheticAnalysis.*`
  (Analyze builds a synthetic map), 30 TB fake drive H: (analysed in 16.6 s).
- T028 Map navigation - `core/map/Viewport.*` (anchored zoom, pan, limits, eased zoom and inertia by elapsed
  time; 7 tests), `ui/map/MapNavigator.*` (drag + inertia, wheel/pinch zoom at the pointer, gestures,
  double-click, map keys, View zoom commands), scaled fallback tiles, upload budget; 30 TB zoom from whole
  volume to one cluster at 71.8 fps (p95 16 ms).
- T029 Minimap and ruler - `core/map/Overview.*` (minimap placement, marker, drag math), `core/map/Ruler.*`
  (row ticks with byte offset / % of volume / % of disk, in-row column ticks, exact-step labels),
  `ui/map/Minimap.*` (overlay in the D2D frame, coarsest tile as image, drag), `ui/map/Ruler.*` (GDI rulers
  left of and above the map), View > Minimap toggle; 15 tests; 30 TB zoom still 73 fps.
- T030 Hover tooltip - `core/map/HitTest.*` (`hitTest()`: LCN, extent, fragment i/n, free run, pixel coverage;
  `describeHit()` lines, `formatFileTime()`), `ui/map/MapHover.*` (popup after a 350 ms rest, follows the
  pointer); 8 tests incl. brute force at every level on a synthetic volume. Type line from file flags until
  classes exist (Phase 5).
- T031 Selection and file navigation - `core/snapshot/FileSearch.*` (`searchFiles()`: names and path pieces,
  ranked, parallel; 3-19 ms on 20.5 M files), `core/map/Selection.*` (fragments, overlay, run shapes,
  `runsTouch()`, `highlightRuns()`, `focusView()`), `ui/map/MapSelection.*` (click select, F3 / Shift+F3),
  `ui/dialogs/GoToFileDialog.*` (Ctrl+F, toolbar search), canvas dirty tiles by selection version; 10 tests
  incl. refreshed tiles = changed tiles at every level.
- T032 Color modes and legend - `core/map/ColorModes.*` (fragmentation, age, size modes; six-bucket age/size values per
  file averaged by the pyramid in a new `MapBin::size` byte (used count now 24 bits); legends, palettes, exact parallel
  `countLegend()` for the visible cells), `ui/map/Legend.*` (strip under the map, counts after the view rests),
  toolbar/menu mode switching (cached tiles re-render via the dirty path, no data rebuild); 7 tests incl. counts =
  per-cluster brute force and tile colors = legend colors; 30 TB whole-volume count ~85 ms, map data ~414 MB.
- T034 Shortcut help - description on every `ui/Accelerators` entry (menu shortcuts and map keys), `commandTip()`,
  `ui/dialogs/ShortcutsDialog.*` (Help > Keyboard shortcuts, F1: grouped by menu, then map keys), tooltips with the
  shortcut on the workflow buttons, the drawn TRIM button, the map color choice and Find file.
- T033 Map performance check - `tools/bench/map-bench.cpp` (30 TB synthetic map as the app builds it; pyramid, memory,
  tiles per level and color mode, real `MapCanvas` in a window through scripted zoom and pans; no power throttling).
  Fixes: per-record color values, LCN record block index for tile rows, tight whole-unit loop, reuse of identical
  unit colors. All plan.md 7.5 targets met: pyramid 0.74 s on 8 threads, map 441 MB, coarse tiles <= 0.8 ms, fine
  <= 1.5 ms (level 3), zoom 119 fps and pans 120 fps at 1920 x 1080. **Phase 2 exit.**
- T040 Test volume scripts - `tools/testvol/make-testvol.ps1` (diskpart: create dynamic GPT/NTFS VHDX with a cluster size,
  mount read-only or writable with a fixed or free letter, dismount, remove, info), `tools/testvol/verify-testvol.ps1`
  (JSON manifest with SHA-256 of every stream incl. ADS, reparse targets; verify + `chkdsk /scan`). 4 GB round trip:
  hash, verify, read-only remount verify clean; a changed file and stream are reported (exit 1).
- T042 Volume enumeration - `platform/win/Volumes.*` (volumes with mount paths and a file system; label, sizes, disk number,
  partition offset, disk size, NTFS volume data; failures noted, not thrown; `isElevated()`), `platform/win/Handle.h`,
  `platform/win/TextConvert.*`, `core/model/VolumeLayout.h` in `DriveInfo`, `services/drives/SystemDriveProvider.*` as
  the default drive list (`ICD_FAKE_DRIVES=1` for the fake one). Undetected badges are optional (no guessed "off").
  `VolumesTest` matches `fsutil fsinfo ntfsinfo` on every lettered NTFS volume.
