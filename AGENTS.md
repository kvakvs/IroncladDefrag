---
apply: always
---

This is a Windows x64 application implementing a disk analyser and defragmenter for 
Windows. This is a simple GUI application with top menu, all disks information panel, 
current drive map panel and controls and info panel in the bottom.

The supported functions are:
- Analysis of a drive (file map, fragmentation of files and free space, etc.)
- Selecting a move strategy and moving files. 
- Multiple move strategies supported such as: defragment a file, optimize hot zones 
  (frequently accessed files moved to faster disk zone), optimize cold zones (rarely
  accessed files moved to slower zone), directory clustering files together, file type
  and size segregation, access time based sorting, size-based placement, free space 
  optimization, packing files in sequential zones to optimize read-ahead caching.
- All the above while trying to maintain minimal amount of moved data.

## Repository Orientation

- Refer to `.codex/arch.md` when making architecture, layering, or responsibility-placement decisions.
- Refer to `.codex/map.md` when looking for important modules, source files, build files, vendored dependencies, or generated-output locations.
- Keep documentation updates consistent with the current source tree. If code structure changes materially, update `.codex/arch.md` and `.codex/map.md` in the same change.

## C++ GUI Development Practices

- Preserve the separation between wxWidgets UI code under `src/ui` and domain model code under `src/model`.
- Keep `src/main.cpp` and `icd::App` thin. Put window composition in UI classes, orchestration in controller/service classes, and disk/file-system logic outside the UI layer.
- Do not run drive scanning, fragmentation analysis, file movement, or other long-running disk operations on the UI thread. Use worker/service code and marshal progress, completion, and error updates back to wxWidgets safely.
- Treat defragmentation and file movement as high-risk operations: prefer explicit planning, dry-run-friendly data structures, cancellation support, careful error reporting, and minimal moved data.
- Keep model and strategy code testable without wxWidgets where practical. Use standard C++ types in the model layer and isolate Windows API calls behind narrow system-boundary modules.
- Use RAII for handles and resources. Avoid raw owning pointers in new code; prefer values, smart pointers, and wx ownership conventions where wxWidgets takes ownership.
- Preserve Unicode/Windows x64 assumptions already present in the build. Be careful with paths, privileges, volume access, and large disk sizes.
- Follow `.clang-format` for C++ formatting and avoid unrelated reformatting.
- Keep precompiled-header additions stable and low churn. Do not add frequently edited project headers to `src/precompiled.h`.
- When adding new source files, update `CMakeLists.txt` explicitly so the `IroncladDefrag` target remains complete.
