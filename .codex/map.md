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
Also loads/saves persisted profile and safety settings, stores recent non-executable analysis summaries, and reclassifies cached snapshots with current safety settings before planning.

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

Builds read-only placement intent from completed analysis snapshots and the active optimization profile, with optional coarse progress reporting for combined plan builds.

- `src/optimization/MovePlanner.h`
- `src/optimization/MovePlanner.cpp`

Builds conservative dry-run move plans from analysis snapshots and placement intent, with simulated destination reservations and optional coarse progress reporting only.

## Persistence Layer

- `src/persistence/AppSettingsSerializer.h`
- `src/persistence/AppSettingsSerializer.cpp`

Serializes/deserializes versioned application settings text containing active profile mode, profile collection, global safety settings, and recent analysis summaries. It does not persist executable analysis snapshots.

- `src/persistence/AppSettingsStore.h`
- `src/persistence/AppSettingsStore.cpp`

Loads and atomically saves settings at `%LOCALAPPDATA%\IroncladDefrag\settings.txt`.

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

Defines `icd::MainFrame`, the top-level `wxFrame`. Owns menu bar creation, Exit/About handlers, the status bar, all-disks panel wiring, analysis document tabs, plan-build progress popup, workflow panel wiring, and shared command paths for analysis, combined planning, fast-lane actions, review, and execution.

- `src/ui/DriveListPanel.h`
- `src/ui/DriveListPanel.cpp`

Defines the top all-disks panel with drive status, capacity/free space, file-system, analysis/move/TRIM badges, refresh, selection, and analyse callbacks.

- `src/ui/DriveAnalysisPage.h`
- `src/ui/DriveAnalysisPage.cpp`

Defines an analysis-result document page shown in the main notebook, with the drive map, render-mode controls, class filter, planned-move outline toggle, and map legend. Text summary, plan, warning/skipped-file, and execution views are owned by the bottom workflow panel.

- `src/ui/DriveMapPanel.h`
- `src/ui/DriveMapPanel.cpp`

Defines the cluster-grid drive map. It renders fixed-size cells, recalculates clusters-per-box on resize, switches between actual analysis/classification colors, intended-placement colors, and planned-move overlays, and supports file-class filters.

- `src/ui/ProfileSettingsDialog.h`
- `src/ui/ProfileSettingsDialog.cpp`

Defines the modal editor for selecting profiles and changing core optimization settings.

- `src/ui/SafetySettingsDialog.h`
- `src/ui/SafetySettingsDialog.cpp`

Defines the modal editor for global safety guardrails, directory/extension/size exclusions, optional dry-run-only behavior, and global moved-data caps.

- `src/ui/MovePlanDialog.h`
- `src/ui/MovePlanDialog.cpp`

Defines the inspection dialog for dry-run move plans, skipped candidates, issues, cancellation boundaries, and rollback notes.

- `src/ui/UIFormatting.h`
- `src/ui/UIFormatting.cpp`

Defines UI-only formatting helpers for byte quantities, drive kinds, capability badges, and profile mode labels.

- `src/ui/WorkflowPanel.h`
- `src/ui/WorkflowPanel.cpp`

Defines the bottom Phase 7 workflow panel with stage status, profile selection, analysis/plan/execution summaries, step-by-step commands, and quick/full fast-lane commands.

## Model Layer

- `src/model/Units.h`

Defines `icd::Quantity` and project-specific type aliases for counts, indexes, byte counts, sector counts, and MB/s throughput.

- `src/model/DomainTypes.h`

Defines value types for drive/volume metadata, drive capabilities, disk zones, file classes, classification results/summaries, optimization settings/profiles, analysis results/stats, placement/move plans, plan-build results, execution results, and job progress.
Also defines Phase 8 safety settings, size exclusion ranges, and recent analysis summaries.

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
