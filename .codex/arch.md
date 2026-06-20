# IroncladDefrag Architecture

## Application Shape

IroncladDefrag is a Windows x64 desktop GUI application written in C++20. The current implementation is a wxWidgets-based shell with read-only drive discovery and analysis. The repository separates GUI bootstrap/window code, controller orchestration, analysis services, Windows platform-boundary code, and domain model types. File-move planning, move execution, and strategy-selection services are not implemented yet.

The app is built as a Windows subsystem executable with CMake and MSVC-oriented wxWidgets libraries vendored under `3rdparty/wxWidgets`.

## Runtime Flow

Startup currently follows this path:

1. `src/main.cpp` defines `wWinMain`, disables wx debug support, and calls `wxEntry()`.
2. `wxIMPLEMENT_APP(icd::App)` registers the application class.
3. `icd::App::OnInit()` creates and shows `icd::MainFrame`.
4. `icd::MainFrame` creates the top menu, status bar, and tabbed analysis document area. The Analysis menu refreshes visible drives, starts read-only analysis for enabled drives, and can request cancellation.

There is now a controller, background worker, drive enumerator, read-only drive analysis service, and deterministic data-drive classification service. There is still no defragmentation executor, move planner, strategy implementation, TRIM action, or file movement implementation.

## Layers

## Entry Point

`src/main.cpp` owns process startup and wxWidgets application registration. It should stay thin and avoid application logic.

## UI Layer

`src/ui/App.*` defines the wx application lifecycle. It is responsible for initializing the GUI and creating the main frame.

`src/ui/MainFrame.*` defines the top-level window. It owns menu setup, Exit/About handling, status text, drive-analysis menu entries, and the `wxNotebook` document surface.

`src/ui/DriveAnalysisPage.*` displays a completed `AnalysisResult` as summary labels plus a visible drive-map TODO placeholder.

The UI layer may use wxWidgets types directly. Long-running drive analysis, file layout scanning, and file movement must not run on the UI thread; those operations should be delegated to worker/service code and reported back through wx-safe event dispatch.

## Application Controller Layer

`src/app/ApplicationController.*` owns orchestration for drive enumeration, selected-drive read-only analysis, stored in-memory analysis snapshots, and progress/completion/error callbacks for the UI.

`src/app/BackgroundJob.*` provides a small `std::thread` worker wrapper with cooperative cancellation and destructor-time joining. It is infrastructure for later real analysis and movement jobs.

## Analysis Layer

`src/analysis/DriveAnalysisService.*` performs read-only file metadata scanning, per-file extent lookup, free-space bitmap lookup, and analysis metric construction for a selected drive.

`src/analysis/FakeAnalysisService.*` remains a synthetic analysis helper for development. It deliberately performs no drive enumeration, cluster scanning, or disk writes.

## Classification Layer

`src/classification/FileClassifier.*` classifies analysed files by size, broad type, recency, directory hints, fragmentation benefit, move-safety status, and expected placement zone. Classification is deterministic, in-memory, and independent from wxWidgets and Win32 APIs.

## Windows Platform Boundary

`src/platform/windows/DriveEnumerator.*` enumerates visible drives, volume metadata, media/capability status, and disabled reasons using read-only Win32 calls.

`src/platform/windows/VolumeQueries.*` contains read-only retrieval-pointer and volume-bitmap FSCTL calls.

`src/platform/windows/UniqueHandle.*` provides RAII for Win32 handles.

## Model Layer

`src/model` contains domain data structures and unit types:

- `Units.h` provides type-safe quantity wrappers for counts, indexes, byte counts, sector counts, and throughput.
- `DomainTypes.h` defines domain value types for drives, volumes, disk zones, file classes, classification results/summaries, optimization profiles/settings, analysis results, placement plans, move plans, job progress, drive capabilities, and analysis statistics.
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

- Privileged or write-capable drive operations.
- Move strategy interfaces and implementations.
- Move planner that minimizes moved data.
- Defragmentation/move executor with cancellation, progress reporting, and error handling.
- Tests or validation harnesses.

## Expected Direction

As the implementation grows, keep responsibilities separated:

- UI: wxWidgets controls, user commands, progress display, and event handling.
- Application/controller layer: command orchestration, current selected drive, selected strategy, job lifecycle, cancellation, and UI-facing view models.
- Analysis services: read-only disk/file/free-space analysis and model construction.
- Strategy services: produce move plans from model data and user-selected goals.
- Execution services: perform privileged Windows file movement operations, report progress, and preserve safety invariants.
- Model: stable value types and domain data structures that do not depend on GUI code.
