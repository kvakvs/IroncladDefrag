# IroncladDefrag Architecture

## Application Shape

IroncladDefrag is a Windows x64 desktop GUI application written in C++20. The current implementation is a wxWidgets-based workflow with read-only drive discovery and analysis, dry-run move planning, visible Phase 7 drive-to-plan-to-execute controls, persisted Phase 8 profile/safety settings, recent non-executable analysis summaries, and a bounded Phase 6 move executor. The repository separates GUI bootstrap/window code, controller orchestration, analysis services, persistence services, execution services, Windows platform-boundary code, and domain model types. TRIM actions are not implemented yet.

The app is built as a Windows subsystem executable with CMake and MSVC-oriented wxWidgets libraries vendored under `3rdparty/wxWidgets`.

## Runtime Flow

Startup currently follows this path:

1. `src/main.cpp` defines `wWinMain`, disables wx debug support, and calls `wxEntry()`.
2. `wxIMPLEMENT_APP(icd::App)` registers the application class.
3. `icd::App::OnInit()` creates and shows `icd::MainFrame`.
4. `icd::MainFrame` creates the top menu, status bar, all-disks panel, tabbed analysis document area, and bottom workflow panel. The Analysis menu and visible controls refresh visible drives, start read-only analysis for enabled drives, and can request cancellation.

There is now a controller, background worker, drive enumerator, read-only drive analysis service, deterministic data-drive classification service with safety exclusions, persisted optimization/safety settings, dry-run optimization profile/placement-intent layer, dry-run move planning, conservative move-plan execution path, visible staged workflow controls, and planned-move map overlays. There is still no TRIM action or advanced resumable pause support.

## Layers

## Entry Point

`src/main.cpp` owns process startup and wxWidgets application registration. It should stay thin and avoid application logic.

## UI Layer

`src/ui/App.*` defines the wx application lifecycle. It is responsible for initializing the GUI and creating the main frame.

`src/ui/MainFrame.*` defines the top-level window. It owns menu setup, Exit/About handling, status text, drive-analysis menu entries, the all-disks panel, the `wxNotebook` document surface, and the bottom workflow panel wiring.

`src/ui/DriveListPanel.*` displays discovered drives with capacity, file-system, capability, move, and TRIM badges. It forwards refresh, selection, and analyse requests to `MainFrame`.

`src/ui/WorkflowPanel.*` displays the selected-drive stage, profile selector, analysis summary, plan preview, warnings/skipped candidates, execution summary, step-by-step commands, and fast-lane quick/full optimize commands.

`src/ui/DriveAnalysisPage.*` displays a completed `AnalysisResult` as a map-focused document page with render-mode, class-filter, planned-move outline controls, and map legend. Summary, plan, warning/skipped-file, and execution text live in the bottom workflow panel.

`src/ui/DriveMapPanel.*` renders the cluster visualization for an analysed drive. It consumes in-memory `AnalysisResult`, optional `PlacementPlan`, and optional `MovePlan` data, can switch between actual layout, intended-placement coloring, and planned-move overlays, supports file-class filters, derives a clusters-per-box scale from the current viewport, and repaints on resize without starting disk I/O, planning, or movement.

`src/ui/ProfileSettingsDialog.*` provides the modal profile editor for core optimization settings. Profile changes are persisted through the controller.

`src/ui/SafetySettingsDialog.*` provides the modal Phase 8 editor for global safety guardrails, directory/extension/size exclusions, optional dry-run-only behavior, and global moved-data caps.

`src/ui/MovePlanDialog.*` displays inspectable dry-run move plans, including operations, skipped candidates, issues, cancellation boundaries, and rollback notes. Execution is exposed separately through the main Optimization menu and workflow panel.

`src/ui/UIFormatting.*` contains small UI-only formatting helpers for byte quantities, drive kinds, capability badges, and profile mode labels.

The UI layer may use wxWidgets types directly. Long-running drive analysis, file layout scanning, and file movement must not run on the UI thread; those operations should be delegated to worker/service code and reported back through wx-safe event dispatch. Phase 6 execution reports only through the status bar.

## Application Controller Layer

`src/app/ApplicationController.*` owns orchestration for drive enumeration, selected-drive read-only analysis, stored in-memory analysis snapshots, persisted profile/safety settings, recent analysis summaries, move-plan execution, and progress/completion/error callbacks for the UI.

`src/app/BackgroundJob.*` provides a small `std::thread` worker wrapper with cooperative cancellation and destructor-time joining. It is infrastructure for later real analysis and movement jobs.

## Analysis Layer

`src/analysis/DriveAnalysisService.*` performs read-only file metadata scanning, per-file extent lookup, free-space bitmap lookup, and analysis metric construction for a selected drive.

`src/analysis/FakeAnalysisService.*` remains a synthetic analysis helper for development. It deliberately performs no drive enumeration, cluster scanning, or disk writes.

## Classification Layer

`src/classification/FileClassifier.*` classifies analysed files by size, broad type, recency, directory hints, fragmentation benefit, global safety exclusions, move-safety status, and expected placement zone. Classification is deterministic, in-memory, and independent from wxWidgets and Win32 APIs.

## Optimization Layer

`src/optimization/ProfileCatalog.*` creates the built-in optimization profiles.

`src/optimization/OptimizationSettingsSerializer.*` provides deterministic string serialization for future settings persistence without performing file I/O.

`src/optimization/PlacementPlanner.*` produces dry-run placement intent from completed analysis/classification snapshots and the active profile. It does not create move plans or execute disk operations.

`src/optimization/MovePlanner.*` converts placement intent into conservative dry-run move plans using only in-memory analysis, free-space, and profile data. It simulates destination reservations and does not write to disk.

## Persistence Layer

`src/persistence/AppSettingsSerializer.*` serializes persisted application settings as deterministic versioned text: active profile mode, profile collection, global safety settings, and recent analysis summaries.

`src/persistence/AppSettingsStore.*` loads and atomically saves settings under `%LOCALAPPDATA%\IroncladDefrag\settings.txt`. It never persists executable analysis snapshots.

## Execution Layer

`src/execution/MoveExecutor.*` performs the bounded Phase 6 execution pass. It rejects dry-run-only, impossible, or empty plans; revalidates file size, attributes, extents, and drive ownership before each move; calls the Windows move boundary; verifies extents after each attempted move; and records per-file outcomes for audit and troubleshooting.

## Windows Platform Boundary

`src/platform/windows/DriveEnumerator.*` enumerates visible drives, volume metadata, media/capability status, and disabled reasons using read-only Win32 calls. Raw volume bitmap access is optional; eligible fixed drives can still run metadata-only analysis when bitmap access is unavailable.

`src/platform/windows/VolumeQueries.*` contains read-only retrieval-pointer and volume-bitmap FSCTL calls.

`src/platform/windows/VolumeMoveOperations.*` contains write-capable file movement and privilege boundary code. It probes administrator/manage-volume capability, can request a UAC relaunch through `ShellExecuteW` with `runas`, and wraps `FSCTL_MOVE_FILE`.

`src/platform/windows/UniqueHandle.*` provides RAII for Win32 handles.

## Model Layer

`src/model` contains domain data structures and unit types:

- `Units.h` provides type-safe quantity wrappers for counts, indexes, byte counts, sector counts, and throughput.
- `DomainTypes.h` defines domain value types for drives, volumes, disk zones, file classes, classification results/summaries, optimization profiles/settings, safety settings, recent analysis summaries, analysis results, placement plans, move plans, execution results, job progress, drive capabilities, and analysis statistics.
- `FileMetadata.*` models file path, size, timestamps, type, parent directory, and cluster fragment locations.
- `FragmentMap.*` models file fragmentation using sector ranges and aggregate fragment state.
- `FreeSpaceMap.*` models free-space blocks and free-space fragmentation metrics.
- `DiskGeometry.*` models disk geometry, zones, performance zones, and performance characteristics.

The model classes now expose constructors and read-only accessors so services can construct and query analysis data without wxWidgets dependencies.

Model code should remain independent from wxWidgets UI concerns. Prefer standard C++ types and narrow Windows-specific abstractions at system-boundary modules.

## Build And Dependencies

`CMakeLists.txt` defines the single `IroncladDefrag` executable and lists all current source files explicitly. It enables C++20, precompiled headers, `/MP`, Windows subsystem output, MSVC exception handling, Unicode definitions, and wxWidgets DLL usage.

wxWidgets 3.3.1 headers, libraries, DLLs, and archives are vendored in `3rdparty/wxWidgets` and `3rdparty`. The build links debug and optimized wx libraries. The current post-build copy loop copies the debug wxWidgets DLL list; release DLL names are defined but are not currently copied by the active loop.

## Precompiled Header

`src/precompiled.h` centralizes Windows, wxWidgets, and common standard-library includes. `src/precompiled.cpp` exists solely to build the precompiled header. Keep high-churn project headers out of the precompiled header.

## Support Layer

`src/support/Logger.*` provides minimal diagnostic logging through `OutputDebugStringW` and standard wide error output.

## Current Gaps

The following architecture pieces are implied by the product goals but are not present yet:

- Write-capable move strategy implementations.
- Advanced write-capable move strategy implementations beyond bounded whole-file moves from existing plans.
- Tests or validation harnesses.
- Interactive file selection on the drive map and detailed per-file inspection.

## Expected Direction

As the implementation grows, keep responsibilities separated:

- UI: wxWidgets controls, user commands, progress display, and event handling.
- Application/controller layer: command orchestration, current selected drive, selected strategy, job lifecycle, cancellation, and UI-facing view models.
- Analysis services: read-only disk/file/free-space analysis and model construction.
- Strategy services: produce move plans from model data and user-selected goals.
- Execution services: perform privileged Windows file movement operations, report progress, and preserve safety invariants.
- Model: stable value types and domain data structures that do not depend on GUI code.
