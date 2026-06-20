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

Owns drive enumeration, selected-drive read-only analysis orchestration, in-memory analysis snapshots, progress/completion/error callbacks, and cancellation.

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

## UI Layer

- `src/ui/App.h`
- `src/ui/App.cpp`

Defines `icd::App`, the `wxApp` subclass that initializes wxWidgets and shows the main frame.

- `src/ui/MainFrame.h`
- `src/ui/MainFrame.cpp`

Defines `icd::MainFrame`, the top-level `wxFrame`. Currently owns menu bar creation, Exit/About handlers, and the status bar.

- `src/ui/DriveAnalysisPage.h`
- `src/ui/DriveAnalysisPage.cpp`

Defines a simple analysis-result document page shown in the main notebook, with summary labels and a drive-map TODO placeholder.

## Model Layer

- `src/model/Units.h`

Defines `icd::Quantity` and project-specific type aliases for counts, indexes, byte counts, sector counts, and MB/s throughput.

- `src/model/DomainTypes.h`

Defines value types for drive/volume metadata, drive capabilities, disk zones, file classes, optimization settings/profiles, analysis results/stats, placement/move plans, and job progress.

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

Enumerates visible drives and read-only volume/media/capability status.

- `src/platform/windows/VolumeQueries.h`
- `src/platform/windows/VolumeQueries.cpp`

Queries file extents and volume free-space bitmap through read-only FSCTL calls.

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
