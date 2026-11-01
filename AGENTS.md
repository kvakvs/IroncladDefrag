---
apply: always
---

# About

This is a Windows x64 application implementing a disk analyser and defragmenter for 
Windows. This is a simple GUI application with top menu, all disks information panel, 
current drive map panel and controls and info panel in the bottom.

## Tools
- "C:\Program Files\" contains system-installed CMAKE.
- The host has Visual Studio community 2026 installed (MSVC STL, Windows SDK, rc, ninja via vcvars64).
- The main compiler is clang-cl from LLVM (`C:\Program Files\LLVM\bin`) with Ninja Multi-Config in
  `build/`. MSVC stays supported as a fallback: `set ICD_TOOLCHAIN=msvc` builds in `build-msvc/`.
- Use `build.bat`, `test.bat`, `quality.bat` (they set up the environment through `tools\dev-env.cmd`).
  `test.bat` runs the fast tests; tests on real volumes (`test.bat Release slow`) and the scripts under
  `tests/integration` run on demand, for example before finishing a task that changes scanning.
  In these scripts never `exit /b` inside a `( )` block: cmd loses the exit code there; use `goto`.

# Design

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

- `plan.md` in the repository root is the development plan. Work through its phases in order and check each
  phase's exit criteria before moving on.
- `.agents/01-plan.md` splits the plan into small isolated tasks. Take the first task whose dependencies are
  finished. When a task is finished, remove it from `.agents/01-plan.md` and add a compact bullet for it to
  `.agents/00-finished.md`.

## Commits

- Commit after each finished task: one commit per task, message `Txxx: <task title>`.
- Do not add `Co-Authored-By` lines to commit messages.
- Keep `.codex/arch.md` (architecture, layering, responsibility placement) and
  `.codex/map.md` (modules, source files, build files, dependencies, generated output) up to date in the same
  change as material structure changes.

## C++ GUI Development Practices

- Preserve the separation between wxWidgets UI code under `src/ui` and domain model code under `src/model`.
- Keep `src/main.cpp` and `icd::App` thin. Put window composition in UI classes, orchestration in controller/service classes, and disk/file-system logic outside the UI layer.
- Do not run drive scanning, fragmentation analysis, file movement, or other long-running disk operations on the UI thread. Use worker/service code and marshal progress, completion, and error updates back to wxWidgets safely.
- Treat defragmentation and file movement as high-risk operations: prefer explicit planning, dry-run-friendly data structures, cancellation support, careful error reporting, and minimal moved data.
- Keep model and strategy code testable without wxWidgets where practical. Use standard C++ types in the model layer and isolate Windows API calls behind narrow system-boundary modules.
- Use RAII for handles and resources. Avoid raw owning pointers in new code; prefer values, smart pointers, and wx ownership conventions where wxWidgets takes ownership.
- Use C++23 (`CMAKE_CXX_STANDARD 23`). Preserve Unicode/Windows x64 assumptions already present in the build. Be careful with paths, privileges, volume access, and large disk sizes.
- Follow `.clang-format` for C++ formatting and avoid unrelated reformatting.
- Keep precompiled-header additions stable and low churn. Do not add frequently edited project headers to `src/precompiled.h`.
- When adding new source files, update `CMakeLists.txt` explicitly so the `IroncladDefrag` target remains complete.

## Code Quality

- Cyclomatic complexity limit is 10 for every function: Lizard CCN <= 10 and clang-tidy cognitive
  complexity <= 10 (`.clang-tidy`: `clang-analyzer-*`, `bugprone-*`, `performance-*`, warnings as
  errors). Split a function instead of raising the limit.
- Before each commit run `quality.bat` (changed files plus translation units that include a changed
  header). Run `quality.bat all` once per finished phase or every 20 commits, whichever comes first.
  Both Lizard and clang-tidy must pass; do not raise thresholds or add suppressions just to pass.
- Findings located in the standard library, the Windows SDK or third-party headers (wxWidgets,
  GoogleTest) cannot be fixed here and may be ignored: disable the check in `.clang-tidy` with a
  comment naming the header and the reason. Findings in our own code are always fixed.
- Keep code clang-formatted (`"C:\Program Files\LLVM\bin\clang-format.exe" -i --style=file <files>`).

## When Editing or Creating Code

- Every new function, class, struct, field, enum (and enum value whose meaning is not obvious) gets a
  one-line comment stating its intent: why it was created, not what the code literally does.
  Put it on the declaration (the header for public items).