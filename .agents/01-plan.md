# IroncladDefrag Task Plan

Step-by-step tasks derived from `plan.md`. Section references (§) point to `plan.md`.

## How to use this file

- Take the **first task whose dependencies are all finished**. Tasks are small and self-contained: each
  one owns the modules listed under *Owns*, and touches other code only at the integration points
  listed under *Touches*.
- A task is finished only when everything under *Done when* is true, the tests pass in Debug and
  Release, `quality.bat` passes, and `.codex/arch.md` / `.codex/map.md` are updated if the structure changed.
- **Commit after each task.** One commit per task, message `Txxx: <task title>` plus a short body.
  Do not add `Co-Authored-By` lines to commit messages.
- **When a task is finished, remove it from this file** and append it to `.agents/00-finished.md` as
  one compact bullet: `- Txxx <title> - what was delivered, key files` (find the commit with `git log --grep Txxx`). Do this in the same
  commit as the task.
- If a task turns out too big, split it here into `Txxxa`, `Txxxb` before starting. If new work is
  discovered, add a new task instead of widening the current one.
- **Gates** are stops: ask the user before continuing past them.

---

## Phase 4 - Fast MFT scanner, snapshots, scale (§6.3, §6.6, §13 Phase 4)

### T060 - MFT record parser
- Depends: T020
- Owns: `src/core/ntfs/*`
- Do: pure parser: update-sequence fixups, `$STANDARD_INFORMATION`, `$FILE_NAME`, `$DATA` (resident
  and run lists), `$ATTRIBUTE_LIST`, `$INDEX_ALLOCATION`; run-list decoding.
- Done when: unit tests on handcrafted and captured records, including corrupt ones (no crash).

### T061 - MFT reader pipeline
- Depends: T060, T047
- Owns: `src/platform/win/MftReader.*`, `src/services/scan/MftScanner.*`
- Do: `$MFT` extents, skip unused ranges via `$MFT::$BITMAP`, one overlapped sequential reader
  (8-16 MB, 2-4 in flight), bounded queue, parser workers with thread-local output, parallel merge
  (§6.3).
- Done when: scans a test volume end to end; disk ≥ 90% busy during the read.

### T062 - Attribute lists and path resolution
- Depends: T061
- Owns: `src/services/scan/MftSecondPass.*`
- Do: second pass for `$ATTRIBUTE_LIST` records; parallel parent resolution.
- Done when: fragmented-MFT-record files from the `special` scenario resolve correctly.

### T063 - Scanner cross-check
- Depends: T062
- Owns: `tests/integration/scanner-equivalence/*`
- Touches: default scanner selection in `AnalysisJob`
- Do: compare MFT and API extent indexes on every scenario for 1, 2 and N workers; switch the default
  to MFT with API as fallback.
- Done when: identical results everywhere; MFT ≥ 10× faster on a 1 M-file volume.

### T064 - Snapshot persistence
- Depends: T063
- Owns: `src/core/snapshot/SnapshotIO.*`, `src/services/snapshot/SnapshotStore.*`
- Do: compressed, versioned snapshot file with journal ID, USN and map aspect (§6.2.6, §7.1).
- Done when: save → load round trip is identical; corrupt or old-version files are rejected cleanly.

### T065 - USN catch-up
- Depends: T064
- Owns: `src/services/snapshot/UsnCatchUp.*`
- Do: read journal from the saved USN, re-read changed files, update snapshot and mark pyramid ranges
  dirty; full-rescan rules (journal ID changed, journal wrapped).
- Done when: after scripted changes on a test volume, catch-up result equals a full rescan.

### T066 - Memory modes
- Depends: T064, T007
- Owns: `src/core/resources/MemoryPlanner.*`, `src/services/snapshot/MappedStorage.*`
- Do: predict snapshot size from MFT record count; give-way ladder (§6.6); memory-mapped extent index
  and name pool; low-memory notification handling.
- Done when: forced 2 GB / 2-thread caps give identical results to the uncapped run.

### T067 - Scale volume and benchmarks
- Depends: T065, T066
- Owns: `tools/testvol/make-scalevol/*`, `tools/bench/scan-bench.*`
- Do: 30 TB thin VHDX filled with `SetFileValidData` files (§12); benchmarks for §6.5 scan and
  catch-up targets.
- Done when: targets met and recorded. **Phase 4 exit check.**

---

## Phase 5 - Classification and activity (§8.2, §13 Phase 5)

### T070 - Rule engine
- Depends: T020
- Owns: `src/core/classify/RuleEngine.*`
- Do: ordered rules over path patterns, extension, size, age; per-directory overrides; parallel
  classification of a snapshot.
- Done when: tests for rule order, overrides and determinism.

### T071 - Built-in class catalog
- Depends: T070
- Owns: `src/core/classify/Catalog.*`, `data/classes.json`
- Do: every class from §8.3-8.6 (source, repo packs, dependencies, build output, temp, archives,
  disc images, VM disks, backups, photos, catalogs, video, unknown) as data.
- Done when: one test per class with typical and edge-case paths.

### T072 - Last-access reliability
- Depends: T070, T042
- Owns: `src/platform/win/LastAccessSetting.*`, `src/core/classify/AccessTimeHeuristic.*`
- Do: read the `disablelastaccess` behaviour; heuristic on timestamp distribution; badge.
- Done when: heuristic tests; badge correct on the dev machine.

### T073 - USN churn statistics
- Depends: T046, T070
- Owns: `src/services/activity/ChurnStats.*`
- Do: per-file and per-directory create/delete/overwrite counts from the retained journal; churn
  flag feeding the rule engine.
- Done when: the `dev` scenario's build and temp directories are detected as churn.

### T074 - Class map mode and summary
- Depends: T071, T032
- Owns: `src/core/map/ClassColorMode.*`, Summary tab class section
- Do: class color mode, per-class counts and bytes.
- Done when: class colors and counts are correct on the `mixed` scenario. **Phase 5 exit check.**

---

## Phase 6 - Strategies and planner (§8, §9, §13 Phase 6)

### T080 - Zone script model
- Depends: T070
- Owns: `src/core/strategy/ZoneScript.*`
- Do: zone model from §8.1 (selector, anchor, order, order_by_level, action, fragment_ok, gap_after),
  JSON read/write, validation with clear errors.
- Done when: round-trip and validation tests.

### T081 - Target layout resolver
- Depends: T080, T021
- Owns: `src/core/planner/TargetLayout.*`
- Do: resolve zones to LCN ranges around fixed obstacles (`$MFT`, MFT zone, unmovable), anchors,
  gaps (§8.1).
- Done when: tests with obstacles at every position, full volumes, empty zones.

### T082 - Plan model, simulator and validator
- Depends: T081
- Owns: `src/core/planner/Plan.*`, `src/core/planner/Validator.*`
- Do: plan and move types; simulation on copies of free runs and extents; all checks from §9.5.
- Done when: tests with deliberately broken plans catch every rule.

### T083 - Lower bound and target fitting
- Depends: T082
- Owns: `src/core/planner/LowerBound.*`
- Do: lower bound (§9.3.1); choose among target candidates by lowest lower bound (§9.3.2).
- Done when: tests on hand-made layouts with known minimum bytes.

### T084 - Good-enough filter and defragment-only
- Depends: T083
- Owns: `src/core/planner/DefragOnly.*`, `src/core/planner/CandidateFilter.*`
- Do: §9.3.3 filter; defragment files near their current position.
- Done when: validator passes; untouched files are exactly the good-enough set.

### T085 - Fast-fill planner
- Depends: T084
- Owns: `src/core/planner/FastFill.*`
- Do: §9.3.5 hole filling per zone and per stripe, choosing the packed boundary that minimises bytes.
- Done when: bytes moved equal the lower bound on test layouts.

### T086 - Sort planner (weighted LIS)
- Depends: T084
- Owns: `src/core/planner/SortZone.*`
- Do: §9.3.4 byte-weighted LIS with tolerance.
- Done when: optimality tests against brute force on small inputs.

### T087 - Pack-to-back planner
- Depends: T084
- Owns: `src/core/planner/PackToBack.*`
- Do: pack zone against volume end; no order, or age order with oldest at the end (§8.6).
- Done when: tests for both orders; already-packed videos never move.

### T088 - Move ordering and cycles
- Depends: T085, T086, T087
- Owns: `src/core/planner/MoveOrdering.*`
- Do: dependency graph, topological order, cycle breaking by parking the smallest file, "move the
  smaller thing" (§9.3.6-9.3.7).
- Done when: tests with chains and cycles; each file moves at most once except reported parking.

### T089 - Budgets and benefit ranking
- Depends: T088
- Owns: `src/core/planner/Budget.*`
- Do: benefit-per-byte ranking, dependency-closed prefixes, budget cut; any prefix improves the volume
  (§9.3.8, §9.2.1 step 5).
- Done when: tests that every prefix validates and improves the metrics.

### T090 - VSS detection and alignment
- Depends: T088, T042
- Owns: `src/platform/win/ShadowCopies.*`, `src/core/planner/VssAlign.*`
- Do: detect shadow copies; prefer 16 KB-aligned destinations; count unaligned moves (§9.4).
- Done when: alignment tests for 4 KB and 16 KB clusters; detection works on the `vss` scenario.

### T091 - Fast level
- Depends: T089, T073
- Owns: `src/core/planner/levels/FastLevel.*`
- Do: hot-spot scoring, local compaction, big long-lived files moved out (≥64 MB, 30 days), budget
  2% / 15 min with multiplier (§9.2.1).
- Done when: Fast plans touch only hot spots and stay within budget on the scenarios.

### T092 - Moderate and Maximum levels
- Depends: T089
- Owns: `src/core/planner/levels/ModerateLevel.*`, `src/core/planner/levels/MaximumLevel.*`
- Do: thresholds, tolerances and budgets from the §9.2 table; parking allowed only at Maximum.
- Done when: tests show Moderate ⊆ Maximum in reached goals and bytes ≤ Maximum.

### T093 - Parallel planning orchestration
- Depends: T091, T092, T090
- Owns: `src/services/plan/PlanJob.*`
- Do: §6.4 stages (filter, per-zone/stripe planning, reconcile, assignment, validation) on the pool;
  three levels in parallel; time estimate (§9.2.2).
- Done when: parallel = serial plans; 30 TB synthetic plan ≤ 60 s on 8 cores.

### T094 - Strategy presets
- Depends: T080, T071
- Owns: `data/strategies/*.json`, `tests/core/presets/*`
- Do: dev, archive/images, photos, video, mixed and the general strategies (§8.3-8.8) as zone scripts.
- Done when: property tests on random synthetic volumes pass for every preset at every level.

### T095 - Planner optimality checks
- Depends: T093, T094, T041
- Owns: `tests/integration/planner/*`
- Do: exhaustive search on tiny volumes confirms minimal bytes; `planned / lower bound ≤ 1.1` on the
  scenarios.
- Done when: both checks pass.

### T096 - CLI plan
- Depends: T093
- Owns: `src/cli/commands/Plan.*`
- Do: `icd-cli plan <vol> --strategy <name> --level fast|moderate|max --json`.
- Done when: output matches the GUI plan for the same snapshot.

### T097 - Strategy tab
- Depends: T094, T012
- Owns: `src/ui/tabs/StrategyTab.*`
- Do: preset picker, effort level, editable zone table including churn gap size (§11).
- Done when: edits change the next plan; invalid edits show validation errors.

### T098 - Plan tab and plan overlay
- Depends: T093, T097
- Owns: `src/ui/tabs/PlanTab.*`, `src/core/map/PlanColorMode.*`, `src/core/map/ZoneFitColorMode.*`
- Do: three levels side by side; details for the chosen level; zone-fit and plan-preview map modes
  (§7.4). Fast plans show the "run again to keep improving" note.
- Done when: plan preview on the map matches the plan's moves. **Phase 6 exit check.**

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
- Do: versioned JSON settings; Fast budget multiplier ×1/×2/×4/×10; thread and memory caps (Auto by
  default).
- Done when: settings persist across restarts and affect the next plan and job.

### T131 - Custom strategies
- Depends: T130, T097
- Owns: `src/services/settings/StrategyLibrary.*`
- Do: save user zone scripts, import/export, per-drive default strategy.
- Done when: a custom strategy survives a restart and produces the same plan. **Phase 10 exit check.**

---

## Phase 11 - Deployment (§13 Phase 11)

### T140 - Installer
- Depends: T002
- Owns: `installer/*.nsi`, `make-installer.bat`
- Do: NSIS installer with `YYYY.MM.N` + short git hash, all DLLs, output in `dist/`.
- Done when: install, start from the install folder and uninstall all work. **Phase 11 exit check.**
