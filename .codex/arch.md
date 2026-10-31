# Architecture Notes

How the code is layered and where responsibilities live. The full design is in `plan.md` (section 5);
this file records what is actually built and the decisions made along the way.

## Layers (target, from `plan.md` section 5)

- `src/core` - wx-free, Win32-free C++23 model and algorithms.
- `src/platform/win` - narrow Win32 wrappers.
- `src/services` - jobs, thread pool, orchestration.
- `src/ui` - wxWidgets windows and widgets.
- `src/cli` - `icd-cli` console front end.

## Build

- Compiler: clang-cl (LLVM) with lld-link and Ninja Multi-Config is the main build; it uses the MSVC
  STL and Windows SDK from vcvars64 and links the prebuilt MSVC wx DLLs (same ABI). MSVC still builds
  (`ICD_TOOLCHAIN=msvc`), so MSVC-only flags (`/permissive-`, `/Zc:*`, `/MP`) are applied only there.
- C++23, warnings as errors, only Debug and Release configurations.
- Dependency direction: `ui`/`cli` -> `services` -> `platform` -> `core`. `core` has no wx or Win32
  includes; `platform` is the only place that includes `<windows.h>` for system calls.
- wx is linked through imported targets, not wx's `#pragma comment(lib)` auto-linking (only
  `include/`, not `include/msvc/`, is on the include path). This lets the post-build step copy exactly
  the runtime DLLs of the active configuration with `$<TARGET_RUNTIME_DLLS>`.
- The version comes from `project(VERSION)`: CMake generates `core/Version.h` (plain macros) that
  both C++ code and `res/IroncladDefrag.rc` include.
- The GUI always runs elevated (`requireAdministrator` in `res/IroncladDefrag.manifest`); the linker's
  own UAC entry is off (`/MANIFESTUAC:NO`) so there is exactly one. `icd-cli` stays `asInvoker` so it
  works in scripts; commands that need administrator rights will check and report it.
- Automated smoke runs start the GUI with `__COMPAT_LAYER=RunAsInvoker` to skip the UAC prompt.

## Logging

- `core::logging` holds the record model, formatting and a thread-safe `Logger` (one mutex; lines are
  formatted outside it, sinks are called under it so lines never interleave). A process-wide
  `globalLogger()` keeps call sites short: `logging::info("scan done", {{"files", n}})`.
- Sinks live in `platform`: a rotating file in `%LOCALAPPDATA%\IroncladDefrag\logs\<exe>.log`
  (8 MB, 5 old files, flushed per line so a crash keeps the tail) and `OutputDebugStringW`.
- `services::initAppLogging()` wires both for each executable; a file that cannot be opened only
  produces a warning, never a failure.

## Jobs: cancellation and progress

- Cancellation is cooperative: work polls a `CancelToken` (a `std::stop_token`) between work items or
  calls `throwIfCancelled()`. A `CancelSource` built from a parent token is cancelled with it, so one
  Cancel button stops a job and all of its sub-jobs.
- Progress is a value (`stage`, `done`, `total`, `message`) pushed to a `ProgressSink`.
  `ThrottledProgress` sits in front of the UI: about 10 reports per second, but the first report,
  stage changes and finished stages always pass, and `flush()` delivers the newest held-back one.
  Within a stage (same name and total) progress only moves forward: workers compute and report counts
  concurrently, so a lower count arriving late is dropped.

## Jobs and the UI

- `services::AppServices` is the composition root (created by `App` after logging, destroyed in
  `OnExit` before the shutdown line): caps, then the `ThreadPool`, then the `JobRunner`. The runner is
  destroyed first, cancelling and waiting for jobs while the pool still exists.
- A job is a function of `JobContext` (token, throttled progress sink, executor). The runner runs it
  as a pool task, logs start and end, and reports exactly one `JobResult` (Succeeded, Cancelled when
  the token was cancelled, Failed with the exception text).
- `ui::JobEventBridge` implements `JobObserver` and turns callbacks into `wxThreadEvent`s with
  payloads, queued with `wxQueueEvent`. A window detaches its bridge before it is destroyed, so
  events from still-running jobs are dropped. Event types are function-local statics (created on
  first use), not `wxDEFINE_EVENT` globals.
- Command enable state is set explicitly when job state changes: wxMSW refreshes `wxEVT_UPDATE_UI` for
  menus only when a menu opens, and it ignores accelerators of disabled items.

## Main window

- Top to bottom: menu, all-disks panel, then a splitter with the map above and the bottom panel below.
  The splitter gives window growth to the map (sash gravity 1); the bottom panel keeps its height.
- Status appears in one place only: the line under the bottom notebook, next to the workflow buttons
  and a gauge. There is no status bar; the Progress tab holds details (stage, counts, resources), not
  a second status text.
- One `CommandId` per command, shared by menu items, bottom-panel buttons and (later) accelerators.
  Buttons carry the command id as their window id, so `wxEVT_MENU` and `wxEVT_BUTTON` go to the same
  handler and `enableCommand()` enables both.
- Drives come from `services::IDriveProvider` (owned by `AppServices`; fake in Phase 1). The model is
  `core::DriveInfo`; the column texts are core functions (`core/format/DriveText`), so the wording is
  tested and the CLI can reuse it. `ui::DrivesPanel` shows them in a `wxDataViewCtrl` (selection,
  keyboard and column resizing come free); two custom renderers draw the space bar and the SSD-only
  TRIM button. A TRIM click selects its row and queues `ID_TRIM` up to the frame.
- Shortcuts: one table in `ui/Accelerators` puts them on the menu items; wxMSW builds the frame's
  accelerator table from the menu and skips disabled items, so keys follow the same enable rules.
  Workflow keys are F5 (Analyze), Shift+F5 (Quick), F6-F9 (Strategy, Build plan, Review, Execute),
  Pause and Esc (only enabled while a job runs). The plain map keys of plan.md 7.4 (+, -, 0, 1,
  arrows, PgUp/PgDn, Home/End) act only while the map has focus, so they never steal typing; zoom
  keys post the same View commands as the menu, panning is reserved for the map engine.
- View commands are stubs until Phase 2: enabled once the map shows a pattern, they update the
  toolbar color mode and the status line.
- UI numbers use `core::formatCount()` rather than the C locale, which would also change printf output.

## Workflow

- `services::Workflow` is the state machine of plan.md section 11 (NoDrive, Unsupported, SsdReady,
  Trimming, NotAnalyzed, Analyzing, Analyzed, Planning, Reviewing, Executing, Paused, Executed). Each
  action has one rule: the states where it is allowed, the next state and the job to start. It runs
  nothing itself, so all transitions are unit-tested; `finish(outcome)` returns a follow-up job (the
  plan of a Quick run, which always stops at Reviewing).
- `ui::WorkflowBinding` maps commands to actions, starts the job a step needs, feeds job ends back
  and enables every command from `Workflow::can()`. The drive list is locked while a job runs, and
  the selected drive is copied so a refresh cannot change it mid-job. Execute asks for confirmation
  first. In Phase 1 every step is the fake parallel job with step-specific stage names; results come
  from `services/workflow/FakeResults`.
- Pause is a `core::PauseGate` that the execution job checks between items; Cancel still works
  while paused because the gate's wait returns on cancellation.

## Parallel execution

- Core algorithms take a `core::Executor&` and never create threads. Services owns one
  `ThreadPool` sized from `ResourceCaps::workerThreads`; tests use `InlineExecutor` as the reference.
- `parallelFor(ChunkedRange{count, grain}, body)` cuts the range by `grain` only, so the work split
  never depends on the thread count; `parallelReduce` combines per-chunk results in chunk order. This
  is how parallel runs give bit-identical results to serial ones (plan.md 6.2.8).
- The pool: one deque per worker (owner takes the newest, thieves the oldest), a condition variable
  for idle workers, `std::jthread` stop tokens for shutdown. The thread calling `parallelFor` claims
  chunks too, so nested loops from pool tasks cannot deadlock. Cancellation stops new chunks; running
  chunks finish, then `OperationCancelled` is thrown. Pending tasks are dropped when the pool is
  destroyed, so job owners cancel and wait first.

## Resources

- `platform::detectSystemResources()` reads what this process may use (processors over all groups,
  narrowed by a single-group affinity mask and a hard job-object CPU rate; RAM narrowed by job memory
  limits). `core::computeCaps()` turns that plus the user's settings into the worker count and memory
  budget, each with a `CapReason` for the UI. Caps change speed only, never results (plan.md 6.2.8).
- Auto: workers = min(32, processors - 1, >= 1); memory = min(8 GB, 60% of available RAM). Manual
  values are capped to what was detected. The startup log records both.

## Quality gate

- Lizard and clang-tidy, pinned in a Python venv, so results do not depend on the installed LLVM.
- clang-tidy uses its own Ninja + clang-cl configure in `build/quality` with precompiled headers off:
  the pinned clang-tidy (22) cannot read `.pch` files from the installed clang-cl (23). It runs in cl
  driver mode inside the vcvars64 environment, which supplies the STL and SDK include paths.

## Third-party

- `tools/dev-env.cmd` clears `PSModulePath`: a cmd started from PowerShell 7 passes its module path
  on, and Windows PowerShell then fails to load cmdlets (`fetch-wx.ps1` also hashes with .NET).
- wxWidgets is used as prebuilt DLLs only (no source build). `tools/fetch-wx.ps1` is the only way the
  tree under `3rdparty/wxWidgets` is produced, so every machine builds against the same pinned files.
