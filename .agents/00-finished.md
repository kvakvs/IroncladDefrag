# Finished Tasks

Finished tasks from `.agents/01-plan.md`, one compact bullet each:
`- Txxx <title> - what was delivered, key files` (find the commit with `git log --grep Txxx`)

- T000 Task plan - split `plan.md` into small isolated tasks in `.agents/01-plan.md`, added commit and
  progress rules to `AGENTS.md`.
- T001 wxWidgets fetch script - `tools/fetch-wx.ps1` downloads wx 3.3.4 vc14x x64 Dev/ReleaseDLL/headers,
  checks pinned SHA-256, extracts via 7-Zip or System32 `tar` into `3rdparty/wxWidgets` through a staging
  dir; stamp file makes re-runs a no-op. Started `.codex/arch.md` and `.codex/map.md`.
- T002 CMake skeleton and build scripts - C++23 targets `icd_core`, `icd_platform`, `icd_services`,
  `IroncladDefrag` (empty frame), `icd-cli` (`--version`); `cmake/IcdWxWidgets.cmake` imported wx targets
  with per-config DLL copy; `build.bat [Debug|Release|All]` fetches wx and builds.
- T002a Quality gate: clang-tidy and Lizard - `quality.bat [changed|all]` with pinned clang-tidy 22.1.8 and
  Lizard 1.24.0 in `.venv-quality`; Ninja + cl compile database in `build/quality`; CCN and cognitive
  complexity <= 10, warnings as errors (`.clang-tidy`, `cmake/Check*.cmake`, `cmake/quality_scope.py`).
- T003 Application manifest and version resource - `res/IroncladDefrag.manifest` (requireAdministrator,
  Common Controls v6, PerMonitorV2, long paths), `res/IroncladDefrag.rc` (wx.rc + VERSIONINFO),
  generated `core/Version.h` from `src/core/Version.h.in`; `run-elevated.bat`.
