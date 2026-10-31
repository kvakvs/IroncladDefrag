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
