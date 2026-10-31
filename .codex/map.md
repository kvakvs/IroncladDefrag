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
| `src/core/exec/Executor.*` | `ChunkedRange`, `Executor` interface (`submit`, `parallelFor`), `InlineExecutor`, deterministic `parallelReduce`. |
| `src/services/exec/ThreadPool.*` | Work-stealing `ThreadPool` (per-worker deques, caller joins `parallelFor`, cancel-aware). |
| `src/core/job/CancelToken.*` | `CancelSource`/`CancelToken` over `std::stop_source`, parent-linked sources, `OperationCancelled`. |
| `src/core/job/Progress.*` | `Progress` snapshot, `ProgressSink`, `ThrottledProgress` (~10 Hz, injectable clock). |
| `src/core/log/Log.h` | `logging::Level`, `Field`, `Record`, `Sink`, thread-safe `Logger`, `globalLogger()`, `info()`/`error()`/... helpers. |
| `src/core/log/LogFormat.cpp` | `formatRecord()`: one-line `key=value` records, UTC time, escaping. |
| `src/core/log/Logger.cpp` | `Logger` and the global helpers. |
| `src/core/resources/ResourceCaps.*` | `computeCaps()` (plan.md 6.6 worker and memory caps with reasons), `describeCaps()`. |
| `src/platform/win/SystemResources.*` | `detectSystemResources()` (groups, affinity, job CPU/memory limits, cores, RAM), `LowMemorySignal`. |
| `src/platform/win/WinError.*` | `formatWinError()`, `lastWinError()`: Win32 error text. |
| `src/platform/win/LogSinks.*` | `FileSink` (rotating `<name>.log`, flushed per line), `DebugOutputSink`, `defaultLogDirectory()`. |
| `src/services/AppInfo.*` | Product name and version (from `core/Version.h`) for GUI and CLI. |
| `src/services/AppServices.*` | Service composition root: resources, caps, `ThreadPool`, `JobRunner` (destroyed in the right order). |
| `src/services/job/JobRunner.*` | `JobRunner`, `JobContext`, `JobObserver`, `JobResult`: jobs on the pool with cancel, throttled progress, one result. |
| `src/services/job/FakeJob.*` | `makeFakeParallelJob()`: serial + parallel stages for the Debug menu and runner tests. |
| `src/ui/bridge/JobEvents.*` | `JobEventBridge` (job callbacks to `wxQueueEvent`), `jobProgressEvent()`, `jobFinishedEvent()`, payloads. |
| `src/services/AppLogging.*` | `initAppLogging()`/`shutdownAppLogging()`: installs the sinks, writes startup/shutdown lines. |
| `src/precompiled.h` | GUI PCH: `<wx/wx.h>` and std headers only. |
| `src/main.cpp` | `wxIMPLEMENT_APP(icd::App)`. |
| `src/ui/App.*` | `icd::App`: starts logging and `AppServices`, creates the main frame, stops services on exit. |
| `src/ui/MainFrame.*` | Main window: File and Debug menus (fake job run/cancel), two-field status bar with job progress. |
| `src/cli/main.cpp` | `icd-cli`: `--version`, `--help`. |
| `tests/CMakeLists.txt` | Fetches pinned GoogleTest, defines `icd_tests`, `gtest_discover_tests`. |
| `tests/core/SmokeTest.cpp` | `formatBytes()` tests. |
| `tests/core/JobTypesTest.cpp` | Cancellation propagation and progress throttling. |
| `tests/core/LogTest.cpp` | Log line format, escaping, level filter, concurrent writes. |
| `tests/platform/LogSinksTest.cpp` | File sink writing, rotation and reopen in a temp directory. |
| `tests/services/JobRunnerTest.cpp` | Job outcomes, cancel, destructor wait, progress throttling, fake job. |
| `tests/services/ThreadPoolTest.cpp` | Identical reductions for 1..N threads, coverage, errors, fast cancel, nested loops. |
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
