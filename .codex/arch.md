# IroncladDefrag Architecture

## Application Shape

IroncladDefrag is a Windows x64 desktop GUI application written in C++20. The current implementation is a wxWidgets-based shell for a disk analyser and defragmenter. The repository already separates GUI bootstrap/window code from domain model types, but the drive-analysis, file-move planning, move execution, and strategy-selection services are not implemented yet.

The app is built as a Windows subsystem executable with CMake and MSVC-oriented wxWidgets libraries vendored under `3rdparty/wxWidgets`.

## Runtime Flow

Startup currently follows this path:

1. `src/main.cpp` defines `wWinMain`, disables wx debug support, and calls `wxEntry()`.
2. `wxIMPLEMENT_APP(icd::App)` registers the application class.
3. `icd::App::OnInit()` creates and shows `icd::MainFrame`.
4. `icd::MainFrame` creates the top menu and status bar.

There is no current background worker, disk scanner, controller, application state object, or defragmentation executor.

## Layers

## Entry Point

`src/main.cpp` owns process startup and wxWidgets application registration. It should stay thin and avoid application logic.

## UI Layer

`src/ui/App.*` defines the wx application lifecycle. It is responsible for initializing the GUI and creating the main frame.

`src/ui/MainFrame.*` defines the top-level window. It currently contains menu setup, Exit handling, About handling, and a status bar. Future all-disks panels, drive map panels, strategy controls, progress UI, and status displays should be composed below this layer.

The UI layer may use wxWidgets types directly. Long-running drive analysis, file layout scanning, and file movement must not run on the UI thread; those operations should be delegated to worker/service code and reported back through wx-safe event dispatch.

## Model Layer

`src/model` contains domain data structures and unit types:

- `Units.h` provides type-safe quantity wrappers for counts, indexes, byte counts, sector counts, and throughput.
- `FileMetadata.*` models file path, size, timestamps, type, parent directory, and cluster fragment locations.
- `FragmentMap.*` models file fragmentation using sector ranges and aggregate fragment state.
- `FreeSpaceMap.*` models free-space blocks and free-space fragmentation metrics.
- `DiskGeometry.*` models disk geometry, zones, performance zones, and performance characteristics.

The current model classes are mostly private storage with no public behavior. They are suitable foundations for later analyzer and strategy services, but callers cannot yet construct, query, or transform most of the model state.

Model code should remain independent from wxWidgets UI concerns. Prefer standard C++ types and narrow Windows-specific abstractions at system-boundary modules.

## Build And Dependencies

`CMakeLists.txt` defines the single `IroncladDefrag` executable and lists all current source files explicitly. It enables C++20, precompiled headers, `/MP`, Windows subsystem output, MSVC exception handling, Unicode definitions, and wxWidgets DLL usage.

wxWidgets 3.3.1 headers, libraries, DLLs, and archives are vendored in `3rdparty/wxWidgets` and `3rdparty`. The build links debug and optimized wx libraries. The current post-build copy loop copies the debug wxWidgets DLL list; release DLL names are defined but are not currently copied by the active loop.

## Precompiled Header

`src/precompiled.h` centralizes Windows, wxWidgets, and common standard-library includes. `src/precompiled.cpp` exists solely to build the precompiled header. Keep high-churn project headers out of the precompiled header.

## Current Gaps

The following architecture pieces are implied by the product goals but are not present yet:

- Drive enumeration and disk capability detection.
- File-system scanner and cluster-map reader.
- Free-space scanner.
- Fragmentation analysis service.
- Move strategy interfaces and implementations.
- Move planner that minimizes moved data.
- Defragmentation/move executor with cancellation, progress reporting, and error handling.
- Threading/job model for keeping the GUI responsive.
- Tests or validation harnesses.

## Expected Direction

As the implementation grows, keep responsibilities separated:

- UI: wxWidgets controls, user commands, progress display, and event handling.
- Application/controller layer: command orchestration, current selected drive, selected strategy, job lifecycle, cancellation, and UI-facing view models.
- Analysis services: read-only disk/file/free-space analysis and model construction.
- Strategy services: produce move plans from model data and user-selected goals.
- Execution services: perform privileged Windows file movement operations, report progress, and preserve safety invariants.
- Model: stable value types and domain data structures that do not depend on GUI code.
