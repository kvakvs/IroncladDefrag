# IroncladDefrag Task Plan

Step-by-step tasks derived from `plan.md`. Section references (§) point to `plan.md`.

## How to use this file

- Take the **first task whose dependencies are all finished**. Tasks are small and self-contained: each
  one owns the modules listed under _Owns_, and touches other code only at the integration points
  listed under _Touches_.
- A task is finished only when everything under _Done when_ is true, the tests pass in Debug and
  Release, `quality.bat` passes, and `.codex/arch.md` / `.codex/map.md` are updated if the structure changed.
- **Commit after each task.** One commit per task, message `Txxx: <task title>` plus a short body.
  Do not add `Co-Authored-By` lines to commit messages.
- **When a task is finished, remove it from this file** and append it to `.agents/00-finished.md` as
  one compact bullet: `- Txxx <title> - what was delivered, key files` (find the commit with `git log --grep Txxx`). Do this in the same
  commit as the task.
- If a task turns out too big, split it here into `Txxxa`, `Txxxb` before starting. If new work is
  discovered, add a new task instead of widening the current one.
- **Gates** are stops: ask the user before continuing past them.
- **TESTING** Run fast tests for each step, run full tests every 3-5 steps.
  - If a test is taking too long, it might be worth to reclassify it into slow test or consider making it shorter to keep it fast. Consult the human. Aim at fast tests under 5-10 min total, slow tests under 15 min total.

---

## Phase 5 follow-up - Classification of churning files (§8.2, §8.3)

### T076b - Dev scenario with Rust, Python, Elixir/Erlang and Node projects

- Depends: T076a
- Owns: the `dev` scenario in `tools/testvol/gen-scenario/Scenarios.*`, `tests/integration/classes/check-classes.ps1`,
  `tests/integration/churn/check-churn.ps1`
- Do: the `dev` scenario gains Rust, Python, Elixir/Erlang and Node projects (with their marker files) with
  build/clean cycles, logs and dumps; the C++ repositories get their `CMakeLists.txt`.
- Since T076a: development types count only inside projects, so `repo-N\build\` needs a marker beside it (today
  `build\build.log` is Logs and `build\obj\*.lib` Unknown), and in `check-classes.ps1` a small `.ts` under `\video`
  (no project) is Unknown, not Source; Logs and Dumps join its expected classes.
- Done when: `check-classes.ps1` / `check-churn.ps1` find every churning folder and file type and nothing else;
  classifying a 6 M-file synthetic volume is not measurably slower than before T076a.

### T076c - Class summary of a real development folder

- Depends: T076a
- Owns: `icd-cli classify <folder>` (`src/cli/commands/Classify.*`)
- Do: a read-only folder walk (ordinary directory listing, no volume handle) that builds a snapshot without extents and
  prints the class summary through the same rule engine; run it on `F:\ESL` and `F:\Projects` (the user's choice; maybe
  also `F:\Projects\Simlunky` copied to a mechanical drive and analysed there); fix the catalog where it is wrong.
- Done when: the class summary of those folders has been reviewed with the user.

---

## Phase 6 - Strategies and planner (§8, §9, §13 Phase 6)

### T095d - Sorted zones within 1.1 x at Maximum

- Depends: T095c
- Owns: `src/core/planner/SortZone.*`, `src/core/planner/MoveOrdering.*`, `src/core/planner/CycleGuard.*`, placement
  in `src/core/planner/levels/MaximumLevel.*`
- Do: after T095c (cycles give up the moves with the fewest clusters per wait, unneeded breaks are kept in place),
  Maximum still parks too much where large files block each other: on the OptimalityTest volumes access-time 1.30 x the
  lower bound, directory-clustering and read-ahead 1.18-1.23, size-placement 1.08-1.18, photos 1.15, video 1.16 (since
  T132 counts videos of any size; back in `tightPreset()` once fixed), mixed 1.10; on
  the test bed `check-planner.ps1 -Scenarios all` fails only on `archive` at Maximum (14 moves of ISOs / VHDXs,
  1.216; was 1.377 before T095c). The cycles come from placement, not from how they are broken: a packed sorted zone's
  layout is forced, so large movers land on each other's sources. Ideas: place large files where no other mover's
  sources are (or evict to a spot outside the zone first instead of the fallback placement), stream the sort through
  free space outside the zone, or choose the zone's slack so packed positions land on free space.
- Done when: every preset stays within 1.1 x at Maximum in OptimalityTest, and `check-planner.ps1 -Scenarios all`
  passes.

---

## Phase 7 - Execution on the test bed (§10.3-10.4, §13 Phase 7)

---

## Phase 8 - GUI execution and live map (§13 Phase 8)

> **Gate G2:** passed 2026-11-03: first real-drive run on X: with the user's go-ahead (`photos`, Moderate, two runs,
> about 180 GB moved, chkdsk clean; see `.codex/arch.md` "First real-drive run"). Later real-drive runs (other presets, Maximum)
> stay the user's call.

---

## Phase 9 - VSS and Windows integration (§13 Phase 9)

> Phase 9 finished 2026-11-04 (T120, T121, T122).

---

## Phase 10 - Settings and custom strategies (§13 Phase 10)

> Phase 10 finished 2026-11-04 (T130, T131).

---

## Phase 11 - Deployment (§13 Phase 11)

### T141 - App icon

- Depends: T002
- Needs from the user: the icon. **Gate:** ask for it (a multi-size `.ico` with 16, 20, 24, 32, 48, 64 and 256 px, or a
  square PNG/SVG master of at least 256 px to build it from) before starting; do not draw one.
- Owns: `res/IroncladDefrag.ico`, the icon lines of `res/IroncladDefrag.rc`, the frame icon in `src/ui/MainFrame.*`
- Do: embed the icon as the executable's first icon resource (Explorer, taskbar, Alt+Tab, shortcuts and the installer
  use it); set it on the main frame and the dialogs through `wxIconBundle` so every size is sharp at any DPI; keep the
  source file in the repository and note its origin.
- Done when: Explorer, the taskbar, the title bar and Alt+Tab show the icon at 100% and 150-200% scaling, with no wx
  default icon left anywhere.

### T142 - Version 1.0

- Depends: T002
- Owns: `project(VERSION)` in `CMakeLists.txt`, `src/core/Version.h.in`, version text in `.codex/arch.md`
- Do: set the product version to 1.0.0 (it is 0.1.0 now; plan.md 14 decision 11: plain `MAJOR.MINOR.PATCH`, no
  year/month numbers). The short git hash is build metadata only (About box, log start line, installer file name),
  with a "-dirty" mark for builds from a changed tree.
- Done when: the title bar, About box, `icd-cli --version`, the executable's file properties and the log start line all
  say 1.0.0 (plus the hash where build metadata is shown).

### T140 - Installer

- Depends: T002, T141, T142
- Owns: `installer/*.nsi`, `make-installer.bat`
- Do: NSIS installer with the product version (T142) + short git hash, the app icon (T141), all DLLs, output in
  `dist/`.
- Done when: install, start from the install folder and uninstall all work. **Phase 11 exit check.**

### T143 - Product web page

- Depends: T140, T141
- Needs from the user: the domain, how to publish to the hosting (SFTP/SSH, FTP, a git push, a control panel upload),
  where the installer download is served from, and the contact / license / privacy text to show. **Gate:** ask before
  starting; publishing is public and needs the user's go-ahead for each upload.
- Owns: `web/` (static site sources and assets), `tools/publish-web.*`
- Do: a small static site (no framework, no tracking, works without JavaScript): what the app does and for whom
  (HDDs, zone strategies, least movement), screenshots of the map, strategies, plan review and live execution taken
  from the fake drive list, the safety model (dry-run plans, revalidation, Windows move API, pause/cancel, logs),
  system requirements (Windows 10 22H2+, x64, NTFS HDDs; SSDs get TRIM only), download with version, size and SHA-256
  of the installer, release notes, and the icon as favicon. Page text in the project's plain style. A publish script
  that uploads `web/` with credentials kept outside the repository.
- Done when: the user has reviewed the page locally, it is published at the domain with the current installer, the
  download's SHA-256 matches `dist/`, and the page reads well on a phone and a desktop.

---

## Deferred (look at later)

### T112 - Resume an unfinished execution from the GUI

- Depends: T111
- Owns: resume flow in `src/ui/WorkflowBinding.*`, `src/ui/ExecutionView.*`
- Why: a GUI run that was cancelled, or a crash or power loss, leaves an execution log the GUI cannot continue; today
  only `icd-cli execute --resume` does (`unfinishedExecutionLog()`, `resumedExecution()`). A new plan from a fresh
  analysis also works, so this is convenience, not safety.
- Do: on drive selection, find the volume's unfinished log and offer "Resume the interrupted run" (with how many moves
  are left); run it with the same job, Progress, Log and live map.
- Done when: a run cancelled in the GUI is resumed from the GUI and the volume ends as after an uninterrupted run.

### T114 - Read and write animation (consider)

- Depends: T111
- Owns: a read/write activity layer in `src/ui/map/*` fed from the analysis and execution progress
- Why: during analysis and execution the map changes in jumps; showing where the disk is reading and writing right
  now makes progress visible and tells a working run from a stalled one.
- Decide first (with the user, from a short mock-up or screenshots): what is shown (analysis: the MFT or bitmap region
  being read; execution: the source extent glowing as it is read, the destination as it is written, maybe a short trail
  between them), and how (map overlay with fading highlights, a small read/write indicator by the progress line, or
  both).
- Constraints: driven by the existing throttled progress (~10 Hz) and the live map feed, never by the disk I/O path;
  repaints only the changed tiles and stays within the 7.5 frame budget; off during Pause; respects the Windows
  "Show animations" setting; a View menu toggle.
- Done when: the chosen design runs on the fake drive list and a test VHDX without lowering execution throughput or
  map frame rate (measured before and after).

### T150 - Churn gap size after measuring NTFS allocation (TODO)

- Depends: T104b
- Owns: the `gapAfter` of the dependencies zone in `data/strategies/development.json`, plan.md 8.3
- Why: plan.md 8.3 reserves a churn gap of `max(5% of volume, 2 x build + temp)` after the dev zones so new build
  output lands in fast, contiguous space, and says to shrink it to 1% if NTFS does not allocate new files into it. The
  Phase 7 measurement (2026-11-02, `dev` scenario on 4 GB, full Maximum development layout, 200 new 256 KB files):
  67% of their clusters landed in the gap while the gap held 87% of the free space, so NTFS does not favour the gap;
  a third of the new data went to the other 13% of free space. The gap mostly works as plain consolidated free space.
- What shrinking would improve: less data moved to open the gap (Moderate and Maximum move files out of the gap's
  range to make it; on `dev` that is part of the ~40 K moves of a Maximum run), a lower lower-bound and fewer evictions
  and parking moves, and the free space it held stays available as zone room and slack for the zones after it. What
  it may cost: new build output less often contiguous next to the sources.
- How to observe the effect: with the default and with 1%, (1) `pwsh -File tests/integration/execute/check-execute.ps1
  -Scenarios dev -ChurnGap -SizeGB 4` prints the share of new build files' clusters in the gap against the gap's share
  of free space; (2) `icd-cli plan T: --strategy development --json` on the same `dev` volume gives `bytesMoved`,
  `lowerBound` and the ratio per level; (3) after a few build/clean cycles (write and delete build folders, as
  gen-scenario's `dev` does), `icd-cli analyze T: --json` shows fragmented files and free runs; compare build output
  fragmentation and the largest free run between the two settings.
- Done when: the measurements for both settings are recorded in `.codex/arch.md` and the default is chosen.
