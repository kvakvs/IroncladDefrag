# Repository Map

Modules, source files, build files, dependencies and generated output. Keep it current with structure
changes (see `AGENTS.md`).

## Top level

| Path | What it is |
| --- | --- |
| `plan.md` | Development plan (phases, design decisions). |
| `data/strategies/*.json` | Built-in strategy presets as zone scripts (plan.md 8.3-8.8), embedded into `icd_core` (`ICD_PRESETS` in CMakeLists.txt). |
| `data/classes.json` | Built-in file classes and classification rules (plan.md 8.2-8.6), embedded into `icd_core`. |
| `cmake/EmbedText.cmake` | Script mode: writes a C++ source returning a text file as a char array (`builtinCatalogJson()`). |
| `.agents/01-plan.md`, `.agents/00-finished.md` | Open tasks and the log of finished ones. |
| `.codex/arch.md`, `.codex/map.md` | Architecture notes and this map. |
| `story/` | Notes from earlier attempts (read-only history). |
| `tools/` | Developer scripts. |
| `CMakeLists.txt` | All targets and their explicit source lists; pins `ICD_WX_VERSION`. |
| `cmake/IcdCompilerOptions.cmake` | `icd_target_defaults()`: C++23, `/W4 /WX /utf-8`, MSVC-only conformance flags, Unicode and Windows defines. |
| `cmake/IcdWxWidgets.cmake` | Imported targets `wx::base`, `wx::core`, `wx::wx` from `3rdparty/wxWidgets`; checks the fetch stamp. |
| `build.bat` | `build.bat [Debug|Release|All]`: fetches wx, configures `build/` (Ninja Multi-Config, clang-cl, lld-link) once, builds. `ICD_TOOLCHAIN=msvc`: VS 2026 in `build-msvc/`. |
| `test.bat` | `test.bat [Debug|Release|All] [fast|slow|all]` (default All fast): `build.bat`, then `ctest -L fast` (no real volumes, seconds), `-L slow` (real volumes and disks, on demand) or both. |
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
| `icd-cli` | console exe | `src/cli/main.cpp`, `src/cli/commands/*` | `icd_services` |
| `map-render-spike` | console exe with a wx window | `tools/spike/map-render-spike/main.cpp` | `icd_services`, `wx::wx`, `d2d1` |
| `gen-scenario` | console exe | `tools/testvol/gen-scenario/*` | `icd_platform`, `advapi32` |
| `synth-bench` | console exe | `tools/bench/synth-bench.cpp` | `icd_services`, `psapi` |
| `scan-bench`, `catchup-bench`, `plan-bench` | console exes | `tools/bench/plan-bench.cpp` | `plan-bench` target (T093): synthetic volume (default 30 TB / 20 M files), classified, three levels planned on N workers, per-level figures, a plan fingerprint and the slow stages (`PlanResult::stages`); `--compare 1` checks one worker gives the same plans. |
| `tools/bench/scan-bench.cpp`, `tools/bench/catchup-bench.cpp`, `tools/bench/plan-bench.cpp` | `icd_services` |
| `map-bench` | console exe with a wx window | `tools/bench/map-bench.cpp` plus the `src/ui/map` canvas sources, `ui/Accelerators`, `ui/Commands` | `icd_services`, `wx::wx`, `d2d1` |
| `icd_tests` | console exe (GoogleTest, CTest) | `tests/**` (own `tests/CMakeLists.txt`) | `icd_core`, `icd_platform`, `icd_services`, `GTest::gtest_main` |
| `check-complexity[-all]`, `check-clang-tidy[-all]`, `check-quality[-all]` | custom | quality gate; clang-tidy only in Ninja builds with a compile database | - |

## Source files

| File | Purpose |
| --- | --- |
| `src/core/Version.h.in` | Version macros template; CMake writes `build*/generated/core/Version.h`. |
| `src/core/format/ByteSize.*` | `formatBytes()`: Explorer-style byte sizes; `parseSizeSpec()` / `sizeSpecText()` for data files ("64 MB"). |
| `src/core/format/Count.*` | `formatCount()`: locale-neutral thousands separators. |
| `src/core/classify/AccessTimeHeuristic.*` | `gatherAccessEvidence()` (files read more than an hour after creation and last change, recent ones), `assessAccessTimes()` (setting plus evidence: reliable, unreliable, unknown with a reason), `verdictName()`. |
| `src/core/classify/Catalog.*` | `Catalog`, `ClassInfo` (id, name, color, description), `parseCatalog()` (strict: unknown keys, classes, flags, bad sizes and colors refused), `builtinCatalog()` (data/classes.json embedded at build time), `builtinCatalogJson()` (generated), `fileFlagNamed()` / `fileFlagNames()`. |
| `src/core/format/JsonReader.*` | `JsonValue` (tree, members in file order, kind checks), `parseJson()` (RFC 8259, errors with line and column), `JsonError`. |
| `src/core/planner/Budget.*` | `TimeModel`, `BudgetOptions`, `moveBenefit()`, `planSeconds()`, `applyBudget()` (benefit-per-byte selection with dependencies, most valuable first, deferred clusters). |
| `src/core/planner/CycleGuard.*` | `CycleGuard`: wait graph of the destinations placed so far (a file waits for the owners of the clusters it lands on) in a topological order repaired as waits are added (Pearce-Kelly), bounded searches; `closesCycle()`, `cycleEnd()`, `record()`. Keeps placement from creating cycles that cost parking (T095b). |
| `src/core/planner/CandidateFilter.*` | `Candidates`, `filterCandidates()`: must-move files per zone and the defragment-only ones (plan.md 9.3.3). |
| `src/core/planner/DefragOnly.*` | `defragmentDestinations()` (for the level pipeline), `planDefragment()`: defragment-only moves to the nearest run that holds the file, or large pieces; skips with a reason; vacated space reused. |
| `src/core/planner/MakeRoom.*` | `findRoom()`: window for a large file whose occupants are small evictable files (plan.md 9.3.6), fewest clusters evicted. |
| `src/core/planner/MoveOrdering.*` | `orderMoves()`: dependency graph from destination/source overlaps, Kahn in destination order, Tarjan for cycles; at Maximum components are broken greedily (park the smallest move of every cycle left, `LocalCycles` finds the rest) or, when huge, a batch per round; elsewhere redirect or drop the cycle and its dependents; `OrderOptions`, `OrderReport`. |
| `src/core/planner/PackToBack.*` | `packToBack()`: back zones filled from the end, or sorted when they have an order at the level. |
| `src/core/planner/Placement.*` | `SpaceRequest`, `findFirst()` / `findLast()` / `findNear()` / `findPieces()` avoiding the file's own runs, `takeRuns()`, `releaseRuns()`, `sortedRuns()`; `Destination` (file, runs, reason, zone) and `appendMoves()`. |
| `src/core/planner/FastFill.*` | `fastFill()`: holes filled from the packed end (front or back) with the largest file that fits without closing a dependency cycle, single runs or large pieces for the rest, overflow; `FillOptions` (with the level's shared `CycleGuard`), `PackFrom`, `reasonFor()`. |
| `src/core/planner/HoleSet.*` | `HoleSet`: mutable merged free runs in blocks with a max-length tree; `take()`, `add()`, `contains()`, `nextFit()` / `prevFit()` within a window, `within()`, `HoleSet::Walk` (a window's holes one at a time). |
| `src/core/planner/LevelPlanner.*` | `PlanInput`, `LevelPolicy`, `PlanMetrics`, `PlanResult`, `assignInput()`, `planZones()` (Moderate/Maximum pipeline: room for sorted zones, zones in script order on pool slices with one cycle guard, eviction and overflow placed without closing cycles where possible), `measurePlan()`, `usedClusters()`. |
| `src/core/planner/levels/FastLevel.*` | `HotSpotOptions`, `StripeScore`, `scoreStripes()`, `findHotSpots()`, `fastPolicy()`, `planFast()` (hot spots cut into pieces of at most 8 stripes, hottest planned first until their movers reach twice the budget; compaction, big long-lived files to their zone, budget). |
| `src/core/planner/levels/ModerateLevel.*`, `levels/MaximumLevel.*` | `moderatePolicy()` / `planModerate()`, `maximumPolicy()` / `planMaximum()`: the plan.md 9.2 table as policies over `planZones()`. |
| `src/core/planner/LowerBound.*` | `GoodEnough`, `fragmentsOk()`, `fragmentOkFor()`, `usableSpan()`, `lowerBound()` (must-move files per zone, parallel), `fitTarget()` (slack before each forward zone by a dynamic programme over stay intervals, plan.md 9.3.2). |
| `src/core/planner/Plan.*` | `Plan` (moves with flat source/destination runs, lower bound, extra, skipped and deferred clusters, notes, `PlanTotals`), `Move`, `MoveReason`, `SkippedFile`, `movableRuns()`, `clustersOf()`, `fragmentsOf()`, `finishTotals()`. |
| `src/core/planner/Validator.*` | `validatePlan()`: replays a plan on copies of free space and extents and checks plan.md 9.5 (free destinations, current sources, kept clusters, zones, one move per file, parking, accounting, totals); valid plans in parallel (moves, then free space per LCN stripe), anything off replayed serially. |
| `src/core/planner/VssAlign.*` | `vssUnit()`, `VssGeometry`, `movesAligned()`, `VssReport` / `vssReport()`, `alignDestinations()` (shift onto 16 KB-aligned positions), `dropUnaligned()` / `blockUnalignedMoves()` (block unaligned moves with what depends on them). |
| `src/core/planner/ZoneAssignment.*` | `assignZones()`: per file the first matching zone, its `FileRole` (Fixed, InPlace, Defragment, Placed) and movable clusters; per-zone need; `fixedFile()`. |
| `src/core/planner/SortZone.*` | `SortKeys` (per-order keys, directory+name ranks), `SortKeyCache` (each order's keys built once per planning run, shared by the levels), `weightedLis()`, `SortedZone`, `sortsAt()`, `applySortBounds()` (anchors: the heaviest chain of in-place files whose offsets from their packed positions stay within the zone's room and never fall; the rest join the bound; tolerance), `sortZone()` (movers between anchors in key order, skipping past cycle-closing files while the group has spare room). |
| `src/core/planner/TargetLayout.*` | `AvailableAxis` (usable clusters numbered around obstacles), `layoutObstacles()`, `gapClusters()`, `resolveLayout()`: placed zones as LCN runs (forward from the front, back zones at the end), gaps, slack, notes; `LayoutInputs` (reserved runs, slack before each forward zone, room per zone taken from the slack). |
| `src/core/strategy/Presets.*` | `builtinPresets()` (14 zone scripts parsed once, default first), `builtinPresetIndex()`, `builtinPresetJson()` (generated sources). |
| `src/core/strategy/ZoneScript.*` | Zone scripts (plan.md 8.1): `ZoneScript`, `Zone` (selector, anchor, order and per-level overrides, action, `fragmentOk`, `GapSpec`), `Effort`, `validateScript()` (issues with their zone), `parseZoneScript()` (strict, `ZoneScriptError` with every issue), `zoneScriptJson()` (round trip, defaults left out), enum keys. |
| `src/core/classify/RuleEngine.*` | `RuleEngine` (ordered rules over path globs, extensions, size, age, flags and churn; per-directory overrides; one automaton for all globs, stepped per directory level and per file name; parallel `classify()`, `classifyPath()`), `RuleSet`, `Rule`, `ClassId`, `extensionOf()`. |
| `src/core/format/Json.*` | `JsonWriter` (streaming, indented, RFC 8259 escaping), `jsonQuote()`. |
| `src/core/format/Duration.*` | `formatDuration()` (stopwatch), `formatAge()` ("3 h ago"). |
| `src/core/format/DriveText.*` | All-disks column texts: `driveName()`, `describeSpace()`, `describeStorage()` (media with bus), `describeFragmentation()`, `describeLastAnalysed()`, `badgeLabels()`, `blockName()` / `blockReason()` / `movesBlockedText()` (setups that block moves). |
| `src/core/model/DriveInfo.*` | `DriveInfo`, `DriveBadges` (undetected facts optional), `MediaType` (HDD, SSD, Virtual, Unknown), `StorageFacts`, `VolumeLayout`; `classifyMedia()`, `canOptimize()` (NTFS, not SSD), `isSsd()`, `MoveBlock` and `moveBlocks()` / `canMove()` (RAID, Storage Spaces, dynamic disks, removable, BitLocker). |
| `src/core/model/VolumeLayout.h` | `VolumeLayout` (volume GUID name, sector/cluster size, cluster counts, disk number, partition offset, disk size, extent count) and `NtfsLayout` (serial, version, record size, MFT/mirror/zone, reserved). |
| `src/core/exec/Executor.*` | `ChunkedRange`, `Executor` interface (`submit`, `parallelFor`), `InlineExecutor`, deterministic `parallelReduce` and `parallelSort` (chunks sorted in parallel, then merged pairwise). |
| `src/services/exec/ThreadPool.*` | Work-stealing `ThreadPool` (per-worker deques, caller joins `parallelFor`, cancel-aware); `busyTime()` (worker time in tasks, running ones up to now) for the Progress tab's utilisation. |
| `src/core/job/CancelToken.*` | `CancelSource`/`CancelToken` over `std::stop_source`, parent-linked sources, `OperationCancelled`. |
| `src/core/job/PauseGate.*` | `PauseGate`: workers wait between items while paused; cancellation releases them. |
| `src/core/job/StageClock.*` | Job elapsed time and a linear per-stage time-left estimate for the Progress tab. |
| `src/core/job/SmoothedEstimate.*` | `SmoothedEstimate`: the shown time left changes at most every 3 s, to the average of the estimates since. |
| `src/core/job/RateMeter.*` | `RateMeter`: rate of a growing total over the last seconds (disk throughput, worker busy time). |
| `src/core/job/Progress.*` | `Progress` snapshot, `ProgressSink`, `ThrottledProgress` (~10 Hz, injectable clock). |
| `src/core/log/Log.h` | `logging::Level`, `Field`, `Record`, `Sink`, thread-safe `Logger`, `globalLogger()`, `info()`/`error()`/... helpers. |
| `src/core/log/LogFormat.cpp` | `formatRecord()`: one-line `key=value` records, UTC time, escaping. |
| `src/core/log/Logger.cpp` | `Logger` and the global helpers. |
| `src/core/ntfs/NtfsFormat.h` | On-disk NTFS constants (attribute types, record/attribute/DOS flags, fixed records), file reference helpers, bounds-checked `fits()` and little-endian `load()`. |
| `src/core/ntfs/Fixups.*` | `applyFixups()`: update sequence (multi-sector) protection undone in place; torn blocks rejected. |
| `src/core/ntfs/RunList.*` | `DataRun`, `decodeRunList()`: mapping pairs to runs (holes, signed deltas), coverage and overflow checks. |
| `src/core/ntfs/MftLayout.*` | `planMftReads()` (chunks in volume order, bitmap skips, split records), `isRecordUsed()`, `joinSegments()`, `mapStreamBytes()`, `MftGeometry`, `MftChunk`. |
| `src/core/ntfs/MftRecord.*` | `parseRecord()` into a reusable `ParsedRecord` (header, `$STANDARD_INFORMATION`, `$FILE_NAME`s, attribute segments with runs or kept resident values), `RecordStatus`, `statusName()`. |
| `src/core/ntfs/AttributeList.*` | `AttributeListEntry`, `parseAttributeList()`. |
| `src/core/ntfs/NtfsText.*` | `utf16ToUtf8()` with U+FFFD for lone surrogates. |
| `src/core/snapshot/SnapshotIO.*` | Snapshot files: `writeSnapshotFile()`, `readSnapshotFile()`, `readSnapshotMeta()`, `SnapshotMeta`, `BlockCodec`, byte sinks and sources, `SnapshotFormatError`; checksummed parallel blocks, full validation on load. |
| `src/core/util/Crc32.h` | `Crc32`: IEEE CRC-32 for snapshot blocks. |
| `src/core/util/LargeArray.h` | `LargeArrayAllocator` (shared memory resource, moves with its container), `LargeVector`, `LargeString` for the big snapshot arrays. |
| `src/core/resources/MemoryPlanner.*` | `estimateSnapshot()`, `MemoryRung` ladder, `planMemory()`, `giveWay()`, `MemoryPlan`, `describeMemoryPlan()`. |
| `src/core/snapshot/Types.h` | `Lcn`, `ClusterCount`, `FileIndex` (32-bit, `noFile`), `ClusterRun`, `VolumeGeometry`, `SnapshotError`. |
| `src/core/snapshot/NamePool.*` | Interned UTF-8 names in one buffer (`intern()`, `name()`, `freezeLookup()`). |
| `src/core/snapshot/FileTable.h` | Per-file columns (name, parent, size, clusters, flags, times, file reference); `FileFlag`. |
| `src/core/snapshot/FileSearch.*` | `searchFiles()`: "go to file" over names (exact, prefix, contains) and path fragments (`photos\a`), ASCII case-insensitive, one pass over distinct names then the file column, parallel and deterministic, with a limit and a total. |
| `src/core/snapshot/BitmapToRuns.*` | `BitmapChunk`, `freeRunsOf()` (word-at-a-time bit scan), `stitchRuns()` (joins runs at piece edges), `freeRunsParallel()` (grains on the pool, stitched in order). |
| `src/core/snapshot/DirectoryTree.*` | Child lists from parent indexes, roots, `path()` with a cycle guard. |
| `src/core/snapshot/ExtentIndex.*` | 16-byte `ExtentRecord` (40-bit LCN, 24-bit length), index by LCN (`find`, `overlapping`) and by file in VCN order (`extentsOf`, `fragmentCount`). |
| `src/core/snapshot/FreeRunList.*` | Sorted, merged free runs (`find`, `overlapping`, `largest`). |
| `src/core/snapshot/Snapshot.*` | The aggregate the map, classifier and planner read (with the `UsnCursor` of the scan); `snapshotMemoryBytes()`. |
| `src/core/snapshot/ScanReconcile.*` | `reconcileScan()`: newer read wins overlaps, extents trim stale free runs, unclaimed used clusters go to a placeholder file; result tiles the volume. |
| `src/core/snapshot/SnapshotBuilder.*` | Validating, order-independent builder (sorts, splits long runs, groups by file). |
| `src/core/snapshot/SnapshotStats.*` | `VolumeStats`, `summarize()`: fragmentation (files and bytes), unmovable clusters, free runs, largest run, free share in 64 MB+ runs; parallel and deterministic. |
| `src/core/snapshot/SnapshotPatch.*` | `SnapshotPatch`, `patchSnapshot()`: a new snapshot as changes to a saved one (continued files by base index with new parents and flags, new files and extents, placeholder runs kept), nullopt when the result could differ from a fresh build; used by the USN catch-up. |
| `src/core/snapshot/Stripes.*` | `StripeLayout` (fixed LCN stripes, 16 MB aligned, 512 on large volumes), `Stripes` views over extents and free runs, `clip()`, `mapStripes()` / `reduceStripes()` with stripe-order merge. |
| `src/core/synth/SynthRandom.h` | Self-contained seeded generator (xoshiro256**, own distributions) so seeds reproduce everywhere. |
| `src/core/synth/SyntheticVolume.*` | `generateSyntheticVolume()`: seeded snapshots up to 30 TB / 20 M files (class mix, fill, fragmentation, free pattern, $MFT, directory indexes). |
| `src/core/map/BinSummary.*` | `MapBin`, `BinAccumulator` (inline `add()`), per-record `extentMapFlags()`, `packExtentValues()`, `recordBlocks()` (LCN block -> first record), `ownerOf()`: the summary rules the pyramid and the rasterizer share. |
| `src/core/map/Palette.*` | `Rgba` (BGRA in memory), `blendColors()`, `usedShare()`, `Palette` interface over `PixelSummary`, `OccupancyPalette` (the fragmentation mode). |
| `src/core/map/ColorModes.*` | `ColorMode` (fragmentation, age, size, file class, zone fit, plan preview), age/size buckets, `newestModification()`, per-file `fileColorValues()` and per-record `extentColorValues()`, legends and `paletteFor()`, `visibleCells()`, exact parallel `countLegend()` |
| `src/core/map/PlanColorMode.*` | Map overlays (T098): `RunLayer`, `MapOverlay`, `mergedRuns()`, `paintOverlay()` into a tile; plan preview `planOverlay()` (vacated and filled clusters) and its colors. |
| `src/core/map/ZoneFitColorMode.*` | Zone-fit overlay (T098): `zoneFitOverlay()` from snapshot, zone assignment and target layout (targets, data outside its zone). |
| `src/core/map/TileRasterizer.*` | `renderTile()`: 256 x 256 tiles from the pyramid (coarse) or the extent index (fine), no grid lines (see `Grid`), `clustersInRect()`. |
| `src/core/map/Grid.*` | Deep-zoom grid drawn per frame at screen resolution: `gridLines()` (each visible cell's right and bottom edge on the last pixel nearest-neighbour drawing gives it, clipped to the last cluster and the window), `gridOpacity()` (fades in from 5 to 10 px per cell), `edgePixel()`. |
| `src/core/map/HitTest.*` | Hover lookups: `hitTest()` (pointer -> cell -> LCN -> extent or free run, fragment i/n via `fragmentOf()`, the pixel's cells and used clusters), `formatFileTime()`, `describeHit()` tooltip lines. |
| `src/core/map/Selection.*` | Selection math: `fileFragments()` (VCN order, continuing runs joined), `SelectionOverlay` / `selectionOf()` (LCN-sorted merged runs), `shapeOf()` (a run as at most three cell rectangles), `runsTouch()` (dirty-tile test), `highlightRuns()` (paints the selection into a tile), `focusView()`. |
| `src/core/map/MapData.*` | Shared immutable bundle of snapshot, world, pyramid, extent flags, per-record color values, per-file classes (`fileClasses()`, for planning) and the record block index (`source()` for the rasterizer, `ageReference()`); `TileIdHash`. |
| `src/core/map/Overview.*` | Minimap math: `placeOverview()` (world shape in the bottom-right corner, hidden in small windows), minimap pixel <-> world, view marker box (clipped, minimum size), `dragTo()`. |
| `src/core/map/Ruler.*` | Ruler ticks: `rowTicks()` (row starts: byte offset, % of volume, % of disk when the partition is known), `columnTicks()` (offset within a row), power-of-two steps, `formatOffset()` / `formatPercent()` label precision. |
| `src/core/map/Pyramid.*` | Map LOD pyramid: quadtree of 8-byte `MapBin`s over the world grid (level k bin = one tile pixel at zoom k), parallel build per base-bin row band, 2x2 merges, dirty-range `update()`. |
| `src/core/execute/ExecutionPlan.*` | `ExecutionPlan` (identity, moves by file reference with sources, destinations, region and flags, paths, reserved runs), `PlanIdentity`, `PlannedMove`, `executionPlanOf()` (a planned level for execution), `plannedClusters()`. |
| `src/core/execute/MoveCalls.*` | `moveCalls()` (pending pieces as calls of at most a chunk; compressed files in whole units, never into the next piece), `MoveCall`, `CallShape`, `retarget()`, `Landing` / `landingOf()` / `landed()` (verification of runs read back), `landedPrefix()` / `remainderOf()` (what a refused call moved anyway), `allocatedOf()`. |
| `src/core/execute/MovePieces.*` | `StreamRun`, `MovePiece`, `matchMove()` (a file's runs now against a move: pending and done pieces, nullopt when changed), `remapMove()`, `lcnRunsOf()`, `asStreamRuns()`, `allDone()`, `pendingClusters()`. |
| `src/core/execute/PlanReplay.*` | `replayPlan()` (moves kept or dropped on the free space and files now, dependents dropped with them), `ReplayResult`, `DropReason` / `dropKey()`, `CurrentFiles`, `SnapshotFiles` (a plan's files in a later snapshot), `allMoves()`. |
| `src/core/map/Viewport.*` | View math: `Viewport` (scale, center, size), limits (32 px per cluster to beyond fit), fit, anchored zoom, pan, eased `ZoomMotion` and `PanInertia` by elapsed time. |
| `src/core/map/World.*` | Fixed map world (plan.md 7.1): `roundToNice()` row width from the screen aspect, LCN <-> cell, zoom levels (-5 = 32 px per cluster up to one tile), `blendFor()` (levels to fade between where cells are under a pixel), tile cell rects, row segments of a run. |
| `src/core/util/LruCache.h` | Generic cost-limited LRU cache (map tiles); `keys()` for finding stale entries. |
| `src/core/resources/ResourceCaps.*` | `computeCaps()` (plan.md 6.6 worker and memory caps with reasons), `describeCaps()`. |
| `src/platform/win/SystemResources.*` | `detectSystemResources()` (groups, affinity, job CPU/memory limits, cores, RAM), `LowMemorySignal`. |
| `src/platform/win/ChildProcess.*` | `runCaptured()` (console program without a window, output chunks to a callback, cancel terminates, exit code), `systemProgram()` (System32 path). |
| `src/platform/win/Console.*` | `ConsoleCancel` (Ctrl+C cancels a source while alive), `useUtf8ConsoleOutput()`. |
| `src/platform/win/Handle.h` | `UniqueHandle`: move-only owner of a Win32 kernel handle. |
| `src/platform/win/Shell.*` | `showInExplorer()`: `SHOpenFolderAndSelectItems` (the running Explorer opens the folder, with the user's rights). |
| `src/platform/win/Privileges.*` | `enablePrivilege()` (e.g. `SE_BACKUP_NAME` for files only SYSTEM may open), `relaunchElevated()` (`ShellExecuteExW` "runas" of this program). |
| `src/platform/win/RetrievalPointers.*` | `readRetrievalPointers()` (all runs of a stream, holes as LCN -1), `readFileFacts()` (times, attributes, size, links, named streams), `openFileById()`, `openForQuery()`. |
| `src/platform/win/ShadowCopies.*` | `countShadowCopies()` (vssadmin listing, GUID path count, nullopt without rights), `listShadowCopies()` (the listing once for a drive list), `countShadowsInListing()`. |
| `src/platform/win/ScheduledOptimize.*` | Windows' scheduled optimization: `ScheduleSelection` (All / AllExcept / Only), `parseScheduleArguments()`, `scheduleArguments()`, `scheduleCovers()`, `withScheduled()`, `readSchedule()` / `writeSchedule()` (ScheduledDefrag task via Task Scheduler COM, `Dfrg\TaskSettings`). |
| `src/platform/win/Com.h` | `ComScope`, `Bstr`, `Variant`: RAII for one COM query (WMI, Task Scheduler). |
| `src/platform/win/VolumeTopology.*` | `isDynamicDisk()` (LDM partitions via `IOCTL_DISK_GET_DRIVE_LAYOUT_EX`, `hasLdmPartition()`), `encryptedVolumes()` (BitLocker states from WMI `Win32_EncryptableVolume`), `volumeKey()`. |
| `src/platform/win/StorageProperties.*` | `queryDiskStorage()` / `queryVolumeStorage()` (seek penalty, TRIM, bus, removable, model via `IOCTL_STORAGE_QUERY_PROPERTY`), `busTypeName()`. |
| `src/platform/win/TextConvert.*` | `toUtf8()` / `fromUtf8()` between the Windows API and the core model, `oemToUtf8()` for console tool output. |
| `src/platform/win/UsnJournal.*` | `queryUsnJournal()` (ID and USN cursor, none when inactive), `enumerateUsnFiles()` (`FSCTL_ENUM_USN_DATA`: reference, parent, attributes, name per MFT record), `mftRecordOf()`. |
| `src/platform/win/VolumeBitmap.*` | `readVolumeBitmap()` (chunked `FSCTL_GET_VOLUME_BITMAP`), `readFreeRuns()` (32 MB pieces converted in parallel into a `FreeRunList`, progress callback). |
| `src/platform/win/Volumes.*` | `VolumeRecord`, `listVolumeNames()`, `mountPathsOf()`, `openVolume()`, `queryVolume()` (basics, disk extents and disk size, NTFS volume data; failures noted, never thrown), `enumerateVolumes()`, `isElevated()`. |
| `src/platform/win/WinError.*` | `formatWinError()`, `lastWinError()`: Win32 error text. |
| `src/platform/win/LastAccessSetting.*` | `readLastAccessSetting()` (NtfsDisableLastAccessUpdate from the registry), `decodeLastAccessSetting()` (user or system managed, on or off, legacy values). |
| `src/platform/win/LogSinks.*` | `FileSink` (rotating `<name>.log`, flushed per line), `DebugOutputSink`, `defaultLogDirectory()`. |
| `src/services/activity/ChurnStats.*` | `collectChurn()` (whole retained journal: sessions per file and per directory, deletes counted from their first record in the directory the file lived in), `directoryChurns()`, `churnFlags()` (churning files, directories and all below), `churnDirectories()`. |
| `src/services/ShellActions.*` | `volumePath()` (volume root plus snapshot path), `showInExplorer()` for the map's context menu. |
| `src/services/Elevation.*` | `runningElevated()`, `restartElevated()`: the GUI's elevation flow without Win32 in `ui`. |
| `src/services/AppInfo.*` | Product name and version (from `core/Version.h`), `buildDescription()` (configuration, compiler). |
| `src/services/AppServices.*` | Service composition root: resources (and `detectResources()` for a fresh reading), caps, `ThreadPool`, `JobRunner`, drive provider (`DriveSource`: system, or fake with `ICD_FAKE_DRIVES=1`), `SettingsStore`. |
| `src/services/drives/IDriveProvider.h` | Drive list source interface used by GUI and CLI; `setScheduledOptimization()` (take a drive out of Windows' schedule or put it back). |
| `src/services/drives/SystemDriveProvider.*` | Real drive list from `enumerateVolumes()`; `toDriveInfo()` (admin-needed badge); the system-wide last-access badge on NTFS drives; `addTopology()` (removable, dynamic disk, BitLocker facts); `SystemFacts` (asked once per listing: rights, last access, BitLocker, Windows schedule, shadow copies) and `driveOf()`. |
| `src/services/drives/FakeDriveProvider.*` | Fake drives: SSD, three NTFS HDDs in different states, one exFAT drive, and a 30 TB drive at the design target; a simulated Windows schedule. |
| `src/services/job/JobRunner.*` | `JobRunner`, `JobContext`, `JobObserver`, `JobResult`: jobs on the pool with cancel, throttled progress, one result. |
| `src/services/job/FakeJob.*` | `makeFakeParallelJob()`: serial + parallel stages with stage names, item labels and a pause gate; stands in for every workflow step in Phase 1. |
| `src/services/plan/PlanJob.*` | `PlanRequest`, `makePlanRequest()`, `fileTimeNow()`, `planLevels()` (three levels in parallel, validated), `LevelOutcome`, `LevelOverlays`, `PlanOutcome` (with the zone assignment and per-level overlays), `levelOverlays()`, `planSummary()` (Plan tab), `PlanOutput`, `makePlanJob()` (classifies when the request has no classes, builds the overlays) |
| `src/services/plan/PlanReport.*` | `planJson()` / `planText()` (per level figures, hot spots, notes, validity, optional move list), `PlanReportOptions`. |
| `src/services/execute/Revalidate.*` | `planRefusal()` (other volume, reformatted, resized, too old), `revalidatePlan()` (volume read again by catch-up or scan, then the replay), `replayOnSnapshot()`, `Revalidation`, `RevalidateOptions`, `PlanAge`. |
| `src/services/execute/ExecuteJob.*` | The GUI's execution: `makeExecuteJob()` (execution plan of a planned level, `runExecution()` with pause), `ExecutionFeed` (thread-safe counters, Log tab lines with a cap, moved records and revalidation's volume for the live map, summary), `reasonText()`, `recordLine()`, `dropLine()`, `summaryLines()`, `summaryStatus()`. |
| `src/services/execute/LiveMap.*` | The live map during execution: `rebaseMap()` (the shown map laid out from revalidation's volume when it differs; classes by file reference), `applyMoves()` (snapshot copy patched with read-back runs, pyramid recomputed over the moved files' old and new clusters), `anyApplicable()`, `LiveMapUpdate`. |
| `src/services/execute/ExecuteReport.*` | `checkAgainstRescan()` (full rescan: moved files where predicted, saved snapshot compared), `RescanCheck`, `zoneSpans()`, `executeJson()` / `executeText()`, `ExecuteReportInput`. |
| `src/services/execute/Executor.*` | `executePlan()` (mover loop with prefetch helper, re-read and match before each move, chunked calls, pause, cancel, throughput limit, stale-bitmap replacement in the move's region, skip reasons, verification), `ExecuteOptions`, `MoveRecord`, `MoveOutcome` / `outcomeKey()`, `ExecutionReport`, `ExecutionTotals`, `ExecutionControls`, `ExecutionHooks`, `throttleDelaySeconds()`. |
| `src/services/execute/ExecutedSnapshot.*` | `snapshotAfter()`: the revalidated snapshot patched with the runs read back after each moved file's last move, or nullopt (then the saved snapshot is removed). |
| `src/services/execute/Execution.*` | `runExecution()` (revalidate (`onRevalidated` hook), log, remove the stale saved snapshot, execute on the volume, save the snapshot after), `executeLogged()`, `freshExecution()`, `resumedExecution()`, `ExecutionRequest`, `ExecutionOutcome`, `ExecutionRunControls`. |
| `src/services/execute/ExecutionLog.*` | `ExecutionLogWriter` (JSON lines: plan, moves, run events, dropped, begin, outcome, end; hooks), `parseExecutionLog()` / `readExecutionLog()` (cut-short last line tolerated), `ExecutionLogContents`, `movesToResume()`, `executionLogDirectory()`, `newExecutionLogPath()`, `unfinishedExecutionLog()`. |
| `src/services/execute/MoveDevice.h` | `MoveDevice` (open by reference, free runs of a range) and `MovableFile` (runs now, one move call): what the executor moves on. |
| `src/services/execute/VolumeMover.*` | `VolumeMover`: the move device of a real volume (OpenFileById, streams by ordinal, `FSCTL_MOVE_FILE`, ranged bitmap). |
| `src/services/scan/AnalysisJob.*` | `analyzeVolume()` (bitmap, file scan, figures; shared by GUI and CLI), `makeAnalysisJob()` (adds the early, partial and final maps), `AnalysisResult`, `AnalysisOutput` (newest map and summary for the UI), `bitmapSnapshot()`, `summaryOf()`. |
| `src/services/scan/AnalysisReport.*` | `analysisJson()` (volume, scan, summary, optional per-file runs), `analysisText()`, `fileFlagNames()`. |
| `src/platform/win/Compression.*` | `XpressCodec`: snapshot block compression with the Windows Compression API (XPRESS Huffman). |
| `src/platform/win/MappedMemory.*` | `MappedFileResource`: large allocations in mapped, delete-on-close temporary files. |
| `src/platform/win/MftReader.*` | `MftReader` (overlapped unbuffered volume reads: `read()`, `stream()` with N in flight), `BufferPool`/`PooledBuffer` (bounded, page-aligned), `ReadStats` (busy time, device busy % from `IOCTL_DISK_PERFORMANCE`, starvation). |
| `src/platform/win/MoveFile.*` | `moveClusters()` (one `FSCTL_MOVE_FILE`), `moveClustersChunked()` (calls of at most N clusters, `keepGoing` between them), `classifyMoveError()` / `MoveError` / `moveErrorKey()`, `openVolumeForMoves()`, `openStreamForMove()` (stream ordinal: 0 main or directory index, 1..n named streams by path, same file id checked), `fileReferenceOf()`, `finalPathOf()`, `freeRunsIn()` (bitmap of one range), `setBackgroundIo()`. |
| `src/services/snapshot/UsnCatchUp.*` | `catchUpSnapshot()` (journal from the saved cursor, changed and metadata records re-read, the saved snapshot patched or, when the patch refuses, every file finished again; dirty ranges), `catchUpBlocker()` (rescan rules), `CatchUpResult` (with `patched`). |
| `src/services/snapshot/MappedStorage.*` | `storageFor()` (mapped files on the last memory rung), `MemoryGovernor` (low-memory signal steps the plan down), `defaultCacheDirectory()`. |
| `src/services/settings/Settings.*` | `Settings` (per-drive `DriveStrategy`: preset id and effort), `settingsJson()`, `parseSettings()` (versioned, `SettingsError`, bad drive entries skipped), `driveSettingsKey()` (volume GUID, else root), `effortId()` / `effortFromId()`. |
| `src/services/settings/SettingsStore.*` | `SettingsStore`: `%LOCALAPPDATA%\IroncladDefrag\settings.json` loaded at start (damaged file kept as `.bad`), atomic save after each change, `driveStrategy()`, `rememberDriveStrategy()`. |
| `src/services/snapshot/SnapshotStore.*` | `SnapshotStore`: per-volume snapshot files under `%LOCALAPPDATA%\IroncladDefrag\snapshots`, atomic save, `peek()`, `load()` with a reason when rejected, `remove()`. |
| `src/services/scan/Classification.*` | `classifyVolume()` (built-in catalog plus churn flags: classes per file and per-class rows), `classSummary()` (files and bytes on disk per class, parallel). |
| `src/services/scan/MftScanner.*` | `scanFilesMft()`: map of `$MFT` from record 0, read plan, streamed reads to parser tasks, merge, USN cursor; `MftScanOptions`. |
| `src/services/scan/MftRows.*` | Parser output per chunk (`ChunkRows`: record, name, segment rows, provisional extents), `parseChunk()`, `appendRecord()`. |
| `src/services/scan/MftMerge.*` | `RowIndex` (rows in record order, lookup by record), `mergeRows()` (files, names, parents, metadata, stream ordinals, flags, extents, reconcile), drafts (`draftFiles()`, `finishDrafts()`, `draftsOfSnapshot()`, `combineDrafts()`), `finishCatchUp()` (catch-up finish as a patch: new parents and flags for kept files, fresh drafts in full), `ExtensionMap`, `extraStreamBase`. |
| `src/services/scan/MftSecondPass.*` | `joinExtensionRecords()`: attribute lists (resident or read through a `ListReader`) decide which extension records join which base file; `SecondPassStats`. |
| `src/services/scan/ScanRules.*` | Rules both scanners share: `unattributedName`, pinned root names, attribute bits to file flags, extent flags, residency; `scannerName()`. |
| `src/services/scan/ScanStats.h` | `ScanStats` (shared by both scanners), `ScannerKind`, `MftReadFigures`. |
| `src/services/scan/ScanCompare.*` | `compareWithApi()` (MFT scan against the API reference), `compareExact()` (snapshot equality), `ScanComparison`. |
| `src/services/scan/ApiScanner.*` | `scanFilesApi()`: reference scan (enumeration, open by id in MFT order, metadata and retrieval pointers per stream, special-case flags, reconcile, snapshot with USN cursor); statistics in `ScanStats`. |
| `src/services/trim/TrimJob.*` | `makeTrimJob()` (defrag /L as a job, percentage as progress), `LineSplitter` (CR or LF), `retrimPercent()`, `TrimOutput` (lines for the Log tab). |
| `src/services/workflow/Workflow.*` | Workflow state machine (states, actions, which job to start; `DriveKind::AnalyzeOnly` never executes); no wx, unit-tested. |
| `src/services/workflow/SyntheticAnalysis.*` | Analysis of fake drives: synthetic snapshot shaped like the drive, then `MapData` on the job's executor, handed over through `AnalysisOutput`. |
| `src/services/workflow/FakeResults.*` | `AnalysisSummary` (with `ScanFigures` from a real scan), `PlanSummary`, fake analysis for fake drives, fake three-level plan (UI shell without a map) and skipped files. |
| `src/ui/bridge/JobEvents.*` | `JobEventBridge` (job callbacks to `wxQueueEvent`), `jobProgressEvent()`, `jobFinishedEvent()`, payloads. |
| `src/services/AppLogging.*` | `initAppLogging()`/`shutdownAppLogging()`: installs the sinks, writes startup/shutdown lines. |
| `src/precompiled.h` | GUI PCH: `<wx/wx.h>` and std headers only. |
| `src/main.cpp` | `wxIMPLEMENT_APP(icd::App)`. |
| `src/ui/App.*` | `icd::App`: starts logging and `AppServices`, creates the main frame (`--drive X:` selects a drive), stops services on exit. |
| `src/ui/MainFrame.*` | Main window: menu, drives panel, map/bottom splitter (bottom keeps its height); creates the workflow binding. |
| `src/ui/WorkflowBinding.*` | Commands to workflow actions, the job per step (all real; fake drives keep the synthetic analysis and a simulated execution), the elevation check before disk actions, the Execute confirmation, Progress fields with worker utilisation, plan overlays to the map (`dropPlan()` clears them), Build plan blocked by strategy problems (`allowed()`), progress and results to the panels, TRIM output to the Log tab, command enable state, per-drive strategy restored on selection and remembered on change. |
| `src/ui/MainMenu.*` | `createMainMenuBar()`: the section 11 menu as tables; `stateDependentCommands()`. |
| `src/ui/Accelerators.*` | One table of menu shortcuts with a description each (`applyMenuShortcuts()`, `shortcutFor()`, `shortcutText()`, `commandTip()` for tooltips; Ctrl+F go to file, F3 / Shift+F3 fragments, F1 shortcut list), and the map keys of plan.md 7.4 with descriptions (`mapKeyBindings()`, `mapKeyText()`, `mapKeyFor()`, `commandForMapKey()`). |
| `src/ui/ExecutionView.*` | Follows one real execution for the UI: its job, Progress fields (bytes, throughput, time left, current file, move counts), Log rows, final status. |
| `src/ui/dialogs/ScheduleDialog.*` | Drive > Windows schedule: why Windows' scheduled optimization may undo zones, Remove / Put back; Cancel is the default button. |
| `src/ui/dialogs/ConfirmExecuteDialog.*` | Execute confirmation: the level's figures and warnings, safety note, low I/O priority and throughput limit; Cancel is the default button. |
| `src/ui/dialogs/AboutDialog.*` | Help > About: version, build, detected resources (re-detected), active caps. |
| `src/ui/dialogs/ShortcutsDialog.*` | Help > Keyboard shortcuts (F1): `shortcutLines()` groups the shortcut table by the menu holding each command, then the map keys (keys of one action on one line); shown as a list with bold headings. |
| `src/ui/dialogs/GoToFileDialog.*` | View > Go to file: search field and match list (path, size, fragments); searches on the pool as the user types (200 ms pause, cancelled when replaced), results through a detachable mailbox. |
| `src/ui/CallbackTimer.h` | `CallbackTimer`: a `wxTimer` calling a function, for helpers without a window (live map, execution view). |
| `src/ui/Commands.*` | `CommandId` (shared by menus, buttons, accelerators; later ids appended so values stay stable), `workflowCommands()` (the bottom bar's steps, also the drive list's context menu), `isMapCommand()`, `enableCommand()` for menu item + button. |
| `src/ui/tabs/LogTab.*` | Log tab: time, file, result, details; counts per result above the list; follows new rows while the newest is in view. |
| `src/ui/tabs/ProgressTab.*` | Progress tab: stage, done, elapsed, time left, throughput, workers, current file, moves (two per row), resources line. |
| `src/ui/panels/BottomPanel.*` | Bottom notebook (Summary, Strategy, Plan, Progress, Log), workflow buttons (tooltips with action and shortcut), the single status line and gauge. |
| `src/ui/panels/SummaryPage.*` | Summary tab: volume figures (plus directories, fragmented data, large free runs, unmovable, unreadable and changed files after a real scan) and per-class table. |
| `src/ui/panels/StrategyPage.*` | Strategy tab: built-in preset picker, effort level, editable zone table (classes, action, anchor, orders, fragment_ok, gap), validation problems under it; `script()` for the next plan, `problems()`; `select()` restores a remembered choice without reporting a change. |
| `src/ui/panels/PlanPage.*` | Plan tab: Fast / Moderate / Maximum side by side, chosen level, warnings; with shadow copies the unaligned moves per level and "Block unaligned moves". |
| `src/ui/dialogs/VssWarning.*` | Before Execute with unaligned moves on a drive with shadow copies: block and plan again, execute anyway, or cancel (default). |
| `src/ui/panels/DrivesPanel.*` | All-disks panel: `wxDataViewCtrl` over a virtual model, space bar and TRIM button renderers (tooltip on the drawn TRIM button), selection callback, right click selects the row and asks for the context menu. |
| `src/ui/panels/MapPanel.*` | Map area (`updateMap()`, `setDisk()` for the rulers): toolbar (color mode, caption, selected file and fragment, "Find file" search opening Go to file) above the `MapCanvas` with rulers left and above and the legend below; forwards the canvas's view changes to the rulers and the legend; View > Color mode (zone fit and plan preview with a plan's overlays, `setPlanOverlays()`), Minimap, Legend, Go to file, next/previous fragment; right click on a file: Show in Explorer, Copy path (`setVolumeRoot()`). |
| `src/ui/map/Legend.*` | Legend strip: swatch, label, size and share of the visible area per entry of the color mode; counts on the pool after the view rests, cancellable, delivered through a mailbox. |
| `src/ui/map/MapCanvas.*` | Drive map widget (`updateMap()` takes newer data of the same volume, keeping the view; with dirty runs only the tiles over them render again and the selection follows its file): Direct2D composition of cached tiles, coarser cached tiles scaled while exact ones render, async tile rendering on the pool via a detachable mailbox (96 in flight, 16 uploads per frame), LRU GPU cache (256 MB), selection highlight and plan overlay (`setOverlay()`) painted into tiles with dirty-tile refresh, frame-time log per animation, `missingTiles()` of the last frame. |
| `src/ui/map/MapSelection.*` | Selected file: click (press and release within 4 DIP, not on the minimap) selects the owner of the cluster or clears on free space; fragments in file order, current fragment, next/previous and go to with an animated focus view; observer for the toolbar label. |
| `src/ui/map/MapLiveUpdates.*` | Follows a run on the map: a 250 ms ticker takes the feed's moved records, one update at a time on the pool (rebase first, then `applyMoves()`), spaced by twice its duration (500 ms at least), shown with `MapPanel::updateMap(map, dirty)`; stops when the moves do not fit or another map is shown; logs a summary. |
| `src/ui/map/MapHover.*` | Hover tooltip: `wxPopupWindow` after a 350 ms rest, follows the pointer, hides on press/drag/wheel/key/leave; local time zone for file times. |
| `src/ui/map/Minimap.*` | Minimap overlay drawn in the canvas's Direct2D frame: holds the coarsest tile as its image, frame and view marker, drag state (grab offset). |
| `src/ui/map/Ruler.*` | Ruler windows (GDI, buffered) beside the map: vertical rows with offset, % and % of disk (`setDisk()`), horizontal offsets within a row; sized for their widest label, refreshed on view changes. |
| `src/ui/map/MapNavigator.*` | Map input: minimap drags and hand cursor, drag with inertia, wheel and pinch zoom at the pointer, two-finger pan, double-click zoom, map keys, View zoom commands; 8 ms frame timer. |
| `src/ui/map/D2DRenderer.*` | Direct2D on one HWND: target creation/resize, tile upload as GPU bitmaps, nearest-neighbour or linear drawing with opacity, no edge anti-aliasing (no tile seams), overlay rectangles, rectangle sets blended as one layer (`fillRects`, the grid), device-loss detection. |
| `src/cli/main.cpp` | `icd-cli`: `--version`, `--help`, dispatch to commands. |
| `src/cli/commands/CheckCaps.*` | `icd-cli check-caps <volume> [--threads N] [--memory-budget B]`: capped (mapped) and uncapped full scans compared exactly. |
| `src/cli/commands/CheckCatchUp.*` | `icd-cli check-catchup <volume>`: saved snapshot caught up and compared with a full MFT scan (access times aside). |
| `src/cli/commands/Churn.*` | `icd-cli churn <volume> [--json | --all]`: MFT scan plus the journal's churn statistics; churning directories, flagged files, every directory's counts with --all. |
| `src/cli/commands/CliCommon.*` | Shared by commands: `ConsoleProgress`, `normalizeRoot()`, `findDrive()`, `refusal()`, `ownExecutor()`, `scannerNamed()`, `effortNamed()`, `presetNamed()`, `presetIds()`. |
| `src/cli/commands/CompareScanners.*` | `icd-cli compare-scanners <volume> [--threads 1,2,8]`: API scan, MFT scans per worker count, comparisons and times. |
| `src/cli/commands/Plan.*` | `icd-cli plan <volume> [--strategy ID] [--level fast|moderate|max] [--json] [--moves] [--threads N] [--synthetic FILES] [--save FILE] [--block-unaligned]`: analysis (or a synthetic volume), three levels planned, report on stdout; `--save` writes the chosen level for `icd-cli execute --plan`. |
| `src/cli/commands/Schedule.*` | `icd-cli schedule <volume> [--remove \| --restore]`: Windows' scheduled optimization of a volume, changed as the GUI does. |
| `src/cli/commands/Execute.*` | `icd-cli execute`: plan now, `--plan FILE` or `--resume [--log FILE]`, options (chunk, priority, throughput, age, prefetch, `--check-rescan`), `--max-moves N` (a prefix of the plan), test hooks (`--pause-after`, `--cancel-after`, `--take-destination`), JSON or text report, exit codes. |
| `src/cli/commands/Analyze.*` | `icd-cli analyze <volume> [--json] [--dump-extents] [--scanner api|mft] [--threads N]`: drive lookup, checks, console progress, Ctrl+C cancel, report on stdout. |
| `tests/integration/vss/check-vss.ps1` | T121: the `vss` scenario executed with unaligned moves blocked; shadow storage grows only by the moves' file system records (1 MB + 48 KB per move); `-Contrast` shows the growth without the block. |
| `tests/integration/schedule/check-schedule.ps1` | T120: a test VHDX taken out of Windows' schedule and put back through `icd-cli schedule`; task arguments and TaskSettings checked, touched values restored. |
| `tests/integration/topology/check-topology.ps1` | T122: a temporary Storage Spaces pool of two VHDX files; its volume is planned, `icd-cli execute` refuses it naming Storage Spaces; pool and files removed. |
| `tests/integration/planner/check-planner.ps1` | T095: per scenario a read-only test volume planned with its preset through `icd-cli plan --json --moves`; validity, ratio <= 1.1 per level, Fast within 2% and inside hot spots. |
| `tests/integration/execute/check-execute.ps1` | T104 / Phase 7 exit (about 12 min for `-Scenarios all`): per scenario a 1 GB volume with a journal, every execution capped by `-MaxMoves`: changes between plan and execute dropped, main run with a stale bitmap, on `mixed` pause/cancel and kill then resume; after each run the report checks and `check-catchup`, at the end hashes and `chkdsk /scan`; `-ChurnGap` measures churn-gap allocation on `dev`; step timings. |
| `tests/integration/classes/check-classes.ps1` | T074 / Phase 5 exit: read-only `mixed` volume, every file's class against the scenario layout, summary bytes per class = dumped clusters, counts cover every file. |
| `tests/integration/churn/check-churn.ps1` | T073: journal first, then the dev scenario; every build output and temp directory is churn, no source, dependency or repository directory. |
| `tests/integration/catchup/check-catchup.ps1` | T065: a test volume with a journal, rounds of scripted changes, `check-catchup` after each, up-to-date and recreated-journal cases. |
| `tests/integration/scanner-equivalence/check-scanners.ps1` | T063 cross-check: per scenario a fresh read-only volume through `icd-cli compare-scanners --threads 1,2,0`; `-RealDrives E,X`; `-ScaleDrive S` with the 10x speed-up check. |
| `tests/integration/analysis/check-analysis.ps1` | Phase 3 exit check: per scenario a fresh read-only test volume analysed by `icd-cli analyze --json --dump-extents`, compared with `fsutil volume diskfree` and `fsutil file queryextents`, plus cluster accounting; `-RealDrive X` for a data drive. |
| `tests/CMakeLists.txt` | Fetches pinned GoogleTest, defines `icd_tests`, `gtest_discover_tests`. |
| `tests/core/MftLayoutTest.cpp` | Read plans: capped chunks, skipped stretches, volume order, split records, random layouts against per-record mapping, segment joining, holes. |
| `tests/services/MftMergeTest.cpp` | Rows and merge on handcrafted records: names, links, stale parents, metadata, pinned, streams, extra attributes, sparse, sizes; same snapshot for any executor and chunking; damaged records counted. |
| `tests/services/PlanJobTest.cpp` | Parallel plans = serial plans move for move (levels in parallel and one after another), summary figures, the job's output, overlays and progress, the job classifying a request without classes |
| `tests/services/PlanReportTest.cpp` | CLI report = GUI job's report for the same snapshot; levels, figures and move list in the JSON; text report. |
| `tests/services/SettingsTest.cpp` | Settings JSON round trip, refused documents, skipped drive entries, effort spellings, drive keys, built-in preset ids; store in a temp directory: defaults, restart, damaged file kept aside, unwritable file. |
| `tests/services/SnapshotStoreTest.cpp` | Save/load round trips (XPRESS, block sizes, executors give the same bytes), meta-only reads, other versions and foreign files, damage to every header byte and random payload bytes, truncation, the store's files. |
| `tests/services/ChurnStatsTest.cpp` | Sessions from close records, deletes where the file lived (POSIX delete into $Deleted) and before their close record, directory churn rule, small folders with churning siblings, flags below churning directories. |
| `tests/services/ClassificationTest.cpp` | Class rows add up to the volume (files, bytes), catalog order, any executor; churn turns content into temp. |
| `tests/services/CatchUpPatchTest.cpp` | Catch-up patch = full finish on random volumes and changes (deletes, moves into free and unclaimed space, renames, new files between records), serial = parallel over four rounds, refusals (fresh over kept extents, kept files turning metadata) leave the saved snapshot untouched. |
| `tests/core/SnapshotPatchTest.cpp` | `patchSnapshot()` on a hand-made volume: released clusters freed, placeholder remainder, new extent placement; invalid patches refused with the base untouched. |
| `tests/services/UsnCatchUpTest.cpp` | Rescan rules, `combineDrafts()` order and replacement, snapshot drafts and re-read records finish into the scanned snapshot (ICD-TEST volume). |
| `tests/core/MapUpdateTest.cpp` | `MapData::update()` over dirty ranges equals a full build; other volume sizes refused. |
| `tests/services/MemoryModesTest.cpp` | Estimate, ladder and give-way, mapped vectors (move keeps, copy leaves, files deleted), snapshots built into mapped files equal heap ones, storage per plan. |
| `tests/services/MftScannerTest.cpp` | MFT scan of the `special` volume: 1 worker = 4 workers, read figures, every cluster accounted for, `$MFT` at its start LCN; equal to the API scan, 1,324-fragment file resolved. |
| `tests/services/MftSecondPassTest.cpp` | Extension records joined only when listed with the right sequence, non-resident and unreadable lists, names and runs across records, same result for any executor. |
| `tests/core/MoveOrderingTest.cpp` | Chains in dependency order, swaps and cycles parked (smallest file) or dropped with dependents, no scratch space, room finding (the parallel search finds the same room); plans validate. |
| `tests/core/NtfsParserTest.cpp` | Fixups, run lists (holes, deltas, malformed, overflow), names, attribute lists, handcrafted records, captured `special` records (MFT, root, resident, compressed, sparse, hard links, reparse points, streams, Unicode, EFS, big directory, 1,324-fragment file over five extension records), fuzzed and truncated records. |
| `tests/core/NtfsRecordBuilder.h` | Test helper: MFT records byte by byte (header, attributes, update sequence) and run list encoding. |
| `tests/data/ntfs/*.bin` | Raw MFT records captured from the `special` scenario (4 KB clusters) by `capture-mft-records.ps1`. |
| `tests/core/SnapshotTest.cpp` | Snapshot building, LCN and file lookup, split runs, paths, bad input, free runs, order independence. |
| `tests/core/SnapshotStatsTest.cpp` | Figures of a hand-made volume, parallel = serial on a synthetic one. |
| `tests/core/SortZoneTest.cpp` | Weighted LIS = brute force, keys per order, only files at their packed places stay (plan validates), tolerance, a synthetic zone ends in order. |
| `tests/core/StripesTest.cpp` | Stripe layout coverage, view coverage on random volumes, parallel = serial on 2-16 threads. |
| `tests/core/SyntheticVolumeTest.cpp` | Seed reproducibility, fill level, fragmented share, free patterns, structure. |
| `tests/core/ViewportTest.cpp` | Pixel/world round trip, fit and limits, anchored zoom, pan clamping, eased zoom motion, durations, inertia. |
| `tests/core/WorldTest.cpp` | Nice widths, landscape/portrait aspect, round trips and edge cells, levels, tile clipping, row segments. |
| `tests/core/AccessTimeHeuristicTest.cpp` | Which files count as read after writing (hour of slack, copies), parallel = serial, setting and evidence combined. |
| `tests/core/BudgetTest.cpp` | Cut plans within cluster and time budgets and valid, valuable moves kept, dependencies kept together, every prefix validates with fewer fragments. |
| `tests/core/BitmapToRunsTest.cpp` | Edge cases, random bitmaps vs per-bit brute force, random chunk cuts stitched = whole, parallel = serial for several grains. |
| `tests/core/ColorModesTest.cpp` | Age and size bucket edges, bucket values, legend counts vs per-cluster brute force (all modes, random rectangles, pool = serial), tile colors = legend colors, visible cells. |
| `tests/core/PlanOverlayTest.cpp` | At one pixel per cluster every tile pixel of the plan-preview overlay matches the Maximum plan's moves, and the zone-fit overlay marks exactly the placed data outside its zone. |
| `tests/core/PyramidTest.cpp` | Base-level choice, every bin vs brute force, dominant class, parallel = serial, dirty-range update = rebuild. |
| `tests/core/TileRasterizerTest.cpp` | Capacity, deep-zoom cells (no grid) on a hand volume, pyramid = extents at the base level, golden checksums, record block index = search and tiles with it = tiles without. |
| `tests/core/SmokeTest.cpp` | `formatBytes()` and `formatCount()` tests. |
| `tests/core/JobTypesTest.cpp` | Cancellation propagation, progress throttling, pause gate, stage clock. |
| `tests/services/TrimJobTest.cpp` | Line splitting across chunks (CR, CRLF), retrim percentages, output queue. |
| `tests/services/WorkflowTest.cpp` | Every workflow transition, Quick, cancel and failure paths, disallowed actions, analyse-only drives. |
| `tests/core/OptimalityTest.cpp` | Exhaustive minimum on tiny volumes against Maximum plans and the lower bound; plan / lower-bound ratios of every preset and level on synthetic volumes (1.1, presets that sort large zones 1.5 at Maximum). |
| `tests/core/CycleGuardTest.cpp` | Swaps and longer cycles reported before they are taken, `cycleEnd()` past the file that closes one, waits recorded against the order still found after its repair. |
| `tests/core/OverviewTest.cpp` | Minimap placement, hiding, pixel/world round trips, view marker, dragging. |
| `tests/core/CatalogTest.cpp` | One test per built-in class with typical and edge-case paths; every class reachable and colored; bad catalogs refused. |
| `tests/core/DefragOnlyTest.cpp` | Fits that avoid own runs, large pieces, defragmenting near the file, skips, and on synthetic volumes: plans validate and the untouched files are exactly the good-enough set. |
| `tests/core/LevelsTest.cpp` | All three levels validate on synthetic volumes; Moderate within Maximum's bytes and goals; Maximum parks where Moderate leaves an unsorted zone; policy table. |
| `tests/core/LowerBoundTest.cpp` | Fragment rules, must-move files on hand-made layouts, defragment-only files, slack moving zones towards files, conflicts, front zones, fitting = brute force over every slack split on random volumes. |
| `tests/core/HoleSetTest.cpp` | HoleSet against a per-cluster model under random takes and adds across block splits; fits in both directions; merging. |
| `tests/core/VssAlignTest.cpp` | Units for 512 B-64 KB clusters, aligned moves, shifting destinations, no room, the report; blocking at every level leaves only aligned moves and valid plans; drops follow dependencies and parked pairs. |
| `tests/core/ValidatorTest.cpp` | Valid chain; broken plans for every validation rule (busy and late destinations, unmovable files, stale and reordered sources, lost clusters, zones, parking, accounting, totals); the parallel checks agree with the replay on a planned Maximum level and catch a destination taken twice. |
| `tests/core/PackToBackTest.cpp` | Packed videos never move at any level, holes in the back zone closed, oldest at the end at Maximum (plans validate). |
| `tests/core/PlannerTestUtil.h` | Planner test helpers: volumes drawn as pictures (one character per cluster), zones and scripts, assignment. |
| `tests/core/TargetLayoutTest.cpp` | Axis vs brute force, zone selection and roles, `System Volume Information` pinned, layouts with obstacles everywhere, full volumes, empty zones, gaps, reserved runs, slack placement. |
| `tests/core/presets/PresetsTest.cpp` | Every preset parses, validates and round-trips; plan.md details of video and development; every preset at every level on random synthetic volumes validates. |
| `tests/core/ZoneScriptTest.cpp` | Zone script round trip with every field, defaults, per-level order, size specs, read errors per zone and key, every validation rule, zone-table texts (gap, fragment_ok, ids, enum names) round trip. |
| `tests/core/JsonReaderTest.cpp` | Values, escapes and surrogates, writer round trip, malformed input, error positions, kind checks. |
| `tests/core/RuleEngineTest.cpp` | Globs (anchored, `**`, `?`, case), rule order, size/age/flag/churn conditions, overrides (deepest, directories untouched), bad rule sets, snapshot classes = path classes on a dev tree and a synthetic volume, any executor. |
| `tests/core/RulerTest.cpp` | Tick steps, label formats, row/column ticks against the world math at fit and deep zoom, minor ticks, disk %. |
| `tests/core/HitTestTest.cpp` | Fragment i/n, deep-zoom and free hits, off-volume, brute force at several zooms (hand volume) and every level (synthetic volume), file time and tooltip text. |
| `tests/core/FastFillTest.cpp` | Tail files into head holes (bytes = lower bound, plan validates), best fit, back packing, never onto own clusters, synthetic volume destinations checked statically (in zone, disjoint, free or vacated, counts kept). |
| `tests/core/FastLevelTest.cpp` | Stripe scores and bridged hot spots, big long-lived file to its zone, synthetic Fast plans valid, only inside hot spots, within budget. |
| `tests/core/FileSearchTest.cpp` | Ranks, case, path queries (slashes, trailing backslash, mid-name start), empty queries, limit and total, brute force over built paths on a synthetic volume, pool = serial. |
| `tests/core/GridTest.cpp` | Grid fade and edge rounding, no grid at small cells, lines exactly on the last pixel of each drawn cell at random fractional views (also at the partial last row), line count bounded by the window. |
| `tests/core/ScanReconcileTest.cpp` | Placeholder for unclaimed clusters, newer read wins, stale free space, clipping, random scans always tile. |
| `tests/core/SelectionTest.cpp` | Fragments vs `fragmentCount()`, overlay merge, run shapes, highlight vs per-cell brute force at every level, refreshed tiles = changed tiles on a synthetic volume, focus view. |
| `tests/core/LruCacheTest.cpp` | LRU eviction by cost, replace/erase/oversize, tile keys; `MapData` build. |
| `tests/core/JsonTest.cpp` | JSON structure and indentation, one-line output, string escaping, non-finite numbers. |
| `tests/core/LogTest.cpp` | Log line format, escaping, level filter, concurrent writes. |
| `tests/platform/ChildProcessTest.cpp` | Output and exit code capture, cancel terminates the child, start failure. |
| `tests/platform/LastAccessSettingTest.cpp` | Every NtfsDisableLastAccessUpdate value decoded; the registry reading equals `fsutil behavior query disablelastaccess`. |
| `tests/platform/LogSinksTest.cpp` | File sink writing, rotation and reopen in a temp directory. |
| `tests/platform/MoveFileTest.cpp` | Move error mapping; on a writable ICD-TEST volume (slow, elevated): a file, a named stream and a directory index moved in 7-cluster chunks, runs re-read, bytes read back unbuffered unchanged, a move onto used clusters refused; a refused 4,096-cluster call keeps the prefix before the taken cluster. |
| `tests/services/JobRunnerTest.cpp` | Job outcomes, cancel, destructor wait, progress throttling, fake job. |
| `tests/services/ThreadPoolTest.cpp` | Identical reductions for 1..N threads, coverage, errors, fast cancel, nested loops, busy time. |
| `tests/services/PlannedBed.h` | A planned synthetic volume with its classes and plan and a `SimVolume` to run it on (feed and live map tests). |
| `tests/services/LiveMapTest.cpp` | Moves applied in batches equal a full build of the snapshot after them; rebase on a changed volume, then the rest; nothing to apply and a patch that does not fit. |
| `tests/services/ShellActionsTest.cpp` | Full paths from volume roots (letters, mount folders) and snapshot paths. |
| `tests/services/ExecuteJobTest.cpp` | `ExecutionFeed` following the executor on `SimDevice` (counts, bytes, drops and skips in words, moved records), the Log tab cap, reason and summary texts. |
| `tests/core/MovePiecesTest.cpp` | Pieces at the sources, streams and VCNs, moves cut short and done, changed files refused, remap, joined LCN runs. |
| `tests/core/MoveCallsTest.cpp` | Chunks of plain pieces, whole compressed units joined over holes and bounded by the next piece, retarget, landings, the landed prefix of a refused call. |
| `tests/services/ExecutorTest.cpp` | On `SimDevice`: a plan runs and verifies (with and without prefetch), pause and resume, cancel mid-move then the rest after revalidation, a taken destination moved elsewhere in its region, locked and changed files skipped, throughput limit, snapshot after execution = volume; a refused call keeps what NTFS moved before it and the snapshot after matches. |
| `tests/services/ExecuteReportTest.cpp` | The execute report from a simulated run parses as JSON with every section (a locked file among the problems, records, zones) and the text names the totals. |
| `tests/services/ExecutionLogTest.cpp` | Plan round trip through the log, cut-short last line only, a crash mid-move resumed from the log to the full plan, unfinished logs found by volume. |
| `tests/services/SimDevice.h` | `testing::SimDevice`: FSCTL_MOVE_FILE semantics over a `SimVolume` (optionally in pieces that stay moved when a later piece is refused, as on NTFS), locked files, a hook before each call, `SimulatedCrash` after N calls. |
| `tests/services/RevalidateTest.cpp` | Refusals (other volume, serial, size, NTFS, age); unchanged volume keeps and runs every move; scripted deletes, changes and taken destinations dropped with reasons while the rest still runs; moves cut short kept with their rest. |
| `tests/services/SimVolume.h` | `testing::SimVolume`: a volume in memory (owner per cluster, files as clusters in stream and VCN order) for revalidation and executor tests; `move()`, `remove()`, `occupy()`, `release()`. |
| `tests/core/DriveInfoTest.cpp` | Drive model rules, move blocks, column texts, badges, durations and ages. |
| `tests/services/AnalysisJobTest.cpp` | Bitmap-only snapshot; full job on the `special` volume (stages, final map, summary); cancel during the scan. |
| `tests/services/AnalysisReportTest.cpp` | JSON report carries the summary and USN cursor; dumped runs joined; flag names. |
| `tests/services/ApiScannerTest.cpp` | On the `special` test volume: every special case flagged, extent kinds, every cluster accounted for, extents = `fsutil file queryextents` for sampled files. |
| `tests/services/FakeDriveProviderTest.cpp` | The fake list covers every row style; the simulated Windows schedule. |
| `tests/core/ResourceCapsTest.cpp` | `computeCaps()` edge cases (1 CPU, 2 GB, 128 threads, manual values) and `describeCaps()`. |
| `tests/platform/ScheduledOptimizeTest.cpp` | The dialog's three argument forms round-trip, matching by any name, opt-out and restore steps; the live task vs `Get-ScheduledTask` (slow). |
| `tests/platform/ShadowCopiesTest.cpp` | Listing parse; the `vss` scenario volume shows a copy (slow, elevated). |
| `tests/platform/VolumeTopologyTest.cpp` | LDM partition rule, volume keys; every disk's layout vs `Get-Partition` and BitLocker states vs `Get-CimInstance` (slow). |
| `tests/platform/StoragePropertiesTest.cpp` | Media and bus of every physical disk vs `Get-PhysicalDisk`; bus names. |
| `tests/platform/UsnJournalTest.cpp` | Enumeration = directory walk (reference, name, parent) on ICD-TEST volumes; Windows drive journal state; record numbers. |
| `tests/platform/VolumeBitmapTest.cpp` | Free runs = NTFS free clusters and diskfree (minus reserved) on ICD-TEST volumes, small pieces = large pieces; Windows drive within 1%. |
| `tests/platform/VolumesTest.cpp` | Volume enumeration (Windows drive, disk placement, order), NTFS data vs `fsutil fsinfo ntfsinfo` (elevated only), `toDriveInfo()`. |
| `tests/platform/SystemResourcesTest.cpp` | Detection plausibility and the low-memory signal. |

## Tools

| Path | What it does |
| --- | --- |
| `tools/bench/synth-bench.cpp` | `synth-bench` target: builds a synthetic snapshot (default 30 TB / 20 M files), its color values and pyramid on 8 and all threads, and times tiles per level, whole-volume legend counts, file searches and a selection change's dirty-tile check; `--dump DIR` writes sample tiles as BMP. |
| `tools/spike/map-render-spike/main.cpp` | `map-render-spike` target (T026): times 4K tile composition with wx Direct2D, raw Direct2D and GDI during scripted zoom and pan. |
| `tools/bench/scan-bench.cpp` | `scan-bench` target (T067): plan.md 6.5 scan and catch-up targets on a scale volume: bitmap, MFT scan and busy share, snapshot memory, save/load, a day of changes and the catch-up (= full rescan), capped mapped scan (= uncapped). |
| `tools/bench/catchup-bench.cpp` | `catchup-bench` target (T069): synthetic 30 TB / 20 M-file saved snapshot, a day of re-read records, catch-up finish as a patch timed against the full finish and compared. |
| `tools/bench/map-bench.cpp` | `map-bench` target (T033): plan.md 7.5 checks on the 30 TB synthetic map: pyramid on 8 threads, map memory, tile times per level and color mode, and the real `MapCanvas` in a window through scripted zoom and pans (fps, stand-in frames); `--no-window 1` skips the window. |
| `tools/testvol/make-testvol.ps1` | Test VHDX volumes through diskpart (admin): `-Action Create` (dynamic VHDX, GPT, NTFS with `-ClusterSize`, `-Letter` or a free one from T down), `Mount` (`-ReadOnly` for no-write checks), `Dismount`, `Remove`, `Info`; files in `%ICD_TESTVOL_DIR%` or `%LOCALAPPDATA%\IroncladDefrag\testvol`. |
| `tools/testvol/gen-scenario/` | `gen-scenario` target: `gen-scenario <scenario> <drive> [--seed N] [--fill SHARE] [--force]`. `FileOps.*` (seeded content, interleaved writers, appends, deletes, compression, sparse, links, junction, EFS, attributes, fragment count via retrieval pointers), `Scenarios.*` (dev with build cycles over the same objects and temp churn, archive, photos, video, mixed, almost-full, vss, special), `main.cpp` (safety checks, budget from the fill level, summary). |
| `tools/testvol/capture-mft-records.ps1` | Reads raw MFT records (fixups as on disk) and their extension records from an ICD-TEST volume into `.bin` test data. |
| `tools/testvol/make-scalevol/` | `make-scalevol` target: `make-scalevol <drive> --files N [--seed S] [--threads T] [--per-dir N] [--fragmented SHARE] [--min-size B] [--max-size B]`: scale volumes of allocated, never written files. |
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
| `build/` | clang-cl Ninja Multi-Config build with `compile_commands.json`; `build/bin/<Config>/` holds the exes and the wx DLLs of that config; `build/generated/` the version header and the embedded catalog source. |
| `build-msvc/` | Optional MSVC (VS 2026) build, same layout. |
| `build/quality/` | Ninja + clang-cl configure for the quality gate; `quality/` holds scope lists and the selected database. |
| `.venv-quality/` | Python venv with the pinned analyzers. |
| `3rdparty/downloads/` | Cached wx archives. |
| `3rdparty/wxWidgets/` | `include/`, `include/msvc/`, `lib/vc14x_x64_dll/` (import libs, debug and release DLLs), `icd-wx-stamp.txt`. |
