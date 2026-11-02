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
