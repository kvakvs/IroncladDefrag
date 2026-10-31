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
| `IroncladDefrag` | GUI exe | `src/main.cpp`, `src/ui/**`, PCH `src/precompiled.h` | `icd_services`, `wx::wx` |
| `icd-cli` | console exe | `src/cli/**` | `icd_services` |
| `icd_tests` | console exe (GoogleTest, CTest) | `tests/**` (own `tests/CMakeLists.txt`) | `icd_core`, `icd_platform`, `icd_services`, `GTest::gtest_main` |
| `check-complexity[-all]`, `check-clang-tidy[-all]`, `check-quality[-all]` | custom | quality gate; clang-tidy only in Ninja builds with a compile database | - |

## Source files

| File | Purpose |
| --- | --- |
| `src/core/Version.h.in` | Version macros template; CMake writes `build*/generated/core/Version.h`. |
| `src/core/format/ByteSize.*` | `formatBytes()`: Explorer-style byte sizes. |
| `src/core/format/Count.*` | `formatCount()`: locale-neutral thousands separators. |
| `src/core/format/Duration.*` | `formatDuration()` (stopwatch), `formatAge()` ("3 h ago"). |
| `src/core/format/DriveText.*` | All-disks column texts: `driveName()`, `describeSpace()`, `describeFragmentation()`, `describeLastAnalysed()`, `badgeLabels()`. |
| `src/core/model/DriveInfo.*` | `DriveInfo`, `DriveBadges`, `MediaType`; `canOptimize()` (NTFS, not SSD), `isSsd()`. |
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
| `src/core/snapshot/DirectoryTree.*` | Child lists from parent indexes, roots, `path()` with a cycle guard. |
| `src/core/snapshot/ExtentIndex.*` | 16-byte `ExtentRecord` (40-bit LCN, 24-bit length), index by LCN (`find`, `overlapping`) and by file in VCN order (`extentsOf`, `fragmentCount`). |
| `src/core/snapshot/FreeRunList.*` | Sorted, merged free runs (`find`, `overlapping`, `largest`). |
| `src/core/snapshot/Snapshot.h` | The aggregate the map, classifier and planner read. |
| `src/core/snapshot/SnapshotBuilder.*` | Validating, order-independent builder (sorts, splits long runs, groups by file). |
| `src/core/snapshot/Stripes.*` | `StripeLayout` (fixed LCN stripes, 16 MB aligned, 512 on large volumes), `Stripes` views over extents and free runs, `clip()`, `mapStripes()` / `reduceStripes()` with stripe-order merge. |
| `src/core/resources/ResourceCaps.*` | `computeCaps()` (plan.md 6.6 worker and memory caps with reasons), `describeCaps()`. |
| `src/platform/win/SystemResources.*` | `detectSystemResources()` (groups, affinity, job CPU/memory limits, cores, RAM), `LowMemorySignal`. |
| `src/platform/win/WinError.*` | `formatWinError()`, `lastWinError()`: Win32 error text. |
| `src/platform/win/LogSinks.*` | `FileSink` (rotating `<name>.log`, flushed per line), `DebugOutputSink`, `defaultLogDirectory()`. |
| `src/services/AppInfo.*` | Product name and version (from `core/Version.h`), `buildDescription()` (configuration, compiler). |
| `src/services/AppServices.*` | Service composition root: resources (and `detectResources()` for a fresh reading), caps, `ThreadPool`, `JobRunner`, drive provider. |
| `src/services/drives/IDriveProvider.h` | Drive list source interface used by GUI and CLI. |
| `src/services/drives/FakeDriveProvider.*` | Phase 1 fake drives: SSD, three NTFS HDDs in different states, one exFAT drive. |
| `src/services/job/JobRunner.*` | `JobRunner`, `JobContext`, `JobObserver`, `JobResult`: jobs on the pool with cancel, throttled progress, one result. |
| `src/services/job/FakeJob.*` | `makeFakeParallelJob()`: serial + parallel stages with stage names, item labels and a pause gate; stands in for every workflow step in Phase 1. |
| `src/services/workflow/Workflow.*` | Workflow state machine (states, actions, which job to start); no wx, unit-tested. |
| `src/services/workflow/FakeResults.*` | Phase 1 fake analysis, strategy presets, three-level plan and skipped files. |
| `src/ui/bridge/JobEvents.*` | `JobEventBridge` (job callbacks to `wxQueueEvent`), `jobProgressEvent()`, `jobFinishedEvent()`, payloads. |
| `src/services/AppLogging.*` | `initAppLogging()`/`shutdownAppLogging()`: installs the sinks, writes startup/shutdown lines. |
| `src/precompiled.h` | GUI PCH: `<wx/wx.h>` and std headers only. |
| `src/main.cpp` | `wxIMPLEMENT_APP(icd::App)`. |
| `src/ui/App.*` | `icd::App`: starts logging and `AppServices`, creates the main frame, stops services on exit. |
| `src/ui/MainFrame.*` | Main window: menu, drives panel, map/bottom splitter (bottom keeps its height); creates the workflow binding. |
| `src/ui/WorkflowBinding.*` | Commands to workflow actions, fake jobs per step, progress and results to the panels, command enable state. |
| `src/ui/MainMenu.*` | `createMainMenuBar()`: the section 11 menu as tables; `stateDependentCommands()`. |
| `src/ui/Accelerators.*` | One table of menu shortcuts (`applyMenuShortcuts()`), and the map keys of plan.md 7.4 (`mapKeyFor()`, `commandForMapKey()`). |
| `src/ui/dialogs/AboutDialog.*` | Help > About: version, build, detected resources (re-detected), active caps. |
| `src/ui/Commands.*` | `CommandId` (shared by menus, buttons, accelerators), `enableCommand()` for menu item + button. |
| `src/ui/panels/BottomPanel.*` | Bottom notebook (Summary, Strategy, Plan, Progress, Log), workflow buttons, the single status line and gauge. |
| `src/ui/panels/SummaryPage.*` | Summary tab: volume figures and per-class table. |
| `src/ui/panels/StrategyPage.*` | Strategy tab: preset picker, effort level, zone list. |
| `src/ui/panels/PlanPage.*` | Plan tab: Fast / Moderate / Maximum side by side, chosen level, warnings. |
| `src/ui/panels/DrivesPanel.*` | All-disks panel: `wxDataViewCtrl` over a virtual model, space bar and TRIM button renderers, selection callback. |
| `src/ui/panels/MapPlaceholder.*` | Map toolbar (disabled; shows the color mode) and a focusable canvas with a made-up pattern; map keys become View commands. |
| `src/cli/main.cpp` | `icd-cli`: `--version`, `--help`. |
| `tests/CMakeLists.txt` | Fetches pinned GoogleTest, defines `icd_tests`, `gtest_discover_tests`. |
| `tests/core/SnapshotTest.cpp` | Snapshot building, LCN and file lookup, split runs, paths, bad input, free runs, order independence. |
| `tests/core/StripesTest.cpp` | Stripe layout coverage, view coverage on random volumes, parallel = serial on 2-16 threads. |
| `tests/core/SmokeTest.cpp` | `formatBytes()` and `formatCount()` tests. |
| `tests/core/JobTypesTest.cpp` | Cancellation propagation, progress throttling, pause gate, stage clock. |
| `tests/services/WorkflowTest.cpp` | Every workflow transition, Quick, cancel and failure paths, disallowed actions. |
| `tests/core/LogTest.cpp` | Log line format, escaping, level filter, concurrent writes. |
| `tests/platform/LogSinksTest.cpp` | File sink writing, rotation and reopen in a temp directory. |
| `tests/services/JobRunnerTest.cpp` | Job outcomes, cancel, destructor wait, progress throttling, fake job. |
| `tests/services/ThreadPoolTest.cpp` | Identical reductions for 1..N threads, coverage, errors, fast cancel, nested loops. |
| `tests/core/DriveInfoTest.cpp` | Drive model rules, column texts, badges, durations and ages. |
| `tests/services/FakeDriveProviderTest.cpp` | The fake list covers every row style. |
| `tests/core/ResourceCapsTest.cpp` | `computeCaps()` edge cases (1 CPU, 2 GB, 128 threads, manual values) and `describeCaps()`. |
| `tests/platform/SystemResourcesTest.cpp` | Detection plausibility and the low-memory signal. |

## Tools

| Path | What it does |
| --- | --- |
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
