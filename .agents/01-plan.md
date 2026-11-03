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

## Phase 6 - Strategies and planner (§8, §9, §13 Phase 6)

### T095c - Sorted zones within 1.1 x at Maximum

- Depends: T095b
- Owns: `src/core/planner/SortZone.*`, `src/core/planner/MoveOrdering.*`, `src/core/planner/CycleGuard.*`
- Do: after T095b, presets that sort large zones (directory-clustering, access-time, read-ahead; photos and sources in
  photos / development) still move 1.1-1.45 x their lower bound at Maximum on the OptimalityTest volumes, nearly all of
  it parking: a packed sorted zone's layout is forced (every mover's place is fixed by the order), so moving most of a
  region in place leaves large dependency cycles (one component of ~1400 of ~2000 movers on directory-clustering).
  Zone room (10%) and greedy cycle breaking help little. Ideas: stream the sort through free space outside the zone,
  choose the zone's slack so its packed positions land on free space, or cycle-aware anchor choice. Also size-placement
  (big files in the pack-to-back zone) reaches 1.27 on one volume.
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

### T120 - Windows scheduled optimization

- Depends: T042
- Owns: `src/platform/win/ScheduledOptimize.*`, badge and dialog in `src/ui/dialogs/ScheduleDialog.*`
- Do: find out how Windows stores per-drive scheduled optimization and implement detection,
  confirmed opt-out and restore (§10.4).
- Done when: opt-out and restore work on a test volume and are visible in "Optimize Drives".

### T121 - VSS user interface

- Depends: T090, T098
- Owns: VSS section of `PlanTab`, `src/ui/dialogs/VssWarning.*`
- Do: warnings, unaligned-move count, "block unaligned moves" option.
- Done when: on the `vss` scenario aligned execution leaves `vssadmin list shadowstorage` unchanged.

### T122 - Unsupported configuration warnings

- Depends: T043
- Owns: `src/platform/win/VolumeTopology.*`
- Do: detect RAID, Storage Spaces, dynamic disks, removable and BitLocker/encrypted volumes; analyse
  only, block moves, explain why.
- Done when: each case shows the right badge and blocks execution. **Phase 9 exit check.**

---

## Phase 10 - Settings and custom strategies (§13 Phase 10)

### T130 - Settings store and dialog

- Depends: T007, T091
- Owns: `src/services/settings/*`, `src/ui/dialogs/SettingsDialog.*`
- Do: extend the settings file from T075 (`services/settings/SettingsStore`); Fast budget multiplier
  ×1/×2/×4/×10; thread and memory caps (Auto by default); the Settings dialog.
- Done when: settings persist across restarts and affect the next plan and job.

### T131 - Custom strategies

- Depends: T130, T097
- Owns: `src/services/settings/StrategyLibrary.*`
- Do: save user zone scripts, import/export. Per-drive strategy memory exists (T075); custom strategies
  get ids that fit `DriveStrategy::preset`.
- Done when: a custom strategy survives a restart and produces the same plan. **Phase 10 exit check.**

---

## Phase 11 - Deployment (§13 Phase 11)

### T140 - Installer

- Depends: T002
- Owns: `installer/*.nsi`, `make-installer.bat`
- Do: NSIS installer with `YYYY.MM.N` + short git hash, all DLLs, output in `dist/`.
- Done when: install, start from the install folder and uninstall all work. **Phase 11 exit check.**

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
