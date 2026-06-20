# Project Map

## Root Files

- `AGENTS.md` - persistent agent instructions for this repository.
- `.codex/arch.md` - detected architecture and intended layering.
- `.codex/map.md` - this repository map.
- `.clang-format` - C++ formatting rules based on LLVM style with 4-space indentation and 120-column limit.
- `.gitignore` - ignored files.
- `CMakeLists.txt` - single executable target, wxWidgets include/library paths, compile definitions, precompiled header setup, and post-build DLL copying.

## Source Root

- `src/main.cpp` - Windows GUI process entry point and wxWidgets application registration.
- `src/precompiled.h` - precompiled header with Windows, wxWidgets, and common standard-library includes.
- `src/precompiled.cpp` - precompiled-header translation unit.

## UI Layer

- `src/ui/App.h`
- `src/ui/App.cpp`

Defines `icd::App`, the `wxApp` subclass that initializes wxWidgets and shows the main frame.

- `src/ui/MainFrame.h`
- `src/ui/MainFrame.cpp`

Defines `icd::MainFrame`, the top-level `wxFrame`. Currently owns menu bar creation, Exit/About handlers, and the status bar.

## Model Layer

- `src/model/Units.h`

Defines `icd::Quantity` and project-specific type aliases for counts, indexes, byte counts, sector counts, and MB/s throughput.

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

## Vendored Dependencies

- `3rdparty/wxWidgets` - vendored wxWidgets 3.3.1 headers, MSVC setup headers, libraries, and DLLs.
- `3rdparty/*.7z`
- `3rdparty/*.zip`

Stored wxWidgets source/header/library archives.

## Build Output

- `cmake-build-debug` - existing local debug build directory. Treat as generated output unless the user explicitly asks about it.
