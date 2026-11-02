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

### T093b - Planning at the 30 TB scale target

- Depends: T093
- Owns: performance of `src/core/planner/*` and `src/services/plan/PlanJob.*`
- Do: bring `plan-bench` (30 TB / 20 M files, 8 workers) to 60 s or less for all three levels (plan.md 6.5), at least
  5x faster than on one worker. Measured after T093: 6 M files 42 s; 30 TB / 20 M files 199 s (Fast 131 s, Moderate
  172 s, Maximum 199 s), slightly superlinear; Fast's single huge hot spot and the serial parts of ordering and metrics
  dominate.
- Done when: `plan-bench --threads 8` reports 60 s or less and `--compare 1` identical plans.

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

### T100 - Move primitive

- Depends: T047
- Owns: `src/platform/win/MoveFile.*`
- Do: `FSCTL_MOVE_FILE` wrapper in chunks (default 16 MB), stream selection, error mapping
  (destination in use, locked, unsupported).
- Done when: integration test moves files on a test volume; hashes unchanged.

### T101 - Revalidation before execution

- Depends: T065, T082
- Owns: `src/services/execute/Revalidate.*`
- Do: USN delta since the snapshot; drop or re-read changed files; refuse stale snapshots (§10.3).
- Done when: scripted changes between plan and execute are detected.

### T102 - Executor

- Depends: T100, T101
- Owns: `src/services/execute/Executor.*`
- Do: single mover thread plus prefetch helper; re-read pointers before each move; verify after;
  stale-bitmap retry; skip reasons; pause/resume/cancel; low I/O priority; throughput limit; snapshot
  updated from verified results.
- Done when: a plan runs on a test volume; pause, resume and cancel mid-run are safe.

### T103 - Execution log and resume

- Depends: T102
- Owns: `src/services/execute/ExecutionLog.*`
- Do: append-only JSON lines log; resume or rebuild after a crash.
- Done when: killing the process mid-run and resuming completes the plan correctly.

### T104 - CLI execute and integration suite

- Depends: T103, T096
- Owns: `src/cli/commands/Execute.*`, `tests/integration/execute/*`
- Do: `icd-cli execute`; per-scenario test: analyse, plan, execute, hash check, `chkdsk`, predicted
  layout = rescan; record NTFS allocation behaviour into the churn gap (§8.3).
- Done when: every scenario passes. **Phase 7 exit check.**

---

## Phase 8 - GUI execution and live map (§13 Phase 8)

### T110 - Execute controls and Progress tab

- Depends: T104, T098
- Owns: `src/ui/tabs/ProgressTab.*`, `src/ui/dialogs/ConfirmExecuteDialog.*`
- Do: Execute / Pause / Cancel; confirmation with plan summary; stage, throughput, worker
  utilisation, ETA, current file, active resource caps.
- Done when: the full workflow runs on a test VHDX from the GUI.

### T111 - Live map and Log tab

- Depends: T110
- Owns: `src/ui/tabs/LogTab.*`, live-update wiring in `src/ui/map/MapLiveUpdates.*`
- Do: moved ranges mark tiles dirty while executing; skipped and failed files with reasons.
- Done when: moves appear on the map as they happen. **Phase 8 exit check.**

> **Gate G2:** first real-drive run (§13 "First real-drive run") only with the user's go-ahead.

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
