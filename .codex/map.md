# Repository Map

Modules, source files, build files, dependencies and generated output. Keep it current with structure
changes (see `AGENTS.md`).

## Top level

| Path | What it is |
| --- | --- |
| `plan.md` | Development plan (phases, design decisions). |
| `.agents/01-plan.md`, `.agents/00-finished.md` | Open tasks and the log of finished ones. |
| `.codex/arch.md`, `.codex/map.md` | Architecture notes and this map. |
| `story/` | Notes from earlier attempts (read-only history). |
| `tools/` | Developer scripts. |
| `CMakeLists.txt` | All targets and their explicit source lists; pins `ICD_WX_VERSION`. |
| `cmake/IcdCompilerOptions.cmake` | `icd_target_defaults()`: C++23, `/W4 /WX /utf-8`, MSVC-only conformance flags, Unicode and Windows defines. |
| `cmake/IcdWxWidgets.cmake` | Imported targets `wx::base`, `wx::core`, `wx::wx` from `3rdparty/wxWidgets`; checks the fetch stamp. |
| `build.bat` | `build.bat [Debug|Release|All]`: fetches wx, configures `build/` (Ninja Multi-Config, clang-cl, lld-link) once, builds. `ICD_TOOLCHAIN=msvc`: VS 2026 in `build-msvc/`. |
| `test.bat` | `test.bat [Debug|Release|All]` (default All): `build.bat`, then `ctest`. |
| `run-elevated.bat` | `run-elevated.bat [Debug|Release]`: starts the GUI through the shell (UAC prompt). |
| `tools/dev-env.cmd` | Shared by the scripts: `ICD_BUILD_DIR`, vcvars64 once, LLVM first on `PATH`. |
| `res/IroncladDefrag.manifest` | Embedded manifest: `requireAdministrator`, Common Controls v6, PerMonitorV2 DPI, long paths, Win10+. |
| `res/IroncladDefrag.rc` | wx standard resources (`wx/msw/wx.rc`, its manifest off) and `VERSIONINFO`. |
| `.gitattributes` | CRLF for `.bat`, `.cmd`, `.ps1`. |
| `quality.bat` | `quality.bat [changed|all]`: creates `.venv-quality`, enters vcvars64, configures `build/quality` (Ninja + clang-cl, compile database, no PCH), builds `check-quality[-all]`. |
| `.clang-tidy` | clang-tidy checks and the cognitive complexity threshold (10). |
| `cmake/QualityCommon.cmake` | Scope, paths and the changed-file helper shared by the check scripts. |
| `cmake/CheckComplexity.cmake` | Lizard gate (CCN <= 10) over `src/`. |
| `cmake/CheckClangTidy.cmake` | clang-tidy gate: selects `src/` entries from the compile database, runs `run-clang-tidy`. |
| `cmake/quality_scope.py` | Changed-file selection: changed files, plus `.cpp` files that include a changed header. |
| `tools/requirements-quality.txt` | Pinned clang-tidy 22.1.8 and Lizard 1.24.0 for `.venv-quality`. |

## Targets

| Target | Kind | Sources | Links |
| --- | --- | --- | --- |
| `icd_core` | static lib | `src/core/**` | - |
| `icd_platform` | static lib | `src/platform/win/**` | `icd_core` |
| `icd_services` | static lib | `src/services/**` | `icd_core`, `icd_platform` |
| `IroncladDefrag` | GUI exe | `src/main.cpp`, `src/ui/**`, PCH `src/precompiled.h` | `icd_services`, `wx::wx`, `d2d1` |
| `icd-cli` | console exe | `src/cli/**` | `icd_services` |
| `map-render-spike` | console exe with a wx window | `tools/spike/map-render-spike/main.cpp` | `icd_services`, `wx::wx`, `d2d1` |
| `synth-bench` | console exe | `tools/bench/synth-bench.cpp` | `icd_services`, `psapi` |
| `map-bench` | console exe with a wx window | `tools/bench/map-bench.cpp` plus the `src/ui/map` canvas sources, `ui/Accelerators`, `ui/Commands` | `icd_services`, `wx::wx`, `d2d1` |
| `icd_tests` | console exe (GoogleTest, CTest) | `tests/**` (own `tests/CMakeLists.txt`) | `icd_core`, `icd_platform`, `icd_services`, `GTest::gtest_main` |
| `check-complexity[-all]`, `check-clang-tidy[-all]`, `check-quality[-all]` | custom | quality gate; clang-tidy only in Ninja builds with a compile database | - |

## Source files

| File | Purpose |
| --- | --- |
| `src/core/Version.h.in` | Version macros template; CMake writes `build*/generated/core/Version.h`. |
| `src/core/format/ByteSize.*` | `formatBytes()`: Explorer-style byte sizes. |
| `src/core/format/Count.*` | `formatCount()`: locale-neutral thousands separators. |
| `src/core/format/Duration.*` | `formatDuration()` (stopwatch), `formatAge()` ("3 h ago"). |
| `src/core/format/DriveText.*` | All-disks column texts: `driveName()`, `describeSpace()`, `describeStorage()` (media with bus), `describeFragmentation()`, `describeLastAnalysed()`, `badgeLabels()`. |
| `src/core/model/DriveInfo.*` | `DriveInfo`, `DriveBadges` (undetected facts optional), `MediaType` (HDD, SSD, Virtual, Unknown), `StorageFacts`, `VolumeLayout`; `classifyMedia()`, `canOptimize()` (NTFS, not SSD), `isSsd()`. |
| `src/core/model/VolumeLayout.h` | `VolumeLayout` (volume GUID name, sector/cluster size, cluster counts, disk number, partition offset, disk size, extent count) and `NtfsLayout` (serial, version, record size, MFT/mirror/zone, reserved). |
| `src/core/exec/Executor.*` | `ChunkedRange`, `Executor` interface (`submit`, `parallelFor`), `InlineExecutor`, deterministic `parallelReduce`. |
| `src/services/exec/ThreadPool.*` | Work-stealing `ThreadPool` (per-worker deques, caller joins `parallelFor`, cancel-aware). |
| `src/core/job/CancelToken.*` | `CancelSource`/`CancelToken` over `std::stop_source`, parent-linked sources, `OperationCancelled`. |
| `src/core/job/PauseGate.*` | `PauseGate`: workers wait between items while paused; cancellation releases them. |
| `src/core/job/StageClock.*` | Job elapsed time and a linear per-stage time-left estimate for the Progress tab. |
| `src/core/job/Progress.*` | `Progress` snapshot, `ProgressSink`, `ThrottledProgress` (~10 Hz, injectable clock). |
| `src/core/log/Log.h` | `logging::Level`, `Field`, `Record`, `Sink`, thread-safe `Logger`, `globalLogger()`, `info()`/`error()`/... helpers. |
| `src/core/log/LogFormat.cpp` | `formatRecord()`: one-line `key=value` records, UTC time, escaping. |
| `src/core/log/Logger.cpp` | `Logger` and the global helpers. |
| `src/core/snapshot/Types.h` | `Lcn`, `ClusterCount`, `FileIndex` (32-bit, `noFile`), `ClusterRun`, `VolumeGeometry`, `SnapshotError`. |
| `src/core/snapshot/NamePool.*` | Interned UTF-8 names in one buffer (`intern()`, `name()`, `freezeLookup()`). |
| `src/core/snapshot/FileTable.h` | Per-file columns (name, parent, size, clusters, flags, times, file reference); `FileFlag`. |
| `src/core/snapshot/FileSearch.*` | `searchFiles()`: "go to file" over names (exact, prefix, contains) and path fragments (`photos\a`), ASCII case-insensitive, one pass over distinct names then the file column, parallel and deterministic, with a limit and a total. |
| `src/core/snapshot/DirectoryTree.*` | Child lists from parent indexes, roots, `path()` with a cycle guard. |
| `src/core/snapshot/ExtentIndex.*` | 16-byte `ExtentRecord` (40-bit LCN, 24-bit length), index by LCN (`find`, `overlapping`) and by file in VCN order (`extentsOf`, `fragmentCount`). |
| `src/core/snapshot/FreeRunList.*` | Sorted, merged free runs (`find`, `overlapping`, `largest`). |
| `src/core/snapshot/Snapshot.*` | The aggregate the map, classifier and planner read; `snapshotMemoryBytes()`. |
| `src/core/snapshot/SnapshotBuilder.*` | Validating, order-independent builder (sorts, splits long runs, groups by file). |
| `src/core/snapshot/Stripes.*` | `StripeLayout` (fixed LCN stripes, 16 MB aligned, 512 on large volumes), `Stripes` views over extents and free runs, `clip()`, `mapStripes()` / `reduceStripes()` with stripe-order merge. |
| `src/core/synth/SynthRandom.h` | Self-contained seeded generator (xoshiro256**, own distributions) so seeds reproduce everywhere. |
| `src/core/synth/SyntheticVolume.*` | `generateSyntheticVolume()`: seeded snapshots up to 30 TB / 20 M files (class mix, fill, fragmentation, free pattern, $MFT, directory indexes). |
| `src/core/map/BinSummary.*` | `MapBin`, `BinAccumulator` (inline `add()`), per-record `extentMapFlags()`, `packExtentValues()`, `recordBlocks()` (LCN block -> first record), `ownerOf()`: the summary rules the pyramid and the rasterizer share. |
| `src/core/map/Palette.*` | `Rgba` (BGRA in memory), `blendColors()`, `usedShare()`, `Palette` interface over `PixelSummary`, `OccupancyPalette` (the fragmentation mode). |
| `src/core/map/ColorModes.*` | `ColorMode` (fragmentation, age, size), age/size buckets, `newestModification()`, per-file `fileColorValues()` and per-record `extentColorValues()`, legends and `paletteFor()`, `visibleCells()`, exact parallel `countLegend()`. |
| `src/core/map/TileRasterizer.*` | `renderTile()`: 256 x 256 tiles from the pyramid (coarse) or the extent index (fine), no grid lines (see `Grid`), `clustersInRect()`. |
| `src/core/map/Grid.*` | Deep-zoom grid drawn per frame at screen resolution: `gridLines()` (each visible cell's right and bottom edge on the last pixel nearest-neighbour drawing gives it, clipped to the last cluster and the window), `gridOpacity()` (fades in from 5 to 10 px per cell), `edgePixel()`. |
| `src/core/map/HitTest.*` | Hover lookups: `hitTest()` (pointer -> cell -> LCN -> extent or free run, fragment i/n via `fragmentOf()`, the pixel's cells and used clusters), `formatFileTime()`, `describeHit()` tooltip lines. |
| `src/core/map/Selection.*` | Selection math: `fileFragments()` (VCN order, continuing runs joined), `SelectionOverlay` / `selectionOf()` (LCN-sorted merged runs), `shapeOf()` (a run as at most three cell rectangles), `runsTouch()` (dirty-tile test), `highlightRuns()` (paints the selection into a tile), `focusView()`. |
| `src/core/map/MapData.*` | Shared immutable bundle of snapshot, world, pyramid, extent flags, per-record color values and the record block index (`source()` for the rasterizer, `ageReference()`); `TileIdHash`. |
| `src/core/map/Overview.*` | Minimap math: `placeOverview()` (world shape in the bottom-right corner, hidden in small windows), minimap pixel <-> world, view marker box (clipped, minimum size), `dragTo()`. |
| `src/core/map/Ruler.*` | Ruler ticks: `rowTicks()` (row starts: byte offset, % of volume, % of disk when the partition is known), `columnTicks()` (offset within a row), power-of-two steps, `formatOffset()` / `formatPercent()` label precision. |
| `src/core/map/Pyramid.*` | Map LOD pyramid: quadtree of 8-byte `MapBin`s over the world grid (level k bin = one tile pixel at zoom k), parallel build per base-bin row band, 2x2 merges, dirty-range `update()`. |
| `src/core/map/Viewport.*` | View math: `Viewport` (scale, center, size), limits (32 px per cluster to beyond fit), fit, anchored zoom, pan, eased `ZoomMotion` and `PanInertia` by elapsed time. |
| `src/core/map/World.*` | Fixed map world (plan.md 7.1): `roundToNice()` row width from the screen aspect, LCN <-> cell, zoom levels (-5 = 32 px per cluster up to one tile), `blendFor()` (levels to fade between where cells are under a pixel), tile cell rects, row segments of a run. |
| `src/core/util/LruCache.h` | Generic cost-limited LRU cache (map tiles); `keys()` for finding stale entries. |
| `src/core/resources/ResourceCaps.*` | `computeCaps()` (plan.md 6.6 worker and memory caps with reasons), `describeCaps()`. |
| `src/platform/win/SystemResources.*` | `detectSystemResources()` (groups, affinity, job CPU/memory limits, cores, RAM), `LowMemorySignal`. |
| `src/platform/win/ChildProcess.*` | `runCaptured()` (console program without a window, output chunks to a callback, cancel terminates, exit code), `systemProgram()` (System32 path). |
| `src/platform/win/Handle.h` | `UniqueHandle`: move-only owner of a Win32 kernel handle. |
| `src/platform/win/StorageProperties.*` | `queryDiskStorage()` / `queryVolumeStorage()` (seek penalty, TRIM, bus, removable, model via `IOCTL_STORAGE_QUERY_PROPERTY`), `busTypeName()`. |
| `src/platform/win/TextConvert.*` | `toUtf8()` / `fromUtf8()` between the Windows API and the core model, `oemToUtf8()` for console tool output. |
| `src/platform/win/Volumes.*` | `VolumeRecord`, `listVolumeNames()`, `mountPathsOf()`, `openVolume()`, `queryVolume()` (basics, disk extents and disk size, NTFS volume data; failures noted, never thrown), `enumerateVolumes()`, `isElevated()`. |
| `src/platform/win/WinError.*` | `formatWinError()`, `lastWinError()`: Win32 error text. |
| `src/platform/win/LogSinks.*` | `FileSink` (rotating `<name>.log`, flushed per line), `DebugOutputSink`, `defaultLogDirectory()`. |
| `src/services/AppInfo.*` | Product name and version (from `core/Version.h`), `buildDescription()` (configuration, compiler). |
| `src/services/AppServices.*` | Service composition root: resources (and `detectResources()` for a fresh reading), caps, `ThreadPool`, `JobRunner`, drive provider (`DriveSource`: system, or fake with `ICD_FAKE_DRIVES=1`). |
| `src/services/drives/IDriveProvider.h` | Drive list source interface used by GUI and CLI. |
| `src/services/drives/SystemDriveProvider.*` | Real drive list from `enumerateVolumes()`; `toDriveInfo()` (admin-needed badge). |
| `src/services/drives/FakeDriveProvider.*` | Fake drives: SSD, three NTFS HDDs in different states, one exFAT drive, and a 30 TB drive at the design target. |
| `src/services/job/JobRunner.*` | `JobRunner`, `JobContext`, `JobObserver`, `JobResult`: jobs on the pool with cancel, throttled progress, one result. |
| `src/services/job/FakeJob.*` | `makeFakeParallelJob()`: serial + parallel stages with stage names, item labels and a pause gate; stands in for every workflow step in Phase 1. |
| `src/services/trim/TrimJob.*` | `makeTrimJob()` (defrag /L as a job, percentage as progress), `LineSplitter` (CR or LF), `retrimPercent()`, `TrimOutput` (lines for the Log tab). |
| `src/services/workflow/Workflow.*` | Workflow state machine (states, actions, which job to start); no wx, unit-tested. |
| `src/services/workflow/SyntheticAnalysis.*` | Phase 2 stand-in for analysis: synthetic snapshot shaped like the drive, then `MapData` on the job's executor; `AnalysisOutput` hands it to the UI. |
| `src/services/workflow/FakeResults.*` | Phase 1 fake analysis, strategy presets, three-level plan and skipped files. |
| `src/ui/bridge/JobEvents.*` | `JobEventBridge` (job callbacks to `wxQueueEvent`), `jobProgressEvent()`, `jobFinishedEvent()`, payloads. |
| `src/services/AppLogging.*` | `initAppLogging()`/`shutdownAppLogging()`: installs the sinks, writes startup/shutdown lines. |
| `src/precompiled.h` | GUI PCH: `<wx/wx.h>` and std headers only. |
| `src/main.cpp` | `wxIMPLEMENT_APP(icd::App)`. |
| `src/ui/App.*` | `icd::App`: starts logging and `AppServices`, creates the main frame, stops services on exit. |
| `src/ui/MainFrame.*` | Main window: menu, drives panel, map/bottom splitter (bottom keeps its height); creates the workflow binding. |
| `src/ui/WorkflowBinding.*` | Commands to workflow actions, the job per step (TRIM real, analysis synthetic, others fake), progress and results to the panels, TRIM output to the Log tab, command enable state. |
| `src/ui/MainMenu.*` | `createMainMenuBar()`: the section 11 menu as tables; `stateDependentCommands()`. |
| `src/ui/Accelerators.*` | One table of menu shortcuts with a description each (`applyMenuShortcuts()`, `shortcutFor()`, `shortcutText()`, `commandTip()` for tooltips; Ctrl+F go to file, F3 / Shift+F3 fragments, F1 shortcut list), and the map keys of plan.md 7.4 with descriptions (`mapKeyBindings()`, `mapKeyText()`, `mapKeyFor()`, `commandForMapKey()`). |
| `src/ui/dialogs/AboutDialog.*` | Help > About: version, build, detected resources (re-detected), active caps. |
| `src/ui/dialogs/ShortcutsDialog.*` | Help > Keyboard shortcuts (F1): `shortcutLines()` groups the shortcut table by the menu holding each command, then the map keys (keys of one action on one line); shown as a list with bold headings. |
| `src/ui/dialogs/GoToFileDialog.*` | View > Go to file: search field and match list (path, size, fragments); searches on the pool as the user types (200 ms pause, cancelled when replaced), results through a detachable mailbox. |
| `src/ui/Commands.*` | `CommandId` (shared by menus, buttons, accelerators; later ids appended so values stay stable), `isMapCommand()`, `enableCommand()` for menu item + button. |
| `src/ui/panels/BottomPanel.*` | Bottom notebook (Summary, Strategy, Plan, Progress, Log), workflow buttons (tooltips with action and shortcut), the single status line and gauge. |
| `src/ui/panels/SummaryPage.*` | Summary tab: volume figures and per-class table. |
| `src/ui/panels/StrategyPage.*` | Strategy tab: preset picker, effort level, zone list. |
| `src/ui/panels/PlanPage.*` | Plan tab: Fast / Moderate / Maximum side by side, chosen level, warnings. |
| `src/ui/panels/DrivesPanel.*` | All-disks panel: `wxDataViewCtrl` over a virtual model, space bar and TRIM button renderers (tooltip on the drawn TRIM button), selection callback. |
| `src/ui/panels/MapPanel.*` | Map area: toolbar (color mode, overlays still disabled, caption, selected file and fragment, "Find file" search opening Go to file) above the `MapCanvas` with rulers left and above and the legend below; forwards the canvas's view changes to the rulers and the legend; View > Color mode, Minimap, Legend, Go to file, next/previous fragment. |
| `src/ui/map/Legend.*` | Legend strip: swatch, label, size and share of the visible area per entry of the color mode; counts on the pool after the view rests, cancellable, delivered through a mailbox. |
| `src/ui/map/MapCanvas.*` | Drive map widget: Direct2D composition of cached tiles, coarser cached tiles scaled while exact ones render, async tile rendering on the pool via a detachable mailbox (96 in flight, 16 uploads per frame), LRU GPU cache (256 MB), selection highlight painted into tiles with dirty-tile refresh, frame-time log per animation, `missingTiles()` of the last frame. |
| `src/ui/map/MapSelection.*` | Selected file: click (press and release within 4 DIP, not on the minimap) selects the owner of the cluster or clears on free space; fragments in file order, current fragment, next/previous and go to with an animated focus view; observer for the toolbar label. |
| `src/ui/map/MapHover.*` | Hover tooltip: `wxPopupWindow` after a 350 ms rest, follows the pointer, hides on press/drag/wheel/key/leave; local time zone for file times. |
| `src/ui/map/Minimap.*` | Minimap overlay drawn in the canvas's Direct2D frame: holds the coarsest tile as its image, frame and view marker, drag state (grab offset). |
| `src/ui/map/Ruler.*` | Ruler windows (GDI, buffered) beside the map: vertical rows with offset and %, horizontal offsets within a row; sized for their widest label, refreshed on view changes. |
| `src/ui/map/MapNavigator.*` | Map input: minimap drags and hand cursor, drag with inertia, wheel and pinch zoom at the pointer, two-finger pan, double-click zoom, map keys, View zoom commands; 8 ms frame timer. |
| `src/ui/map/D2DRenderer.*` | Direct2D on one HWND: target creation/resize, tile upload as GPU bitmaps, nearest-neighbour or linear drawing with opacity, no edge anti-aliasing (no tile seams), overlay rectangles, rectangle sets blended as one layer (`fillRects`, the grid), device-loss detection. |
| `src/cli/main.cpp` | `icd-cli`: `--version`, `--help`. |
| `tests/CMakeLists.txt` | Fetches pinned GoogleTest, defines `icd_tests`, `gtest_discover_tests`. |
| `tests/core/SnapshotTest.cpp` | Snapshot building, LCN and file lookup, split runs, paths, bad input, free runs, order independence. |
| `tests/core/StripesTest.cpp` | Stripe layout coverage, view coverage on random volumes, parallel = serial on 2-16 threads. |
| `tests/core/SyntheticVolumeTest.cpp` | Seed reproducibility, fill level, fragmented share, free patterns, structure. |
| `tests/core/ViewportTest.cpp` | Pixel/world round trip, fit and limits, anchored zoom, pan clamping, eased zoom motion, durations, inertia. |
| `tests/core/WorldTest.cpp` | Nice widths, landscape/portrait aspect, round trips and edge cells, levels, tile clipping, row segments. |
| `tests/core/ColorModesTest.cpp` | Age and size bucket edges, bucket values, legend counts vs per-cluster brute force (all modes, random rectangles, pool = serial), tile colors = legend colors, visible cells. |
| `tests/core/PyramidTest.cpp` | Base-level choice, every bin vs brute force, dominant class, parallel = serial, dirty-range update = rebuild. |
| `tests/core/TileRasterizerTest.cpp` | Capacity, deep-zoom cells (no grid) on a hand volume, pyramid = extents at the base level, golden checksums, record block index = search and tiles with it = tiles without. |
| `tests/core/SmokeTest.cpp` | `formatBytes()` and `formatCount()` tests. |
| `tests/core/JobTypesTest.cpp` | Cancellation propagation, progress throttling, pause gate, stage clock. |
| `tests/services/TrimJobTest.cpp` | Line splitting across chunks (CR, CRLF), retrim percentages, output queue. |
| `tests/services/WorkflowTest.cpp` | Every workflow transition, Quick, cancel and failure paths, disallowed actions. |
| `tests/core/OverviewTest.cpp` | Minimap placement, hiding, pixel/world round trips, view marker, dragging. |
| `tests/core/RulerTest.cpp` | Tick steps, label formats, row/column ticks against the world math at fit and deep zoom, minor ticks, disk %. |
| `tests/core/HitTestTest.cpp` | Fragment i/n, deep-zoom and free hits, off-volume, brute force at several zooms (hand volume) and every level (synthetic volume), file time and tooltip text. |
| `tests/core/FileSearchTest.cpp` | Ranks, case, path queries (slashes, trailing backslash, mid-name start), empty queries, limit and total, brute force over built paths on a synthetic volume, pool = serial. |
| `tests/core/GridTest.cpp` | Grid fade and edge rounding, no grid at small cells, lines exactly on the last pixel of each drawn cell at random fractional views (also at the partial last row), line count bounded by the window. |
| `tests/core/SelectionTest.cpp` | Fragments vs `fragmentCount()`, overlay merge, run shapes, highlight vs per-cell brute force at every level, refreshed tiles = changed tiles on a synthetic volume, focus view. |
| `tests/core/LruCacheTest.cpp` | LRU eviction by cost, replace/erase/oversize, tile keys; `MapData` build. |
| `tests/core/LogTest.cpp` | Log line format, escaping, level filter, concurrent writes. |
| `tests/platform/ChildProcessTest.cpp` | Output and exit code capture, cancel terminates the child, start failure. |
| `tests/platform/LogSinksTest.cpp` | File sink writing, rotation and reopen in a temp directory. |
| `tests/services/JobRunnerTest.cpp` | Job outcomes, cancel, destructor wait, progress throttling, fake job. |
| `tests/services/ThreadPoolTest.cpp` | Identical reductions for 1..N threads, coverage, errors, fast cancel, nested loops. |
| `tests/core/DriveInfoTest.cpp` | Drive model rules, column texts, badges, durations and ages. |
| `tests/services/FakeDriveProviderTest.cpp` | The fake list covers every row style. |
| `tests/core/ResourceCapsTest.cpp` | `computeCaps()` edge cases (1 CPU, 2 GB, 128 threads, manual values) and `describeCaps()`. |
| `tests/platform/StoragePropertiesTest.cpp` | Media and bus of every physical disk vs `Get-PhysicalDisk`; bus names. |
| `tests/platform/VolumesTest.cpp` | Volume enumeration (Windows drive, disk placement, order), NTFS data vs `fsutil fsinfo ntfsinfo` (elevated only), `toDriveInfo()`. |
| `tests/platform/SystemResourcesTest.cpp` | Detection plausibility and the low-memory signal. |

## Tools

| Path | What it does |
| --- | --- |
| `tools/bench/synth-bench.cpp` | `synth-bench` target: builds a synthetic snapshot (default 30 TB / 20 M files), its color values and pyramid on 8 and all threads, and times tiles per level, whole-volume legend counts, file searches and a selection change's dirty-tile check; `--dump DIR` writes sample tiles as BMP. |
| `tools/spike/map-render-spike/main.cpp` | `map-render-spike` target (T026): times 4K tile composition with wx Direct2D, raw Direct2D and GDI during scripted zoom and pan. |
| `tools/bench/map-bench.cpp` | `map-bench` target (T033): plan.md 7.5 checks on the 30 TB synthetic map: pyramid on 8 threads, map memory, tile times per level and color mode, and the real `MapCanvas` in a window through scripted zoom and pans (fps, stand-in frames); `--no-window 1` skips the window. |
| `tools/testvol/make-testvol.ps1` | Test VHDX volumes through diskpart (admin): `-Action Create` (dynamic VHDX, GPT, NTFS with `-ClusterSize`, `-Letter` or a free one from T down), `Mount` (`-ReadOnly` for no-write checks), `Dismount`, `Remove`, `Info`; files in `%ICD_TESTVOL_DIR%` or `%LOCALAPPDATA%\IroncladDefrag\testvol`. |
| `tools/testvol/verify-testvol.ps1` | `-Action Hash` writes a JSON manifest (every entry: size, attributes, SHA-256 of the main stream and each ADS, reparse targets not followed); `Verify` compares and runs `chkdsk /scan` (exit 1 on any difference); `Chkdsk` alone. |
| `tools/fetch-wx.ps1` | Downloads the pinned wxWidgets prebuilt MSVC x64 archives (Dev, ReleaseDLL, headers), checks SHA-256, extracts to `3rdparty/wxWidgets`. No-op when the stamp matches; `-Force` redoes it. |

## Dependencies

| Dependency | Version | How it is obtained |
| --- | --- | --- |
| wxWidgets (MSW, vc14x x64 DLL) | 3.3.4 | `tools/fetch-wx.ps1`; version and hashes pinned in the script. |
| GoogleTest | 1.18.0 | CMake `FetchContent` in `tests/CMakeLists.txt`, archive SHA-256 pinned. |

## Generated output (git-ignored)

| Path | Contents |
| --- | --- |
| `build/` | clang-cl Ninja Multi-Config build with `compile_commands.json`; `build/bin/<Config>/` holds the exes and the wx DLLs of that config. |
| `build-msvc/` | Optional MSVC (VS 2026) build, same layout. |
| `build/quality/` | Ninja + clang-cl configure for the quality gate; `quality/` holds scope lists and the selected database. |
| `.venv-quality/` | Python venv with the pinned analyzers. |
| `3rdparty/downloads/` | Cached wx archives. |
| `3rdparty/wxWidgets/` | `include/`, `include/msvc/`, `lib/vc14x_x64_dll/` (import libs, debug and release DLLs), `icd-wx-stamp.txt`. |
