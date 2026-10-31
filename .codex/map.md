# Repository Map

Modules, source files, build files, dependencies and generated output. Keep it current with structure
changes (see `AGENTS.md`).

## Top level

| Path | What it is |
| --- | --- |
| `plan.md` | Development plan (phases, design decisions). |
| `.agents/01-plan.md`, `.agents/00-finished.md` | Open tasks and the log of finished ones. |
| `.codex/arch.md`, `.codex/map.md` | Architecture notes and this map. |
| `story/` | Notes from earlier attempts (read-only history). |
| `tools/` | Developer scripts. |

## Tools

| Path | What it does |
| --- | --- |
| `tools/fetch-wx.ps1` | Downloads the pinned wxWidgets prebuilt MSVC x64 archives (Dev, ReleaseDLL, headers), checks SHA-256, extracts to `3rdparty/wxWidgets`. No-op when the stamp matches; `-Force` redoes it. |

## Dependencies

| Dependency | Version | How it is obtained |
| --- | --- | --- |
| wxWidgets (MSW, vc14x x64 DLL) | 3.3.4 | `tools/fetch-wx.ps1`; version and hashes pinned in the script. |

## Generated output (git-ignored)

| Path | Contents |
| --- | --- |
| `3rdparty/downloads/` | Cached wx archives. |
| `3rdparty/wxWidgets/` | `include/`, `include/msvc/`, `lib/vc14x_x64_dll/` (import libs, debug and release DLLs), `icd-wx-stamp.txt`. |
