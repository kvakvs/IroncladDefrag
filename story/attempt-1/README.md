# Attempt 1 (Nov 2025 – Jun 2026)

The first try at IroncladDefrag. The source was removed from the working tree to start fresh;
everything is still in git history.

## Files here

- `plan.md` – the original 10-phase development plan (goals, zones, profiles, safety, deployment).
- `arch.md` – the architecture as it stood at the end of the attempt.
- `map.md` – the source file map as it stood at the end of the attempt.

## Recovering the old code

Last commit with the full source tree: `aacdcfb` ("Remove execution limit").

```
git show aacdcfb:src/ui/DriveMapPanel.cpp     # view one file
git checkout aacdcfb -- src CMakeLists.txt    # restore the old tree
```

Not in git (was untracked and deleted): `3rdparty/` with wxWidgets 3.3.1 prebuilt MSVC x64 libs,
headers and archives (`wxMSW-3.3.1_vc14x_x64_Dev.7z`, `wxMSW-3.3.1_vc14x_x64_ReleaseDLL.7z`,
`wxWidgets-3.3.1-headers.7z`, `wxWidgets-3.3.1.zip`), and the `build/` output directory.

## What was built

About 8,700 lines of C++20 across 67 files, built with CMake + Visual Studio 2026, wxWidgets 3.3.1 DLLs.

| Layer | Contents |
|---|---|
| `ui` | MainFrame, drive list panel, notebook of analysis pages, cluster-grid drive map, workflow panel, profile/safety/move-plan dialogs |
| `app` | ApplicationController (orchestration, snapshots, callbacks), BackgroundJob (`std::thread` + cooperative cancel) |
| `analysis` | Read-only scan: file metadata, retrieval pointers (extents), volume bitmap (free space); a fake service for development |
| `classification` | FileClassifier: size, type, recency, directory hints, safety exclusions, target zone |
| `optimization` | Profile catalog, settings serializer, PlacementPlanner (target intent), MovePlanner (dry-run plan with simulated reservations) |
| `execution` | MoveExecutor: revalidate, `FSCTL_MOVE_FILE`, verify extents, per-file outcomes |
| `persistence` | Versioned text settings at `%LOCALAPPDATA%\IroncladDefrag\settings.txt` |
| `platform/windows` | Drive enumeration, FSCTL queries, move/privilege/UAC relaunch, RAII handles |
| `model` | Domain value types, unit-safe quantities, file/fragment/free-space/geometry models |

## Timeline

| Date | Commits | What happened |
|---|---|---|
| 2025-11-04 | `9c4c0d3`–`62cfa0f` | Initial skeleton; tried WinUI, then WinUI 3 |
| 2025-11-05 | `9953485` | WinUI 3 build would not start ("DLL not found") |
| 2025-11-05 | `c64dfb5`, `fa4c6d3` | Switched to wxWidgets; app starts |
| 2026-06-20 | `d9f68c9`–`1852cd8` | Codex inspection, build/launch scripts, `plan.md` written |
| 2026-06-20 | `cd87542`–`1e20d21` | Phases 1–8 implemented in a single day (foundation, analysis, classification, drive map, profiles, planning, execution, GUI workflow, persistence/safety) |
| 2026-06-20 | `b94d036`–`aacdcfb` | UI cleanup: status throttling, removed duplicated tabs/panels, dropped default dry-run, planning progress indicator, removed execution cap |

## Where it ended

- Phases 9 (Validation) and 10 (NSIS deployment) were never started. There were no tests.
- TRIM for SSDs was not implemented.
- Execution moved whole files only, from plans built on an in-memory snapshot.
- Post-build only copied the debug wxWidgets DLLs; release DLLs were listed but not copied.
- The last commits were mostly UI rework (duplicated status and panels, a large `DriveAnalysisPage`
  cut back), so UI layout and workflow were still in flux.
- Safety defaults moved over time: dry-run went from default to opt-in, and the moved-data cap was removed.

## Things to consider for the next plan

- Choose the GUI stack early and get it starting reliably before adding features (WinUI 3 cost the first days).
- Add tests from the start, especially for classification and planning, using synthetic drive maps.
- Design the UI workflow (panels, tabs, where status appears) before building it, to avoid the
  duplication that had to be removed later.
- Decide the safety defaults (dry-run, moved-data caps, confirmations) up front.
- Smaller phases with exit criteria that are actually checked before moving on.
