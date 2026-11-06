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
- Since T076a/c: development types count only inside projects, so `repo-N\build\` needs a marker beside it (today
  `build\build.log` is Logs, and `build\obj\*` is Build only by extension); in `check-classes.ps1` a small `.ts`
  under `\video` (no project) is Unknown, not Source, and other `.git` files are Repository; Logs and Dumps join its
  expected classes. `icd-cli classify <folder>` (T076c) shows a generated tree's classes without a volume scan.
- Done when: `check-classes.ps1` / `check-churn.ps1` find every churning folder and file type and nothing else;
  classifying a 6 M-file synthetic volume is not measurably slower than before T076a.

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

### T143 - Product web page

- Depends: T140, T141
- Decided with the user (2026-11-04): a static Next.js site (`output: 'export'` into `web/out/`) at
  https://ironclad.clau.se on the server pupa (`/home/defrag/www`, nginx, certbot), installers under `/downloads/`.
  Published by the `/deploy` skill (`.claude/skills/deploy/SKILL.md`, `deploy/publish-web.sh` over WSL ssh + rsync:
  the site with deletion of remote extras, the installers separately without, so they survive redeploys); server
  setup in `deploy/setup-server.sh` and `deploy/nginx/ironclad.clau.se`. Still needed from the user: the contact /
  license / privacy text. **Gate:** publishing is public; each upload needs the user's go-ahead (invoking /deploy).
- Owns: `web/` (Next.js sources and assets), `deploy/`
- Do: a small static site (no tracking, readable without JavaScript: static export, no client-only content): what the app does and for whom
  (HDDs, zone strategies, least movement), screenshots of the map, strategies, plan review and live execution taken
  from the fake drive list, the safety model (dry-run plans, revalidation, Windows move API, pause/cancel, logs),
  system requirements (Windows 10 22H2+, x64, NTFS HDDs; SSDs get TRIM only), download with version, size and SHA-256
  of the installer (from the `.sha256` next to it), release notes, and the icon as favicon. Page text in the
  project's plain style.
- Done when: the user has reviewed the page locally, it is published at the domain with the current installer, the
  download's SHA-256 matches `dist/`, and the page reads well on a phone and a desktop.

---

## Deferred (look at later)

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
- Scenarios dev -ChurnGap -SizeGB 4`prints the share of new build files' clusters in the gap against the gap's share
  of free space; (2)`icd-cli plan T: --strategy development --json`on the same`dev`volume gives`bytesMoved`,
  `lowerBound`and the ratio per level; (3) after a few build/clean cycles (write and delete build folders, as
  gen-scenario's`dev`does),`icd-cli analyze T: --json` shows fragmented files and free runs; compare build output
  fragmentation and the largest free run between the two settings.
- Done when: the measurements for both settings are recorded in `.codex/arch.md` and the default is chosen.
