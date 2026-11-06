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
- T043 Media detection - `platform/win/StorageProperties.*` (seek penalty, TRIM, bus type, removable, model on the
  physical disk, no elevation needed), `core::StorageFacts` and `classifyMedia()` (virtual disks first, then seek
  penalty, TRIM as a hint, else Unknown), `MediaType::Virtual` keeps the full workflow for VHDX test volumes, Media
  column "HDD · SATA" (`describeStorage()`). `StoragePropertiesTest` matches `Get-PhysicalDisk` on every disk.
- T044 SSD TRIM action - `services/trim/TrimJob.*` (Windows `defrag <vol> /L /U /V` from System32 as a cancellable
  job; CR/LF line splitting, OEM output to UTF-8, retrim percentage as progress, other lines to the Log tab),
  `platform/win/ChildProcess.*` (`runCaptured()`, `systemProgram()`), `oemToUtf8()`. TRIM of the F: SSD from the GUI
  finished in 5.9 s with Windows' report in the Log tab; HDD, virtual and unknown rows never offer TRIM.
- T041 Scenario generator - `tools/testvol/gen-scenario/` (`gen-scenario` target): seeded dev, archive, photos, video,
  mixed, almost-full, vss (WMI shadow copy) and special (compressed, sparse, hard/symbolic links, junction, ADS, EFS,
  resident, 1,324-fragment file, big directory) scenarios with interleaved writes, deletes and appends; refuses
  non-ICD-TEST volumes. Two runs per scenario on fresh 4 GB volumes give identical content; fragment counts are
  identical on special/archive/photos/video and within a few files on the small-file scenarios (NTFS timing).
  verify-testvol hashes in batches and handles long paths and empty volumes.
- T045 Volume bitmap to free runs - `core/snapshot/BitmapToRuns.*` (word-at-a-time scan, edge stitching, parallel
  grains equal to serial), `platform/win/VolumeBitmap.*` (chunked `FSCTL_GET_VOLUME_BITMAP`, `readFreeRuns()` with
  32 MB pieces converted on the pool). Free total = NTFS free clusters and = `fsutil volume diskfree` + reserved on
  4 KB and 16 KB test volumes; 4 KB pieces give the same runs.
- T046 USN enumeration - `platform/win/UsnJournal.*`: `queryUsnJournal()` (journal ID, first/next/lowest valid USN;
  none when inactive, never created by analysis), `enumerateUsnFiles()` (`FSCTL_ENUM_USN_DATA`, works without a
  journal). Every entry a directory walk finds on the `special` and `photos` test volumes is enumerated with the same
  reference, name and parent; the Windows drive journal is read.
- T047 API extent scan - `services/scan/ApiScanner.*` (USN list plus system records, files opened by id in MFT order,
  metadata, stream list and retrieval pointers per stream, special-case flags, USN cursor in the snapshot),
  `platform/win/RetrievalPointers.*`, `platform/win/Privileges.*` (backup privilege for System Volume Information),
  `core/snapshot/ScanReconcile.*` (newer read wins, stale free runs trimmed, unclaimed clusters to an unmovable
  `$Unattributed` placeholder). On the `special` volume every special case is flagged, all clusters are accounted
  for, and sampled extents equal `fsutil file queryextents`.
- T048 Analysis service and live map - `services/scan/AnalysisJob.*` (bitmap first with an immediate map, API scan
  with partial maps every 3 s, final map and summary; cancellable), `core/snapshot/SnapshotStats.*` (fragmentation and
  free-run figures), `MapCanvas/MapPanel::updateMap()` (newer data keeps the view, tiles redraw without blanking), the
  Summary tab (existing `ui/panels/SummaryPage`, not a new `ui/tabs/SummaryTab`) with real-scan rows, ruler "% of disk",
  real Analyze for real drives (synthetic stays for `ICD_FAKE_DRIVES`). GUI: X: (2.3 TB, 112 K files) analysed in
  14.7 s without blocking; cancel stops it and clears the half-read map.
- T049 CLI analyze - `icd-cli analyze <vol> [--json] [--dump-extents]` (`cli/commands/Analyze.*`): runs the shared
  `services::analyzeVolume()` (the GUI job now wraps it), progress on stderr, Ctrl+C cancel (`platform/win/Console.*`);
  report from `services/scan/AnalysisReport.*` with a small `core/format/Json` writer. The JSON summary of the
  16 KB-cluster photos test volume equals the GUI Summary tab figure for figure.
- T050 Analysis verification - `tests/integration/analysis/check-analysis.ps1`: per scenario a fresh 4 GB test volume,
  remounted read-only (no writes possible), analysed by `icd-cli analyze --json --dump-extents` and compared with
  `fsutil volume diskfree` and `fsutil file queryextents` (sampled files), plus every-cluster accounting; `-RealDrive`
  for a data drive. All 8 scenarios and the X: data drive (109 K files, 3.8 s) pass; `quality.bat all` passes.
  **Phase 3 exit.**
- T060 MFT record parser - `core/ntfs/*` (pure C++): update sequence fixups, header checks, `$STANDARD_INFORMATION`,
  `$FILE_NAME`, every non-resident attribute with its run list (`decodeRunList()`: holes, deltas, coverage and
  overflow checks), resident `$DATA`/`$ATTRIBUTE_LIST`/`$BITMAP`, attribute lists, UTF-16 names like
  `WideCharToMultiByte`. `NtfsParserTest` on handcrafted records (`NtfsRecordBuilder.h`), records captured from the
  `special` scenario (`tests/data/ntfs`, `tools/testvol/capture-mft-records.ps1`) and 20,000 fuzzed copies.
- T061 MFT reader pipeline - `services/scan/MftScanner.*` (map of `$MFT` from record 0 and its extension records,
  `$MFT::$BITMAP`), `core/ntfs/MftLayout.*` (read plan: 16 MB cluster-aligned chunks in volume order, unused stretches
  skipped, split records), `platform/win/MftReader.*` (overlapped unbuffered reads, 3 in flight, bounded `BufferPool`,
  busy time and device counters), parser tasks into per-chunk `MftRows`, `MftMerge` (files, names, parents, metadata,
  stream ordinals, flags, reconcile; any executor gives the same snapshot), `ScanRules` shared with the API path,
  `ScanCompare` and `icd-cli compare-scanners`, `icd-cli analyze --scanner/--threads`. `special` volume scans end to
  end (1 = 2 = 8 workers); X: (112 K files) reads `$MFT` with the disk 100% busy, 3.9x faster than the API scan.
- T062 Attribute lists and path resolution - `services/scan/MftSecondPass.*` (`joinExtensionRecords()`: resident or
  read-from-disk attribute lists accept only listed, current extension records; stale ones go to `$Unattributed`),
  merge folds names, sizes and segments of accepted parts into the base file; parents resolved per file in parallel
  in the merge. Access times compared as "deferred" when the record is older than memory. `special` (1,324-fragment
  file over five extension records, compressed logs) and X: now equal to the API scan; `MftSecondPassTest`.
- T063a Scale volume generator - `tools/testvol/make-scalevol/` (split from T067: real-drive tests use only E: and X:):
  millions of allocated, never written files (`SetEndOfFile` + `SetFileValidData`, cluster-rounded log-uniform sizes,
  interleaved growth for a fragmented share) on ICD-TEST volumes. 1 M files in 294 s; the thin VHDX still grows by
  about one block per file (NTFS writes where each file's data begins), 139 GB for that volume.
- T063 Scanner cross-check - `tests/integration/scanner-equivalence/check-scanners.ps1` (every scenario read-only
  through `compare-scanners` for 1, 2 and all workers, real drives E: and X:, scale volume with a speed-up check); all
  agree. Analysis now defaults to the MFT scanner with the API scanner as fallback, and the MFT scan feeds partial
  maps. 1 M-file scale volume: API scan 23.5 s, MFT scan 0.48 s (49x; the VHDX sits on an SSD, $MFT 1 GB read with the device 99% busy).
- T064 Snapshot persistence - `core/snapshot/SnapshotIO.*` (versioned file: checksummed header with `SnapshotMeta`
  incl. USN cursor and map aspect, payload of in-memory arrays in CRC-32 blocks compressed in parallel, full
  validation on load), `core/util/Crc32.h`, `platform/win/Compression.*` (XPRESS via the Windows Compression API),
  `services/snapshot/SnapshotStore.*` (per-volume files, atomic replace). Round trips are identical; damaged,
  truncated, foreign and other-version files are rejected cleanly (`SnapshotStoreTest`).
- T065 USN catch-up - `services/snapshot/UsnCatchUp.*` (rescan rules; journal from the saved cursor; changed,
  parent and metadata records re-read through `readMftRecords()` with their extension records; saved drafts replaced
  via the new `draftFiles()`/`finishDrafts()` split of the merge and `draftsOfSnapshot()`/`combineDrafts()`; free space
  and dirty ranges), `platform::readUsnJournal()`, analysis catches up and saves snapshots, `MapData::update()` for
  dirty ranges, `icd-cli check-catchup`, `tests/integration/catchup/check-catchup.ps1`: after every round of scripted
  changes the catch-up equals a full rescan (access times aside, which NTFS does not journal).
- T068 Fast and slow test split - CTest labels `fast`/`slow` (`ICD_SLOW_TESTS` in `tests/CMakeLists.txt`: tests on
  real volumes and disks), `test.bat [config] [fast|slow|all]` running only fast tests by default (about 260 in seconds),
  the USN enumeration test limited to scenario volumes, integration scripts defaulting to `special` + `mixed` on 2 GB
  volumes (`-Scenarios all` for every scenario). The quick run found TxF files under unlisted `$Extend` folders, now
  accepted by `compareWithApi()`.
- T066 Memory modes - `core/resources/MemoryPlanner.*` (estimate from the MFT record count, the plan.md 6.6 ladder,
  give-way), `core/util/LargeArray.h` (allocator that moves with its container) for the extent index and name pool,
  `platform/win/MappedMemory.*` (delete-on-close mapped files), `services/snapshot/MappedStorage.*` (storage per plan,
  `MemoryGovernor` on the low-memory signal), analysis plans and reports its memory mode, `icd-cli check-caps`. 2
  workers, 2 GB budget and mapped storage give exactly the uncapped result on the special and 1 M-file volumes.
- T067 Scale volume and benchmarks - `tools/bench/scan-bench.cpp`; 30 TB thin VHDX (8 KB clusters, 2 M files from
  `make-scalevol`, 51 GB on the host): bitmap 0.16 s, MFT scan 1.58 s with the disk 100% busy, 181 MB snapshot
  (about 4.5 GB estimated at 20 M files), catch-up of 10,000 changes 1.14 s and equal to a rescan, capped mapped scan
  identical; on 1 M files the MFT scan is 49x faster than the API scan. Catch-up at 20 M files extrapolates to about
  11 s: follow-up T069. `quality.bat all` passes. Both scale volumes deleted afterwards. **Phase 4 exit.**
- T069 Catch-up at 20 M files - the catch-up finish patches the saved snapshot instead of finishing every file
  again: `core/snapshot/SnapshotPatch.*` (`patchSnapshot()`: kept files by base index with new parents and flags, extent
  records copied in one parallel pass with remapped file indexes and new records merged by LCN, released clusters
  freed, `$Unattributed` remainder kept, new names into the saved pool; refuses whatever a fresh build could do
  differently), `mft::finishCatchUp()` (parents and flags by the full finish's rules), full finish as fallback.
  Synthetic 20 M files: 32 s -> 2.0-2.8 s on 8 workers, same snapshot (`tools/bench/catchup-bench.cpp`). 10 M-file
  scale volume on the E: HDD: catch-up 3.51 s and equal to a rescan. Fixed on the way: re-reads naming extension
  records read the wanted set again in one plan (interleaved chunks at 10 M files), `RowIndex` orders chunks by their
  first row, MappedStorage tests use their own folders. `CatchUpPatchTest`, `SnapshotPatchTest`. **Phase 4 exit.**
- T070 Rule engine - `core/classify/RuleEngine.*`: ordered rules (first match wins) over path globs (`*`, `?`, `**`,
  anchored or not, case-insensitive), extensions, size and age ranges, file flags and churn; per-directory overrides
  (deepest wins, regular files only); all globs as one automaton stepped once per directory level (parallel) and once
  per file name, extensions looked up once per distinct name; `classifyPath()` equals `classify()`. `RuleEngineTest`.
- T071 Built-in class catalog - `data/classes.json` (every class of plan.md 8.3-8.6 plus NTFS metadata and
  directories, colors, descriptions, ordered rules), embedded by `cmake/EmbedText.cmake`; `core/classify/Catalog.*`
  (strict `parseCatalog()`, `builtinCatalog()`), `core/format/JsonReader.*` (JSON reader for data files).
  `CatalogTest` (one test per class, typical and edge cases), `JsonReaderTest`.
- T072 Last-access reliability - `platform/win/LastAccessSetting.*` (registry `NtfsDisableLastAccessUpdate`, decoded;
  equals `fsutil behavior query disablelastaccess`: this machine system managed, on), `core/classify/AccessTimeHeuristic.*`
  (files read over an hour after creation and last change; setting plus evidence give reliable, unreliable or unknown
  with a reason); "Last access on/off" badge on every NTFS drive, verdict on the Summary tab and in the JSON report.
  `AccessTimeHeuristicTest`, `LastAccessSettingTest`.
- T073 USN churn statistics - `services/activity/ChurnStats.*`: the journal's retained window as sessions per file and
  per directory (creations and data changes from close records, deletes from their first record and in the directory
  the file lived in, since POSIX deletes move files into $Extend\$Deleted); a directory churns with 10+ deletes at a
  quarter of its creations, or with some deletes when most siblings churn; flags spread below churning directories.
  `icd-cli churn`, dev scenario with repeated rebuilds of the same objects and temp churn in `.cache`,
  `tests/integration/churn/check-churn.ps1` (all 69 build and 3 temp directories, nothing else). `ChurnStatsTest`.
- T074 Class map mode and summary - analysis classifies every file (built-in catalog, churn from the journal) into
  `AnalysisResult::classes` and the Summary tab's class table (files, bytes on disk; JSON `summary.classes`, dumped files'
  `class`); `services/scan/Classification.*`; `ColorMode::FileClass` (catalog colors, legend, per-record class bytes for
  tiles, `MapData::update()` follows class changes on unmoved extents), File class enabled in the View menu and toolbar,
  "Class:" in the tooltip. `tests/integration/classes/check-classes.ps1` on `mixed` (2 and 4 GB): every class right,
  bytes and counts add up. `ClassificationTest`, class cases in `ColorModesTest`, `MapUpdateTest`, `HitTestTest`.
  `quality.bat all` passes. **Phase 5 exit.**
- T075 Per-drive strategy memory - `services/settings/Settings.*` (versioned JSON, per-drive preset id and effort
  keyed by volume GUID) and `SettingsStore.*` (`%LOCALAPPDATA%\IroncladDefrag\settings.json`, atomic save, damaged
  file kept as `.bad`), owned by `AppServices`; stable `StrategyPreset::id` and `presetIndex()`; `StrategyPage::select()`;
  `WorkflowBinding` restores the choice on drive selection and saves it on each change. `SettingsTest`.
- T080 Zone script model - `core/strategy/ZoneScript.*`: zones (selector over classes, globs, extensions, size, age, flags, churn; anchor; order with per-level overrides; action; `fragmentOk` or whole file; gap as the largest of free/volume share, size and class multiple), `Effort`, strict `parseZoneScript()` reporting every issue per zone, `validateScript()`, `zoneScriptJson()` round trip; `parseSizeSpec()` / `sizeSpecText()` in `core/format/ByteSize`, `fileFlagNamed()` / `fileFlagNames()` in the catalog. `ZoneScriptTest`.
- T081 Target layout resolver - `core/planner/ZoneAssignment.*` (first matching zone per file, roles Fixed/InPlace/Defragment/Placed, movable clusters, per-zone need) and `core/planner/TargetLayout.*` (`AvailableAxis` numbering usable clusters around obstacles, forward zones from the front with gaps, back zones ending at the volume end, slack placement input, gaps shrunk and MFT zone given up with notes). `tests/core/PlannerTestUtil.h` draws volumes as pictures. `TargetLayoutTest`.
- T082 Plan model, simulator and validator - `core/planner/Plan.*` (moves of whole files' movable clusters, flat runs, lower bound / extra / skipped / deferred accounting, totals), `core/planner/HoleSet.*` (mutable free runs in blocks with a max-length tree, fits both ways), `core/planner/Validator.*` (replay with every plan.md 9.5 check). `HoleSetTest`, `ValidatorTest` (a broken plan per rule).
- T083 Lower bound and target fitting - `core/planner/LowerBound.*`: `lowerBound()` (must-move files: outside their zone or with fragments under `fragmentOk`, whole files at Maximum; per zone), `fitTarget()` (slack before each forward zone chosen by a dynamic programme over stay intervals so zones move towards the files; front zones fixed). `LowerBoundTest` (known minimums, brute force over every slack split).
- T084 Good-enough filter and defragment-only - `core/planner/CandidateFilter.*` (must-move files per zone and defragment-only), `core/planner/Placement.*` (first/last/nearest fits and large pieces on a HoleSet, never over the file's own runs), `core/planner/DefragOnly.*` (largest first, nearest run or large pieces, skips with reasons, vacated space reused). `DefragOnlyTest`: plans validate and untouched files are exactly the good-enough set on synthetic volumes.
- T085 Fast-fill planner - `core/planner/FastFill.*`: `fastFill()` fills a zone's holes from its packed end with the largest file that fits (union-find over size-sorted files), then single runs or large pieces, overflow for the rest; `Destination` and `appendMoves()` in Placement. Bytes equal the lower bound on test layouts; synthetic destinations checked statically. Per-stripe splitting of very large zones is left to T093 (planning orchestration). `FastFillTest`.
- T086 Sort planner (weighted LIS) - `core/planner/SortZone.*`: `SortKeys` per order (directory+name ranks), `weightedLis()` (Fenwick, = brute force), `applySortBounds()` (anchors stay, the rest join the lower bound; Moderate tolerance), `sortZone()` (movers between anchors in key order, anchors given up as extra when a gap is short). `Move::extra` / `Destination::extra` feed the validator's extra. `SortZoneTest`.
- T087 Pack-to-back planner - `core/planner/PackToBack.*`: back zones fast-filled from the end without order, sorted (age, oldest last) at Maximum. `PackToBackTest`: packed videos never move at any level, holes closed, age order at Maximum; plans validate.
- T088 Move ordering and cycles - `core/planner/MoveOrdering.*` (dependency graph, Kahn in destination order, Tarjan components, park the smallest file at Maximum or drop the cycle with its dependents), `core/planner/MakeRoom.*` (`findRoom()`: move the smaller thing). `MoveOrderingTest`: chains, swaps, three-cycles, dependents, no scratch; every plan validates (each file once except reported parking).
- T089 Budgets and benefit ranking - `core/planner/Budget.*`: move benefit (fragments removed, zone reached, hole closed), `TimeModel` estimate, `applyBudget()` takes moves by benefit per byte with their dependencies within cluster/time budgets and orders the kept ones most valuable first; deferred clusters reported. `orderMoves()` now drops destinations on files that stay. `BudgetTest`: every prefix validates and improves fragmentation.
- T090 VSS detection and alignment - `platform/win/ShadowCopies.*` (vssadmin listing, count of the volume's GUID path; unknown without rights), `core/planner/VssAlign.*` (16 KB unit per cluster size, aligned-move test, destinations shifted onto aligned positions, unaligned count and growth estimate). `VssAlignTest`, `ShadowCopiesTest` (the vss-scenario check is a slow, elevated test; this session was not elevated, so it skipped).
- T091 Fast level - `core/planner/levels/FastLevel.*` (stripe scores from churn, small holes and fragmentation; bridged hot spots; hot spots packed towards their head; big long-lived files to their zone; 2% / 15 min budget times the multiplier; run-again note) and the shared `core/planner/LevelPlanner.*` (`PlanInput`, `LevelPolicy`, metrics, `planZones()` pipeline for Moderate/Maximum, zones in parallel on pool slices). `FastLevelTest`.
- T092 Moderate and Maximum levels - `core/planner/levels/ModerateLevel.*` (zone fragment_ok, 20% sort tolerance, eviction, no parking, 20% budget) and `levels/MaximumLevel.*` (whole files, exact order, parking, no budget); placement re-planned in up to three rounds without files that found no space; cycles at non-parking levels broken by sending the smallest file to free space; parked files whose return is dropped stay parked. `LevelsTest`: Moderate within Maximum's bytes and goals on synthetic volumes, all plans validate.
- T093 Parallel planning orchestration - `services/plan/PlanJob.*` (`planLevels()`: zones once, three levels in parallel on the pool, each validated; `makePlanRequest()`, `planSummary()`, `makePlanJob()`), `tools/bench/plan-bench.cpp`. Parallel = serial plans (`PlanJobTest`). Scaling fixes in ordering (back-edge cycle breaking), fast-fill slices, room finding, pool building, HoleSet empty blocks, budget closures; re-planning rounds and a Maximum scratch reserve. 6 M files plan in 42 s on 8 workers; the 30 TB / 20 M target was split off as T093b.
- T094 Strategy presets - `data/strategies/*.json` (mixed, development, archive, photos, video, defrag-only and the eight general strategies of plan.md 8.8) embedded per file, `core/strategy/Presets.*` (`builtinPresets()`, `builtinPresetIndex()`). `tests/core/presets/PresetsTest.cpp`: every preset at every level on six random synthetic volumes validates.
- T095 Planner optimality checks - `tests/core/OptimalityTest.cpp` (exhaustive minimum over every slack split and placement on 600 tiny volumes: lower bound never above it, fully reaching plans never below, parking-free plans exactly at it; preset ratios on synthetic volumes: Fast and Moderate <= 1.1 for every preset, Maximum <= 1.1 for the storage presets), `tests/integration/planner/check-planner.ps1` (scenario volumes through `icd-cli plan`; not run here: needs an elevated session). Zone-less cycles redirect at Maximum. Remaining Maximum extra for whole-volume sort/pack presets split off as T095b.
- T096 CLI plan - `src/cli/commands/Plan.*` (`icd-cli plan <vol> --strategy --level --json --moves --threads`, `--synthetic N` without a real volume), `services/plan/PlanReport.*` (`planJson()`, `planText()`). `PlanReportTest`: the CLI's report equals the GUI job's for the same snapshot.
- T097 Strategy tab - `ui/panels/StrategyPage.*` (kept instead of `ui/tabs/StrategyTab`): built-in preset picker, effort level, editable zone table (classes, action, anchor, order, order at Maximum, fragment_ok, gap after, so the churn gap too) read through new zone-table text forms in `core/strategy/ZoneScript` (`gapText()`, `fragmentOkText()`, `idsText()`, `*Named()` and parsers); problems from `validateScript()` show under the table and keep Build plan disabled; remembered choices use the built-in preset ids (the Phase 1 fake preset list is gone).
- T098 Plan tab and plan overlay - `core/map/PlanColorMode.*` (`MapOverlay` layers painted into tiles, `planOverlay()`: vacated and filled clusters) and `core/map/ZoneFitColorMode.*` (`zoneFitOverlay()`: targets and data outside its zone) as the zone-fit and plan-preview color modes (the Phase 1 overlay checkboxes are gone). The GUI plans the shown map for real (`makePlanJob()` classifies when needed and builds per-level overlays), the Plan tab shows `planSummary()` for the three levels with Fast's "run it again" note, Review switches to plan preview, and overlays clear whenever the plan is dropped. `PlanOverlayTest`: every pixel of the preview matches the plan's moves. Phase 6 exit.
- T095b Fewer extra moves at Maximum - `core/planner/CycleGuard.*` (wait graph of chosen destinations in a Pearce-Kelly
  topological order, bounded searches with a shared visit budget): fast-fill picks, single-run placement, eviction and
  next-to-zone overflow avoid closing dependency cycles; one guard per round with zones placed in script order. Sorted
  zones: anchors are the heaviest chain of files whose offset from their packed position stays within the zone's room
  and never falls (`applySortBounds()` with the layout; `usableSpan()` public), so the bound counts files no packed
  layout keeps and anchors are never given up; `LayoutInputs::room` (a tenth of a sorted forward zone's need from the
  slack); the sorter skips cycle-closing positions where its group has room. Ordering at Maximum breaks components of
  up to 2000 moves greedily (`LocalCycles`: smallest move of every cycle left). OptimalityTest at Maximum: fast-fill
  presets <= 1.1 (free-space 1.29 -> 1.06, hot/cold-zone 1.28 -> 1.07, type-segregation 1.33 -> 1.04, archive 1.07 ->
  1.06), whole-volume sorts 1.5-2.1 -> 1.23-1.44 (threshold 2.5 -> 1.5); the rest split off as T095c.
  `CycleGuardTest`; SortZone and PackToBack tests count files off their packed places in the bound.
- T093b Planning at scale: Fast pieces, ordering stall, metrics - Fast plans hot spots in pieces of at most 8 stripes,
  hottest first until their movers reach twice the budget (30 TB: 131 s -> 25 s); at Maximum a zone-less cycle move
  that no free run holds is parked instead of ending the round (4 M files: ordering 900 -> 18 rounds, 210 s -> 10 s,
  a third of the bound no longer skipped); `measurePlan()` free runs by set algebra, `parallelSort()` in
  `core/exec/Executor.h`, `HoleSet` skips sorting sorted input, validator per-file arrays instead of hash maps.
  30 TB / 20 M files: 369 s with validation (T095b's guard costs zone placement time); the 60 s target split off as
  T093c. `FastLevelTest` hot-volume pieces; mixed moved to OptimalityTest's 1.5 group (it now completes its bound).
- T100 Move primitive - `platform/win/MoveFile.*`: `moveClusters()` (one `FSCTL_MOVE_FILE`), `moveClustersChunked()`
  (calls of N clusters, `keepGoing` between them), `MoveError` mapping (destination in use via a bitmap probe, locked,
  unsupported, file gone, retry), `openVolumeForMoves()`, `openStreamForMove()` (scanner stream ordinals, named streams
  by path with the file id checked), `freeRunsIn()`, `setBackgroundIo()`. `tests/platform/MoveFileTest.cpp`: error
  mapping (fast); `ClusterMove.MovesFilesOnTestVolumes` (slow, elevated: file, named stream and directory index moved
  in chunks, bytes read back unbuffered) - not run here: the session was not elevated.
- T101 Revalidation before execution - `core/execute/ExecutionPlan.*` (plans by file reference with identity, regions,
  paths), `core/execute/MovePieces.*` (`matchMove()`: a file now against a move, done and pending pieces, changed files
  refused; `remapMove()`), `core/execute/PlanReplay.*` (`replayPlan()` on the free space and files now: gone, changed
  and blocked moves dropped with everything that waited on them), `services/execute/Revalidate.*` (`planRefusal()`:
  other volume, reformatted, resized, older than 24 h; `revalidatePlan()`: catch-up or full scan, then the replay).
  `tests/services/SimVolume.h` (volume in memory), `MovePiecesTest`, `RevalidateTest` (scripted deletes, changes and
  taken destinations detected; the kept moves still run). The scripted check on a real volume is part of T104.
- T102 Executor - `services/execute/Executor.*` (one mover thread plus a prefetch helper; runs re-read and matched
  before each move; 16 MB calls, whole units for compressed files via `core/execute/MoveCalls.*`; pause, resume and
  cancel between calls; throughput limit; low I/O priority; stale-bitmap replacement in the move's region avoiding
  the MFT zone and later destinations; skip reasons; verification against landings), `MoveDevice.h`,
  `VolumeMover.*` (FSCTL_MOVE_FILE device), `ExecutedSnapshot.*` (`snapshotAfter()`: snapshot patched from verified
  runs, else none). `tests/services/SimDevice.h`, `ExecutorTest`, `MoveCallsTest`. Pause, resume and cancel mid-run
  are covered on the simulated volume; the run on a real test volume is T104's integration script.
- T103 Execution log and resume - `services/execute/ExecutionLog.*` (append-only JSON lines: the whole plan first, run
  events, dropped, begin and outcome lines with the runs read back, end; cut-short last line tolerated; `movesToResume()`,
  `unfinishedExecutionLog()`), `services/execute/Execution.*` (`runExecution()`: revalidate, log, remove the stale saved
  snapshot, execute on the volume, save the snapshot after; `freshExecution()`, `resumedExecution()`), one-line mode in
  `core/format/Json`. `ExecutionLogTest`: a crash mid-move (simulated) resumes from the log and completes the plan; the
  real kill-and-resume run is part of T104's integration script.
- T104 CLI execute and integration suite - `cli/commands/Execute.*` (`icd-cli execute`: plan now, `--plan FILE`,
  `--resume [--log FILE]`; chunk, priority, throughput, age and prefetch options; `--check-rescan`; test hooks
  `--pause-after`, `--cancel-after`, `--take-destination` for a real stale-bitmap conflict), `icd-cli plan --save FILE`,
  `services/execute/ExecuteReport.*` (`checkAgainstRescan()`: moved files where predicted after a full rescan, saved
  snapshot vs rescan; JSON and text reports with drops, problems, zones), shared CLI helpers (`effortNamed()`,
  `presetNamed()`), `tests/integration/execute/check-execute.ps1` (per scenario: changes between plan and execute,
  main run with a stale bitmap, pause/cancel/resume, kill/resume, churn-gap measurement on `dev`; hashes, chkdsk and
  rescan after each run), `ExecuteReportTest`. Not run here (session not elevated): the exit run is T104b.
- T104b Phase 7 exit run on the test bed - elevated runs: `ClusterMove.*` and the slow suite pass; `check-execute.ps1
  -Scenarios all` passes every check (first on 4 GB volumes with whole plans, up to 52 K moves per run; then the
  bounded version). Found and fixed: cancelled runs resumable (`resumable()`); catch-up always re-reads records 0-15,
  metadata and `\System Volume Information` (shadow storage grows unjournaled) and reports "up to date" only when they
  kept their clusters; analysis rescans when a catch-up disagrees with the bitmap; `\System Volume Information` pinned
  in the planner; a refused `FSCTL_MOVE_FILE` keeps the prefix before the taken cluster (`landedPrefix()`, recorded
  and verified; `ClusterMove.RefusedCallKeepsAPrefix`); "destination-in-use" when no replacement fits; `compareExact()`
  compares records, not positions; executor log lines (progress, refusals with details, totals). The integration
  script now runs about 12 min for all scenarios (1 GB volumes, `icd-cli execute --max-moves`, hashes and chkdsk once
  per scenario, resume and kill on `mixed`, `-ChurnGap` opt-in). Churn gap measured (67% of new build output in a gap
  holding 87% of free space); the decision is T150. **Phase 7 exit check.**
- T110 Execute controls and Progress tab - `services/execute/ExecuteJob.*` (`makeExecuteJob()`: execution plan of the
  reviewed level, `runExecution()` with pause; `ExecutionFeed` with counts, bytes, current file, Log lines in words
  with a cap, moved records, summary), `ui/dialogs/ConfirmExecuteDialog.*` (figures, warnings, safety note, low I/O
  priority, throughput limit, Cancel default), `ui/tabs/ProgressTab.*` (stage, done, elapsed, time left, throughput,
  workers, current file, moves, resources), `ui/ExecutionView.*`, `core/job/RateMeter.*`, `ThreadPool::busyTime()`,
  elevation flow (`services/Elevation.*`, `platform::relaunchElevated()`, `--drive`), `PlanOutput::takeRequest()`,
  `onRevalidated` hook; Log tab gains a Result column. `ExecuteJobTest`, `RateMeter` and busy-time tests. Run from
  the GUI on a 1 GB `mixed` VHDX: Fast, Maximum (2,959 moves) with pause and resume, a cancelled Maximum; hashes and
  chkdsk clean.
- T111 Live map and Log tab - `services/execute/LiveMap.*` (`applyMoves()`: snapshot copy patched with read-back runs,
  pyramid over the moved files' clusters only; `rebaseMap()` on revalidation's volume when files changed after
  planning), `ui/map/MapLiveUpdates.*` (ticker, one pool update at a time spaced by its cost, mailbox),
  `MapCanvas::updateMap(map, dirty)` (only touched tiles, selection follows its file), `MapSelection::refresh()`,
  `ui/tabs/LogTab.*` (counts per result, follows new rows); `ExecutionFeed::takeVolumeNow()`. `LiveMapTest`. GUI
  runs on a 1 GB `mixed` VHDX: the map follows every move (40 updates, slowest 14 ms); deleted and grown files
  dropped and listed with reasons. **Phase 8 exit check.**
- T113 GUI feedback from the first real-drive run - right-click menus: drives (the bottom bar's steps from one table,
  `workflowCommands()`, enabled like the buttons; TRIM on SSDs) and map files (Show in Explorer via
  `platform/win/Shell` / `services/ShellActions`, Copy path; on the right-button release, since the canvas gets no
  context-menu event from the mouse); time left shown every 3 s as an average (`core/job/SmoothedEstimate`), restarted
  per stage and on Resume; Log rows stamped when they happen and taken while paused; the legend keeps its numbers
  across live updates (`Legend::updateMap()`); `ui/CallbackTimer.h`. `SmoothedEstimate` and `ShellActions` tests.
  Checked in the GUI on the T: test VHDX with real mouse input. First real-drive run (X:, Moderate): 22,038 moves,
  82 GB at 54 MB/s, cancelled by the user, nothing skipped or unverified.
- Gate G2 First real-drive run - X: (2.32 TB HDD, photos), `photos` preset at Moderate from the GUI: 22,038 moves
  (82 GB, cancelled) then 7,638 (99.6 GB), nothing skipped or unverified; fragmented files 125 to 0, the largest free
  run 1.85 TB; chkdsk clean with identical totals; moved files open. Recorded in `.codex/arch.md`.
- T093c Planning at the 30 TB scale target - `plan-bench --threads 8` (30 TB / 20 M files, validation on) 57-59 s on
  the 8 performance cores (was 369 s; 63 s unpinned), `--compare 1` identical plans (one worker 178 s, 3.1x: the
  levels are serial pipelines, so the 5x of the task text is not reached). Stage times in `PlanResult::stages`, plan
  fingerprints in the bench; parallel validation per LCN stripe with a serial fallback, lazy and parallel room search,
  flat arrays and live-only rounds in ordering, shared sort keys, parallel pools, items and budget steps; plans
  unchanged except the cycle guard budget (64 -> 4 visits per destination, 0.03% more moves).
- T095c Cycle breaking parks less at Maximum - `src/core/planner/MoveOrdering.cpp` (`breakGreedily()`: per round each
  cycle gives up the move with the fewest clusters per wait through it, then chosen moves whose return closes no cycle
  are kept in place). Worst Maximum ratio on the OptimalityTest volumes 1.44 -> 1.30 (development now within 1.1, test
  limit 1.5 -> 1.35); `check-planner.ps1 -Scenarios all`: every scenario within 1.1 except `archive` at Maximum (1.377
  -> 1.216). The 1.1 x goal for sorted zones moved to T095d.
- T122 Unsupported configuration warnings - `core::MoveBlock` / `moveBlocks()` / `canMove()` (RAID and Storage Spaces
  bus, dynamic disk or several disk extents, removable, BitLocker), `platform/win/VolumeTopology.*` (LDM partitions via
  `IOCTL_DISK_GET_DRIVE_LAYOUT_EX`, BitLocker states from WMI `Win32_EncryptableVolume`), badge "No moves (...)",
  `movesBlockedText()` in the plan-ready status and `icd-cli execute`, `DriveKind::AnalyzeOnly` (analysis and plans,
  never Execute). `tests/integration/topology/check-topology.ps1` (temporary Storage Spaces pool: planned, execute
  refused); dynamic disks cannot be created on current Windows, the other setups are unit tests on the facts.
- T120 Windows scheduled optimization - `platform/win/ScheduledOptimize.*` (the drive choice lives in the ScheduledDefrag
  task's arguments, `-c` / `<GUID paths> -e` / `<GUID paths>`, mirrored in `Dfrg\TaskSettings` fAllVolumes, fExclude,
  Volumes; found by diffing what Optimize Drives writes), `platform/win/Com.h`, `IDriveProvider::setScheduledOptimization()`,
  "Auto-optimize on/off" badge from the task, Drive > Windows schedule... with `ui/dialogs/ScheduleDialog.*`,
  `icd-cli schedule <volume> [--remove | --restore]`, `tests/integration/schedule/check-schedule.ps1`. Opt-out and
  restore checked on a test VHDX: Optimize Drives showed it unchecked, the task XML was identical after restore.
- T121 VSS user interface - shadow copies detected for real (`listShadowCopies()` once per drive list; T090 never
  called it), every level aligns on such drives (Fast too), `dropUnaligned()` / `PlanInput::blockUnaligned` (unaligned
  moves dropped with their dependents and parked pairs, needed ones skipped), Plan tab VSS row and "Block unaligned
  moves", `ui/dialogs/VssWarning.*` before Execute, `icd-cli plan|execute --block-unaligned`, unaligned counts in the
  plan JSON, `tests/integration/vss/check-vss.ps1`. On the `vss` scenario blocked Moderate grew shadow storage by 2.1 MB
  for 51 moves (file system records only) against 34-37 MB unblocked; "unchanged" is impossible once any file moves.
- T130 Settings store and dialog - settings file version 2 (version 1 still read): `fastBudgetMultiplier` (x1/x2/x4/x10,
  into the GUI's plan requests) and manual caps `resources` (0 = Auto); `ui/dialogs/SettingsDialog.*` (Settings >
  Preferences / Resources, Planning and Resources pages, live "In effect" caps), enabled while no job runs. Caps apply
  without a restart: the pool starts a thread per processor and `ThreadPool::setConcurrency()` activates the capped
  count; `AppServices::refreshCaps()` / `applySettings()` before each job and after a change; Progress tab line follows.
  `SettingsTest` (round trip, v1, bad values, caps now and after restart), `ThreadPoolTest` (active count, drained queues).
- T131 Custom strategies - `services/settings/StrategyLibrary.*` (one zone script file per custom strategy in
  `strategies\` beside the settings file, `custom-<name>` ids that fit `DriveStrategy::preset`, save / replace by name,
  delete, import that never replaces, export, unusable files skipped), owned by `AppServices`; Strategy tab picker over
  built-in and custom strategies with Save as / Delete / Import / Export (`ui/StrategyActions.*`); `icd-cli plan|execute
  --strategy` find custom ids. `StrategyLibraryTest`. **Phase 10 exit:** a custom strategy survives a restart and gives
  the same plan at every level (unit test; two `icd-cli plan` processes gave identical plans; GUI Save as / Delete
  checked through UI Automation).
- T123 Last TRIM date and trimmed bytes - `platform/win/OptimizeStatistics.*` reads Windows' optimizer statistics
  (`Dfrg\Statistics\Volume{GUID}`: LastRunTime as local SYSTEMTIME, LastRunClustersTrimmed); `DriveInfo::lastOptimized`
  shown on SSD rows ("trimmed 11 days ago", same as Optimize Drives' "Last run"); after a TRIM the status line says how
  much Windows trimmed (`TrimResult`, `trimResult()`) and the row updates. Last analysed column widened.
  `OptimizeStatisticsTest`, `TrimJobTest`, `DriveInfoTest`.
- T124 Map colors tab - `ui/panels/MapColorsPage.*`: the bottom panel's Map colors tab with the color mode switch and
  the legend (now scrolled); the strip under the map, the color choice and the state caption above it are gone (the
  caption repeated the status line), so the map is taller; `MapPanel::attachColors()` (weak reference),
  `syncColors()`; View > Map colors and legend (Ctrl+L) shows the tab.
- T125 Map color presets and custom colors - `core/map/ColorScheme.*` (Standard = original colors, Colorblind-safe,
  Dark; custom colors per legend key), keys on `LegendEntry`, `paletteFor(mode, scheme)`, `legendColors()`,
  `OccupancyPalette` with scheme colors, canvas background from the scheme; settings `mapColors` (preset, custom colors
  per preset as "#RRGGBB"); Map colors tab: preset picker, Reset colors, click a legend entry to pick its color.
  `ColorModesTest` (tiles = legend colors for every preset and a custom scheme, standard = original, presets cover
  every key distinctly), `SettingsTest` (map colors).
- T126 Light and dark windows - Settings > Preferences > Appearance (System / Light / Dark, `Settings::appearance`),
  applied at start through `wxApp::MSWEnableDarkMode()`; dark variants for the drive space bars and the Strategy tab's
  problem text. Checked in forced dark mode on the fake drive list (window, tabs, grid, rulers, Settings dialog).
- T127 File tab - `ui/panels/FilePage.*`: the selected file's path and facts (`core::describeFile()`), "Find file",
  Show in Explorer, Copy path, previous / next fragment; the row above the map is gone (the map panel is only canvas and
  rulers); `MapPanel::attachFile()`, `selectedFile()`, `setSelectionHandler()`; a newly selected file brings the tab
  forward when no job runs. `HitTestTest.DescribeFile`.
- T128 Defragment this file - `services/execute/FileDefrag.*` (`planFileDefrag()`: nearest whole free run, else 64 MB+
  pieces when that leaves fewer fragments, MFT zone kept free, validated; `makeFileDefragJob()`), `runGuiExecution()`
  shared with planned levels, `WorkflowAction::DefragmentFile` (after analysis, review or a run; not where moves are
  blocked), `ID_DEFRAG_FILE` in Optimize, the File tab and the map's file menu, confirmation, "File defragmented"
  status. Checked on T:: 13 extents -> 1, hash unchanged. `FileDefragTest`, `WorkflowTest.DefragmentFile`.
- T129 Defragment file shortcut - Ctrl+D for `ID_DEFRAG_FILE` in the shortcut table (works with focus on the map,
  follows the workflow's enable rule, listed under Optimize in F1), `withShortcut()` puts the key on popup menu labels
  (map file menu, drive menu), File tab button tooltip from `commandTip()`. Checked on T:: 7 extents -> 1 via Ctrl+D.
- T132 Video formats - `data/classes.json`: video at any size (the 100 MB limit left clips unknown) with more formats
  (m4v, flv, mpg, 3gp, ogv, mxf, ...), DVD / HD DVD / Blu-ray streams (vob, vro, evo, m2ts, ssif), and from 10 MB the
  extensions shared with code (ts, mts, mod) and GIF animations; small .mts / .cts are TypeScript sources.
  `CatalogTest.Video`, check-classes.ps1 (passes on 4 GB). The video preset at Maximum now parks 1.16 x on one
  OptimalityTest volume (a fuller sorted zone), so it joins photos outside `tightPreset()` until T095d.
- T076a Project markers, churn file types, Logs and Dumps classes - `RuleEngine`: `markers` (in or below a directory
  holding one) and `folders` + `markers` (in or below such a folder whose parent holds one), one bit per marker glob
  and per rule, marker bits collected per directory from the listing (one lookup each), `classifyPath()` takes the
  volume's other entries. `Catalog`: `projectMarkers` + `"inProject"`. `data/classes.json`: development types (source,
  build extensions, debug info, source maps) only inside projects, build folders of Cargo, Maven, sbt, Mix, rebar,
  dune, Erlang, npm, Python, Gradle, CMake, Meson, MSBuild, Stack/Cabal, Zig, SwiftPM, Bazel, Dart, Xcode beside their
  markers, unambiguous caches without, venvs by `pyvenv.cfg`, editor leftovers, partial downloads, browser caches;
  new Logs class (kept in place with the churn in every preset) and Dumps (anywhere, cold with archives) with colors in
  every color preset. The synthetic volume gets a root `.git`. SnapshotStoreTest: XPRESS ignores some flipped bits, so
  random damage must be refused or load identical. CatalogTest, RuleEngineTest.
- T076c Class summary of a real development folder - `icd-cli classify <folder> [--depth N] [--top N] [--list CLASS]`
  (`src/cli/commands/Classify.*`) over `platform::walkFolder()` (`src/platform/win/FolderWalk.*`, ordinary directory
  API, no admin, junctions not entered): an extent-less snapshot classified by the same RuleEngine, files and bytes per
  class with the folders holding most. Reviewed with the user on F:\ESL (35 K files) and F:\Projects (426 K files);
  fixes: build folders before the Git rules (CMake `_deps` packs are build), other `.git` data repository, `.bin` /
  `.img` disc images only from 1 MB, no Crashpad folder rule, more project files as source (README, LICENSE, .iml,
  .inc, .in, .asn1, .app.src, .appup, .mib, .xsd, .s, .svg, .gql, .j2, .pem...), binaries inside projects (exe, dll,
  lib, a, so, dylib, pyd, wasm) build output (the user's call). The synthetic volume's binaries are `.sys` (installed
  programs, outside the root project). FolderWalkTest, CatalogTest.
- T141 App icon - Flaticon "HDD" by Smashicons (`CREDITS.md`): masters in `res/icon` (`hdd-color.png` used, the
  outline and blue styles kept), `res/IroncladDefrag.ico` (16, 20, 24, 32, 48, 64, 256 px; 256 as PNG) built by
  `tools/make-icon.ps1` (ImageMagick), embedded as `APPICON` ahead of wx.rc's `wxICON_AAA`; `ui::appIcons()`
  (`src/ui/AppIcon.*`) sets the bundle on the main frame and all seven dialogs. Checked: the exe's first icon group,
  shell extraction at 16-256 px, the frame's small and big icons and its title bar at 100%.
- T142 Version 1.0 - `project(VERSION 1.0.0)`; `cmake/BuildCommit.cmake` (target `icd_build_commit`, every build,
  header rewritten only on change) gives `ICD_BUILD_COMMIT` (short hash, "-dirty"); `services::buildCommit()` /
  `versionWithCommit()` in the About box, `icd-cli --version` and the log start line (`commit=`); title bar and file
  properties stay the bare 1.0.0. Checked all five places.
- T140 Installer - `installer/IroncladDefrag.nsi` (NSIS 3.12, MUI2: per machine into Program Files, GUI, icd-cli,
  wx DLLs, app-local VC++ runtime, Start menu and optional desktop shortcut, Installed apps entry, uninstaller keeping
  user data unless asked) and `make-installer.bat` (Release build, version and commit from the generated headers,
  CRT from `VCToolsRedistDir`) writing `dist/IroncladDefrag-1.0.0-<commit>-setup.exe` (4.7 MB). Checked without
  elevation: silent install into a folder, icd-cli and the GUI start from it at 1.0.0, silent uninstall removes it and
  keeps user data, nothing left in HKLM or the Start menu. The elevated install (Program Files, Installed apps) is the
  user's Phase 11 exit check.
- T112 Resume an unfinished execution from the GUI - `services::findUnfinishedRun()` / `UnfinishedRun` (the volume's
  newest log only, when crashed or cancelled with moves left; moves and bytes left, strategy, level, how and when it
  stopped; `unfinishedExecutionLog()` and so `icd-cli execute --resume` now ignore older logs: a newer run replaces
  them), `makeResumeJob()` and `runGuiRequest()` (`ExecuteJob`), `WorkflowAction::ResumeRun` (`setResumable()`,
  analyzes first without a map, then executes; `resuming()`), `ui/ResumeCheck.*` (reads the log on the pool, the
  latest look reports through `CallAfter`), Resume run button shown only while it can run
  (`WorkflowCommand::onlyWhenEnabled`, `BottomPanel::showCommand()`), Optimize > Resume interrupted run (Shift+F9),
  `ConfirmExecuteDialog` for a resume (the run's figures, same options), `ExecutionView::startResume()` / `RunKind`,
  status line on selection, look again after every run, a warning when a new plan replaces an offered run.
  `WorkflowTest` (ResumeRun, ResumeRunStopsWithItsAnalysis), `ExecutionLogTest.OnlyTheNewestLogIsResumed`. Seen
  without elevation: X:'s cancelled run (69,967 of 80,180 moves left) offered on selection. The elevated GUI run is
  T112b.
- T112b GUI check of Resume run (elevated, 2026-11-06, fresh 1 GB `mixed` VHDX on the E: HDD each, mixed at Maximum,
  the GUI driven by posted command ids on the IDE's display): (1) Execute cancelled after 613 of 2,839 moves; Resume
  run appeared at once, resumed with the map shown, 2,226 moves in 33 s, the live map followed all of them (48
  updates, slowest 19 ms); (2) the GUI killed with move 617 of 3,284 begun; a new start offered "an interrupted run
  has 2,667 of 3,284 moves left", Resume run analyzed first and ran the rest (move 617 found already done). After
  each: the log ends with a non-cancelled end line and every move moved or already done and verified, every moved
  file's extents (`fsutil file queryextents`, 2,726 and 3,167 files) equal its planned destinations, file hashes
  unchanged, `chkdsk /scan` clean, `icd-cli check-catchup` equal. Nothing to fix.
- T114 Read and write animation - pulses over the map where the disk is read and written (the user's design, then
  tuned with them): each bar of the runs (`core::activityBars()`: first-row piece, full rows as one block, last-row
  piece) filled over the map and scaled about its center for 0.4 s, reads 1x to 1.2x wide and 10x tall at 0.5 to
  0.05 opacity, writes backwards. Colors `activity.read` / `activity.write` in every preset, listed as Reading and
  Writing after every mode's legend entries and editable there. Reads at a move's begin (`ExecutionFeed::takeBegun()`,
  mover-stamped), writes once the live map shows the move (spread to the next update), the MFT share an analysis read
  between progress reports, simulated moves on the fake drive list. `core/map/Activity.*` (`ActivityLayer`, 256
  pulses), `ui/map/MapActivity.*` (8 ms timer, pause, "activity frames" log), `D2DRenderer::pushScaledClip()`, View >
  Read/write activity (`Settings::mapActivity`). Windows' "Show animations" is not read (off on the user's machine;
  their choice). Seen on the fake drive list and 1 GB `mixed` VHDXs: 65-67 fps with pulses, paint 2-4 ms, zoom 57-86
  fps either way; throughput varied 8-34 MB/s run to run with pulses on and off, so further measurement was dropped
  (the user's call). `ActivityTest`, `ExecutionFeed` begun moves, settings round trip.
- T133 Order moves by disk head position - `core/execute/HeadOrder.*` (`orderForHead()`: the ready move whose first
  read is nearest the last write goes next; moves sharing clusters or a file keep plan order), `core/util/BitTree.*`
  (ready set), `services::headOrdered()` (logs head travel before and after), Execute confirmation checkbox (on by
  default, not on Resume), `ExecuteJobRequest::headOrder`, `icd-cli execute --plan-order`. 1 M moves order in 0.7 s
  on one thread. Also: "workflow job finished" log line with seconds for every GUI job, seconds on the synthetic
  analysis line. `HeadOrderTest`, `BitTreeTest`.
- T114b Pulse size independent of zoom - a pulse is 1 to 16 window pixels tall (read grows, write shrinks) and 100-120%
  of its bar's width; the height was a multiple of the row height and grew huge zoomed in. `MapActivity` sizes each
  bar's rectangle; `D2DRenderer::pushScaledClip()` removed. `ActivityTest` heights.
- T144 Release skill and versions page - `.claude/skills/release/SKILL.md` (`/release [x.y.z|patch|minor|major]`:
  version in `CMakeLists.txt`, user-approved changelog, `Release x.y.z` commit, `make-installer.bat`, record, upload
  via `/deploy` with downloads before site), `deploy/record-release.ps1` (installer fields and `.sha256` into
  `website/data/releases.json`, refuses dirty builds), site `/releases/` (every version newest first, size, SHA-256,
  changelog in `<details>`), Download buttons on the newest installer. 1.0.0 recorded as build `218867e`.
