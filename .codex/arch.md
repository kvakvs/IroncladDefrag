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

- wxWidgets is used as prebuilt DLLs only (no source build). `tools/fetch-wx.ps1` is the only way the
  tree under `3rdparty/wxWidgets` is produced, so every machine builds against the same pinned files.
