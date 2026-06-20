# Project Map

## Root Files

- `AGENTS.md` - persistent agent instructions for this repository.
- `.codex/arch.md` - detected architecture and intended layering.
- `.codex/map.md` - this repository map.
- `.clang-format` - C++ formatting rules based on LLVM style with 4-space indentation and 120-column limit.
- `.gitignore` - ignored files.
- `CMakeLists.txt` - single executable target, wxWidgets include/library paths, compile definitions, precompiled header setup, and post-build DLL copying.
- `cmake-build.bat` - configures Visual Studio 2026 x64 files into `build/` and builds the selected configuration.

## Source Root

- `src/main.cpp` - Windows GUI process entry point and wxWidgets application registration.
- `src/precompiled.h` - precompiled header with Windows, wxWidgets, and common standard-library includes.
- `src/precompiled.cpp` - precompiled-header translation unit.

## Application Controller Layer

- `src/app/ApplicationController.h`
- `src/app/ApplicationController.cpp`

Owns drive enumeration, selected-drive read-only analysis orchestration, in-memory analysis snapshots, move-plan execution orchestration, progress/completion/error callbacks, and cancellation.

- `src/app/BackgroundJob.h`
- `src/app/BackgroundJob.cpp`

Small `std::thread` worker wrapper with cooperative cancellation and destructor-time joining.

## Analysis Layer

- `src/analysis/DriveAnalysisService.h`
- `src/analysis/DriveAnalysisService.cpp`

Runs read-only analysis for a selected drive: recursive file metadata scan, retrieval-pointer extent collection, free-space bitmap collection, and summary metrics.

- `src/analysis/FakeAnalysisService.h`
- `src/analysis/FakeAnalysisService.cpp`

Simulates long-running analysis and returns a synthetic `AnalysisResult`. It performs no real drive I/O.

## Classification Layer

- `src/classification/FileClassifier.h`
- `src/classification/FileClassifier.cpp`

Classifies completed analysis snapshots by size, broad file type, recency, directory hints, fragmentation benefit, safety status, and expected placement zone.

## Optimization Layer

- `src/optimization/ProfileCatalog.h`
- `src/optimization/ProfileCatalog.cpp`

Creates the built-in Phase 4 optimization profiles.

- `src/optimization/OptimizationSettingsSerializer.h`
- `src/optimization/OptimizationSettingsSerializer.cpp`

Serializes/deserializes optimization profiles as deterministic text for future persistence, with no file I/O.

- `src/optimization/PlacementPlanner.h`
- `src/optimization/PlacementPlanner.cpp`

Builds read-only placement intent from completed analysis snapshots and the active optimization profile.

- `src/optimization/MovePlanner.h`
- `src/optimization/MovePlanner.cpp`

Builds conservative dry-run move plans from analysis snapshots and placement intent, with simulated destination reservations only.

## Execution Layer

- `src/execution/MoveExecutor.h`
- `src/execution/MoveExecutor.cpp`

Runs the bounded Phase 6 execution pass for existing move plans. It rejects dry-run-only/impossible/empty plans, revalidates files before movement, calls the Windows move boundary, verifies extents afterward, reports progress, and records per-file execution results.

## UI Layer

- `src/ui/App.h`
- `src/ui/App.cpp`

Defines `icd::App`, the `wxApp` subclass that initializes wxWidgets and shows the main frame.

- `src/ui/MainFrame.h`
- `src/ui/MainFrame.cpp`

Defines `icd::MainFrame`, the top-level `wxFrame`. Currently owns menu bar creation, Exit/About handlers, and the status bar.

- `src/ui/DriveAnalysisPage.h`
- `src/ui/DriveAnalysisPage.cpp`

Defines an analysis-result document page shown in the main notebook, with a vertical splitter containing the drive map above scrollable summary/classification and placement-intent labels.

- `src/ui/DriveMapPanel.h`
- `src/ui/DriveMapPanel.cpp`

Defines the read-only cluster-grid drive map. It renders fixed-size cells, recalculates clusters-per-box on resize, and can switch between actual analysis/classification colors and intended-placement colors from an in-memory `PlacementPlan`.

- `src/ui/ProfileSettingsDialog.h`
- `src/ui/ProfileSettingsDialog.cpp`

Defines the modal in-memory editor for selecting profiles and changing core optimization settings.

- `src/ui/MovePlanDialog.h`
- `src/ui/MovePlanDialog.cpp`

Defines the inspection dialog for dry-run move plans, skipped candidates, issues, cancellation boundaries, and rollback notes.

## Model Layer

- `src/model/Units.h`

Defines `icd::Quantity` and project-specific type aliases for counts, indexes, byte counts, sector counts, and MB/s throughput.

- `src/model/DomainTypes.h`

Defines value types for drive/volume metadata, drive capabilities, disk zones, file classes, classification results/summaries, optimization settings/profiles, analysis results/stats, placement/move plans, execution results, and job progress.

- `src/model/FileMetadata.h`
- `src/model/FileMetadata.cpp`

Defines `icd::FileMetadata`, file type classification, file size/path/timestamps, parent directory, and cluster fragment locations.

- `src/model/FragmentMap.h`
- `src/model/FragmentMap.cpp`

Defines `icd::FragmentMap`, file fragment sector ranges, total fragment count, and contiguous-file state.

- `src/model/FreeSpaceMap.h`
- `src/model/FreeSpaceMap.cpp`

Defines `icd::FreeSpaceMap`, free-space sector blocks, total free space, fragmentation count, and fragmentation ratio.

- `src/model/DiskGeometry.h`
- `src/model/DiskGeometry.cpp`

Defines `icd::DiskGeometry`, basic disk geometry, zone layout, and performance zones.

## Windows Platform Boundary

- `src/platform/windows/UniqueHandle.h`
- `src/platform/windows/UniqueHandle.cpp`

RAII wrapper for Win32 handles.

- `src/platform/windows/DriveEnumerator.h`
- `src/platform/windows/DriveEnumerator.cpp`

Enumerates visible drives and read-only volume/media/capability status. It keeps raw volume bitmap access optional so fixed mechanical/unknown drives can still run metadata-only analysis.

- `src/platform/windows/VolumeQueries.h`
- `src/platform/windows/VolumeQueries.cpp`

Queries file extents and volume free-space bitmap through read-only FSCTL calls.

- `src/platform/windows/VolumeMoveOperations.h`
- `src/platform/windows/VolumeMoveOperations.cpp`

Probes elevation/manage-volume capability, requests UAC relaunch through `ShellExecuteW` with `runas`, opens write-capable volume/file handles, and wraps `FSCTL_MOVE_FILE`.

## Support Layer

- `src/support/Logger.h`
- `src/support/Logger.cpp`

Minimal diagnostic logging to Windows debug output and standard wide error output.

## Vendored Dependencies

- `3rdparty/wxWidgets` - vendored wxWidgets 3.3.1 headers, MSVC setup headers, libraries, and DLLs.
- `3rdparty/*.7z`
- `3rdparty/*.zip`

Stored wxWidgets source/header/library archives.

## Build Output

- `cmake-build-debug` - existing local debug build directory. Treat as generated output unless the user explicitly asks about it.
- `build` - Visual Studio build directory used by `cmake-build.bat`. Treat as generated output unless the user explicitly asks about it.
