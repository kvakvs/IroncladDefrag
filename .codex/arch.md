# Architecture Notes

How the code is layered and where responsibilities live. The full design is in `plan.md` (section 5);
this file records what is actually built and the decisions made along the way.

## Layers (target, from `plan.md` section 5)

- `src/core` - wx-free, Win32-free C++23 model and algorithms.
- `src/platform/win` - narrow Win32 wrappers.
- `src/services` - jobs, thread pool, orchestration.
- `src/ui` - wxWidgets windows and widgets.
- `src/cli` - `icd-cli` console front end.

## Build

- Compiler: clang-cl (LLVM) with lld-link and Ninja Multi-Config is the main build; it uses the MSVC
  STL and Windows SDK from vcvars64 and links the prebuilt MSVC wx DLLs (same ABI). MSVC still builds
  (`ICD_TOOLCHAIN=msvc`), so MSVC-only flags (`/permissive-`, `/Zc:*`, `/MP`) are applied only there.
- C++23, warnings as errors, only Debug and Release configurations.
- Dependency direction: `ui`/`cli` -> `services` -> `platform` -> `core`. `core` has no wx or Win32
  includes; `platform` is the only place that includes `<windows.h>` for system calls.
- wx is linked through imported targets, not wx's `#pragma comment(lib)` auto-linking (only
  `include/`, not `include/msvc/`, is on the include path). This lets the post-build step copy exactly
  the runtime DLLs of the active configuration with `$<TARGET_RUNTIME_DLLS>`.
- The version comes from `project(VERSION)`: CMake generates `core/Version.h` (plain macros) that
  both C++ code and `res/IroncladDefrag.rc` include.
- The GUI always runs elevated (`requireAdministrator` in `res/IroncladDefrag.manifest`); the linker's
  own UAC entry is off (`/MANIFESTUAC:NO`) so there is exactly one. `icd-cli` stays `asInvoker` so it
  works in scripts; commands that need administrator rights will check and report it.
- Automated smoke runs start the GUI with `__COMPAT_LAYER=RunAsInvoker` to skip the UAC prompt.

## Logging

- `core::logging` holds the record model, formatting and a thread-safe `Logger` (one mutex; lines are
  formatted outside it, sinks are called under it so lines never interleave). A process-wide
  `globalLogger()` keeps call sites short: `logging::info("scan done", {{"files", n}})`.
- Sinks live in `platform`: a rotating file in `%LOCALAPPDATA%\IroncladDefrag\logs\<exe>.log`
  (8 MB, 5 old files, flushed per line so a crash keeps the tail) and `OutputDebugStringW`.
- `services::initAppLogging()` wires both for each executable; a file that cannot be opened only
  produces a warning, never a failure.

## Jobs: cancellation and progress

- Cancellation is cooperative: work polls a `CancelToken` (a `std::stop_token`) between work items or
  calls `throwIfCancelled()`. A `CancelSource` built from a parent token is cancelled with it, so one
  Cancel button stops a job and all of its sub-jobs.
- Progress is a value (`stage`, `done`, `total`, `message`) pushed to a `ProgressSink`.
  `ThrottledProgress` sits in front of the UI: about 10 reports per second, but the first report,
  stage changes and finished stages always pass, and `flush()` delivers the newest held-back one.
  Within a stage (same name and total) progress only moves forward: workers compute and report counts
  concurrently, so a lower count arriving late is dropped.

## Jobs and the UI

- `services::AppServices` is the composition root (created by `App` after logging, destroyed in
  `OnExit` before the shutdown line): caps, then the `ThreadPool`, then the `JobRunner`. The runner is
  destroyed first, cancelling and waiting for jobs while the pool still exists.
- A job is a function of `JobContext` (token, throttled progress sink, executor). The runner runs it
  as a pool task, logs start and end, and reports exactly one `JobResult` (Succeeded, Cancelled when
  the token was cancelled, Failed with the exception text).
- `ui::JobEventBridge` implements `JobObserver` and turns callbacks into `wxThreadEvent`s with
  payloads, queued with `wxQueueEvent`. A window detaches its bridge before it is destroyed, so
  events from still-running jobs are dropped. Event types are function-local statics (created on
  first use), not `wxDEFINE_EVENT` globals.
- Command enable state is set explicitly when job state changes: wxMSW refreshes `wxEVT_UPDATE_UI` for
  menus only when a menu opens, and it ignores accelerators of disabled items.

## Main window

- Top to bottom: menu, all-disks panel, then a splitter with the map above and the bottom panel below.
  The splitter gives window growth to the map (sash gravity 1); the bottom panel keeps its height.
- Status appears in one place only: the line under the bottom notebook, next to the workflow buttons
  and a gauge. There is no status bar; the Progress tab holds details (stage, counts, resources), not
  a second status text.
- One `CommandId` per command, shared by menu items, bottom-panel buttons and (later) accelerators.
  Buttons carry the command id as their window id, so `wxEVT_MENU` and `wxEVT_BUTTON` go to the same
  handler and `enableCommand()` enables both.
- Drives come from `services::IDriveProvider` (owned by `AppServices`): `SystemDriveProvider` enumerates the real
  volumes; `ICD_FAKE_DRIVES=1` brings back the Phase 1 fake list (with the 30 TB design-target drive). The model is
  `core::DriveInfo`; the column texts are core functions (`core/format/DriveText`), so the wording is
  tested and the CLI can reuse it. `ui::DrivesPanel` shows them in a `wxDataViewCtrl` (selection,
  keyboard and column resizing come free); two custom renderers draw the space bar and the SSD-only
  TRIM button. A TRIM click selects its row and queues `ID_TRIM` up to the frame.
- Shortcuts: one table in `ui/Accelerators` puts them on the menu items; wxMSW builds the frame's
  accelerator table from the menu and skips disabled items, so keys follow the same enable rules.
  Workflow keys are F5 (Analyze), Shift+F5 (Quick), F6-F9 (Strategy, Build plan, Review, Execute),
  Pause and Esc (only enabled while a job runs). The plain map keys of plan.md 7.4 (+, -, 0, 1,
  arrows, PgUp/PgDn, Home/End) act only while the map has focus, so they never steal typing; zoom
  keys post the same View commands as the menu, panning is reserved for the map engine.
- Every shortcut table entry (menu shortcuts and map keys) carries a short description. Help > Keyboard
  shortcuts (F1) and the tooltips of the workflow buttons, the TRIM button and the map toolbar are
  generated from it (`commandTip()`: "Analyze the selected drive (F5)"), so there is no second list to
  drift. The TRIM button is drawn in a list cell, so the list's rows window shows its tooltip while the
  pointer is over it.
- View commands are enabled once a map is shown; color modes whose features are still to come (file
  class, zone fit, plan preview) stay disabled (`MapPanel::commandAvailable()`). The toolbar color
  choice posts the same View command as the menu, so check marks and toolbar always agree.
- UI numbers use `core::formatCount()` rather than the C locale, which would also change printf output.

## Drive discovery

- `platform::enumerateVolumes()` walks `FindFirstVolumeW`, keeps volumes that are mounted somewhere and have a file
  system (recovery/EFI partitions and empty card readers drop out) and orders them by first mount path. Per volume:
  label, file system and sizes from the path APIs; disk number, partition offset and disk size from
  `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` / `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` (any access); NTFS data
  (`FSCTL_GET_NTFS_VOLUME_DATA`: exact cluster counts, MFT, MFT mirror and zone, record size, version) through a
  read handle, which needs administrator rights. Cluster counts come from NTFS data when present:
  `GetDiskFreeSpaceW`'s 32-bit counts wrap above 16 TB with 4 KB clusters.
- Media (`platform/win/StorageProperties`): `IOCTL_STORAGE_QUERY_PROPERTY` on `\\.\PhysicalDriveN` (no read access
  needed) for seek penalty, TRIM, bus type, removable and model, kept raw as `core::StorageFacts`.
  `core::classifyMedia()` decides: file-backed virtual disks are `Virtual` first (a VHDX on an SSD reports no seek
  penalty, yet it is the test bed and keeps the full workflow), then the seek penalty (HDD / SSD), then TRIM as an
  SSD hint; a device that says nothing stays `Unknown` (full workflow, shown as such). Checked against
  `Get-PhysicalDisk` in `StoragePropertiesTest`.
- Nothing throws: the first failed query is kept in `VolumeRecord::problem` and logged, and the drive is listed with
  what was answered. Without NTFS data and without elevation the drive gets the "Admin needed" badge.
- The core model is `core::VolumeLayout` inside `DriveInfo`; `toDriveInfo()` converts (UTF-16 to UTF-8 through
  `platform/win/TextConvert`). Badges that are not detected yet (last access, scheduled optimization) are
  `std::optional` and show nothing until known, rather than a guessed "off".
- `tests/platform/VolumesTest` compares every lettered NTFS volume with `fsutil fsinfo ntfsinfo` (serial, version,
  clusters, sector and cluster size, record size, MFT, mirror, zone, reserved).

## SSD TRIM

- TRIM is Windows' own `defrag <volume> /L /U /V` (plan.md 10.5), started from System32 by full path through
  `platform::runCaptured()` (no window, stdout and stderr on one pipe, cancel terminates it; stopping a retrim
  halfway is harmless). `services::makeTrimJob()` splits the output on CR and LF (progress lines are redrawn
  with a bare CR), converts it from the OEM code page, reports "Retrim: NN%" as progress (a "Performing pass N"
  line starts a new stage, since the percentage restarts) and hands every other line to a `TrimOutput` queue.
- The workflow binding moves queued lines into the Log tab on each progress event and at the end. defrag
  buffers its output when it writes to a pipe, so in practice the lines arrive together when it finishes.
- Only SSD rows show the TRIM button and only `SsdReady` allows the action, so HDD, virtual and unknown drives
  never offer it.

## Workflow

- `services::Workflow` is the state machine of plan.md section 11 (NoDrive, Unsupported, SsdReady,
  Trimming, NotAnalyzed, Analyzing, Analyzed, Planning, Reviewing, Executing, Paused, Executed). Each
  action has one rule: the states where it is allowed, the next state and the job to start. It runs
  nothing itself, so all transitions are unit-tested; `finish(outcome)` returns a follow-up job (the
  plan of a Quick run, which always stops at Reviewing).
- `ui::WorkflowBinding` maps commands to actions, starts the job a step needs, feeds job ends back
  and enables every command from `Workflow::can()`. The drive list is locked while a job runs, and
  the selected drive is copied so a refresh cannot change it mid-job. Execute asks for confirmation
  first. In Phase 1 every step is the fake parallel job with step-specific stage names; results come
  from `services/workflow/FakeResults`.
- Pause is a `core::PauseGate` that the execution job checks between items; Cancel still works
  while paused because the gate's wait returns on cancellation.

## Snapshot

- `core::Snapshot` is the in-memory layout of one volume (plan.md 6.2 principle 4): a structure-of-arrays
  `FileTable` with 32-bit indexes, an interned `NamePool`, a `DirectoryTree` of parent indexes (paths
  are rebuilt on demand), an `ExtentIndex` of 16-byte records sorted by LCN with a per-file list in
  VCN order, and a merged `FreeRunList`. About 58 bytes per file and 20 per extent, so the design
  target (20 M files, 60 M extents) needs roughly 3 GB.
- Only `SnapshotBuilder` writes it. Input order never changes the result: extents are sorted by a
  total order key, and children keep file-index order. Bad input (overlaps, runs off the volume,
  unknown files or parents) throws `SnapshotError`; nothing is planned on such data.
- Runs longer than 24 bits are split into consecutive records; `fragmentCount()` rejoins runs that
  continue on disk, so splitting never inflates fragmentation.

## Free space from the volume bitmap

- `platform::readVolumeBitmap()` asks `FSCTL_GET_VOLUME_BITMAP` for pieces of about 32 MB of bitmap (1 TB of 4 KB
  clusters; a 30 TB volume is about 29 pieces) into one reused buffer; `ERROR_MORE_DATA` means another piece
  follows. The full 915 MB bitmap of a 30 TB volume is never held.
- `core::freeRunsOf()` scans 64-bit words: all-free and all-used words cost one compare, mixed words are walked
  with `countr_zero` over the bit changes; bits past the valid cluster count read as used. `freeRunsParallel()`
  cuts a piece into 1 MB grains scanned on the pool and stitched in LCN order (`stitchRuns()` joins runs that
  meet at an edge), so the runs equal one serial pass for any thread count.
- The bitmap's free count equals NTFS's `FreeClusters`; `GetDiskFreeSpaceExW` (and `fsutil volume diskfree`)
  report that minus `TotalReserved`. `VolumeBitmapTest` checks both exactly on every mounted ICD-TEST volume
  (4 KB and 16 KB clusters) and re-reads with 4 KB pieces to exercise the stitching on real data.

## USN journal and file enumeration

- `platform::enumerateUsnFiles()` walks every in-use MFT record with `FSCTL_ENUM_USN_DATA` (V2 records, 64 KB
  batches, in MFT record order): file reference, parent reference, attributes and one name (UTF-8). It works
  without an active journal; a file with hard links appears once, under one of its names.
- `platform::queryUsnJournal()` returns journal ID, first, next and lowest valid USN, or nothing when the volume has
  no journal. Analysis never creates a journal (a write); such volumes simply cannot be caught up later and get a
  full rescan. The test volumes have none; Windows keeps one on its own drive.
- `UsnJournalTest` walks each ICD-TEST volume (not into reparse points), identifies every entry by handle, and
  requires the enumeration to hold it with the same name and parent (single-link files).

## API scanner (reference path)

- `services::scanFilesApi()` (plan.md 10.2): journal cursor first (changes during the scan stay after it), then
  the system records 0-11 under the root, then the `FSCTL_ENUM_USN_DATA` list; each file is opened by its file
  reference (`OpenFileById`, `FILE_READ_ATTRIBUTES`, backup semantics, reparse points not followed) in MFT
  record order, so no paths are built. One handle gives basic and standard info (times, attributes, size, link
  count), the stream list and the main stream's retrieval pointers; a directory handle yields its index
  allocation. Named streams are opened by path (`\\?\T:\dir\file:stream`), the only place a path is built.
- Flags: attributes give directory, compressed, sparse, encrypted, reparse point; link count > 1 hard link (the
  enumeration lists a file once, so it is counted once); named streams; system records and everything under
  `$Extend` are metadata and unmovable; root `pagefile.sys`, `hiberfil.sys`, `swapfile.sys` unmovable; a file
  without clusters that is neither a directory nor sparse is resident. Extents carry directory-index,
  compression-unit and unmovable flags; holes (LCN -1 in sparse and compressed files) are skipped.
- The process enables `SeBackupPrivilege` (administrators hold it switched off) so `System Volume Information`
  opens too. `$LogFile`, `$Volume`, `$Bitmap`, `$Boot`, `$BadClus`, `$Secure` still refuse `OpenFileById`; they
  are listed without extents.
- `core::reconcileScan()` then makes the reads one consistent picture: overlapping extents keep the newer read
  (higher file index), extents take their clusters out of the earlier bitmap's free runs, and every used cluster
  nobody claims becomes an unmovable extent of the `$Unattributed` placeholder file (the unreadable system files,
  locked files, extents dropped because a file changed). Extents and free runs then tile the volume, which the
  snapshot builder requires. Quiet test volumes need no corrections beyond the placeholder.
- Checked on the `special` scenario: every special case flagged, clusters fully accounted for, bitmap free count
  unchanged, and the extents of ~25 sampled files equal `fsutil file queryextents`.

## Analysis

- `services::makeAnalysisJob()` (T048): opens the volume for reading (fails with a clear message without
  administrator rights), reads fresh geometry, then the bitmap. A map of the bitmap alone follows at once
  (`bitmapSnapshot()`: used space owned by one "(files not read yet)" placeholder), so the map shows used and
  free space within a second or two (plan.md 6.2 principle 7). The API scan then reads the files and hands over a
  partial snapshot every 3 s (files read so far; unread used space still belongs to the unmovable placeholder,
  so the map turns from green to file colors as the scan proceeds); each becomes a `MapData` in the job.
  Finally `core::summarize()` gives the Summary figures and the final map is published.
- The API path reads files in MFT order, not by LCN stripe, so the map fills in by file rather than stripe by
  stripe as plan.md 6.2 describes for the MFT scanner (Phase 4).
- `AnalysisOutput` (moved from SyntheticAnalysis) holds only the newest map; the UI takes it on every progress
  event and shows it with `MapPanel::updateMap()`: same world, so the canvas keeps its view and marks every
  tile dirty (the palette-switch path) instead of resetting, and nothing blanks. A cancelled or failed analysis
  clears the map, so a half-read volume never looks analysed.
- `core::VolumeStats`: files, directories, fragmented files and extra fragments, fragmented share of files and
  of file bytes, unmovable clusters, free runs, largest run and the free share in runs of 64 MB or more. The
  Summary tab adds these real-scan rows (`AnalysisSummary::scan`) plus unreadable files and files changed during
  the scan when there are any; the drive row takes the fragmented-file percentage.
- The selected drive's partition offset and disk size feed the row ruler's "% of disk" labels.
- Measured from the GUI (Release): X: (2.3 TB HDD, 112 K files) 14.7 s cold, about 2 s warm; E: (8.6 TB HDD,
  15 K files) 1.5 s including the 2.3 G-cluster bitmap. Cancel during the file scan stops within a fraction of a
  second ("cancelled after 1.7 s").

## icd-cli analyze

- `icd-cli analyze <volume> [--json] [--dump-extents]` (T049) runs `services::analyzeVolume()`, the same function
  the GUI job wraps (the job only adds map building and publishing), so the CLI never builds maps. The volume is
  given as `T`, `T:`, `T:\` or a mount folder and must be NTFS; administrator rights are checked up front.
- Progress goes to stderr on one rewritten line, so stdout carries only the report; Ctrl+C cancels through
  `platform::ConsoleCancel` (the console handler trips the job's cancel source, the analysis stops between
  items). Exit codes: 0 done, 1 failed or cancelled, 2 bad arguments.
- The report (`services/scan/AnalysisReport`): volume facts (sizes, geometry, disk placement, NTFS data, media),
  the scan (seconds, USN cursor, unreadable files, corrections) and `summary`, written from the same
  `AnalysisSummary` the Summary tab shows, so the two agree by construction (checked on the 16 KB-cluster test
  volume: every figure equal). `--dump-extents` adds each file with clusters: path, size, flags and runs as
  `[lcn, clusters, stream]`, records that continue on disk joined. JSON comes from a small writer in
  `core/format/Json` (output only; no JSON library yet).

## Analysis verification (T050, Phase 3 exit)

- `tests/integration/analysis/check-analysis.ps1` builds a fresh 4 GB test volume per scenario, fills it with
  gen-scenario, and remounts it read-only before analysing: a read-only volume refuses every write (the script
  proves it with a write probe), so a passing run shows analysis writes nothing. It then runs
  `icd-cli analyze --json --dump-extents` and checks free space against `fsutil volume diskfree` (after NTFS's
  reserved clusters), that file runs plus free space cover every cluster exactly once, and that the runs of up to
  40 sampled files equal `fsutil file queryextents`. `-RealDrive X` runs the free-space (within 0.1%, the drive is
  live) and extent checks on a real data drive. It is a script, not a CTest, because it needs administrator
  rights and a few minutes.
- Cancel is covered by `AnalysisJobTest.CancelStopsTheScan` and was checked in the GUI (T048).
- Result of the Phase 3 exit run (Release, 4 GB volumes, seed 1):

```
[dev]
    ok    volume mounted read-only (any write would fail)
    63132 files, 4.1% fragmented, 2327 free runs, 5.03 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 40 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[archive]
    ok    volume mounted read-only (any write would fail)
    7 files, 100.0% fragmented, 30 free runs, 0.01 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 6 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[photos]
    ok    volume mounted read-only (any write would fail)
    356 files, 74.6% fragmented, 352 free runs, 0.03 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 40 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[video]
    ok    volume mounted read-only (any write would fail)
    6 files, 100.0% fragmented, 30 free runs, 0.01 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 5 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[mixed]
    ok    volume mounted read-only (any write would fail)
    15867 files, 4.8% fragmented, 976 free runs, 1.24 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 40 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[almost-full]
    ok    volume mounted read-only (any write would fail)
    18956 files, 5.2% fragmented, 450 free runs, 1.54 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 40 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[vss]
    ok    volume mounted read-only (any write would fail)
    8916 files, 11.1% fragmented, 1706 free runs, 0.73 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 40 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[special]
    ok    volume mounted read-only (any write would fail)
    3217 files, 57.1% fragmented, 2098 free runs, 0.07 s
    ok    free space = fsutil volume diskfree + reserved
    ok    extents of 12 sampled files = fsutil file queryextents
    ok    runs + free space = every cluster
    ok    nothing changed during the scan
[real drive X:]
    109466 files, 0.1% fragmented, 3.78 s
    ok    free space ~ fsutil volume diskfree (live drive)
    ok    extents of 40 sampled files = fsutil file queryextents
All analysis checks passed.
```

## Stripes

- A volume is cut into fixed LCN stripes (`core::StripeLayout`): sizes are multiples of 4096
  clusters, 512 stripes on large volumes, fewer on small ones; the cut depends only on the volume
  size. `Stripes` finds each stripe's record and free-run range once, so a `StripeView` is free.
- Records and free runs that cross a stripe edge appear in both views; per-stripe work clips them
  (`clip()`), so every cluster counts once.
- `mapStripes()` returns per-stripe results in stripe order and `reduceStripes()` combines them in
  that order, so results are bit-identical for any executor (checked on 2, 5 and 16 threads).

## Synthetic volumes

- `core::generateSyntheticVolume()` builds seeded snapshots for tests and benchmarks: a directory tree
  (each directory with a small index allocation), files of a class mix with log-normal sizes scaled to
  the fill level, the $MFT as an unmovable metadata file, fragmented files whose later pieces land a
  random number of pieces further on (so files interleave), and free gaps by pattern (scattered,
  tail, mixed). Names repeat per class, as on real volumes, so the name pool stays small.
- Its random source is self-contained (`SynthRandom`): std distributions differ between standard
  libraries, and a seed must give the same volume everywhere.
- Measured with `synth-bench` (Release, clang-cl, this machine): 30 TB / 20 M files with 30% of files
  fragmented, up to 40 pieces, builds in 29 s into 55.7 M extents and 16.6 M free runs; snapshot data
  2.6 GB, peak working set 5.6 GB, within the 8 GB budget. Building is serial; the sort of the pending
  extents dominates.

## Map world

- `core::World` lays clusters out row by row with row width `roundToNice(sqrt(N * aspect))`, fixed
  once from the screen aspect (plan.md 7.1). Nice widths keep at most four significant binary
  digits, so the row width is a multiple of a large power of two (8192 for 30 TB at 16:9) and
  pyramid bins up to that size never wrap a row; the width is within 1/16 of the ideal.
- Zoom level `L` means one pixel is `2^L` cells across: `-5` is 32 px per cluster (the deepest
  zoom), `maxLevel()` fits the world in one 256 px tile. `levelFor()` picks the nearest finer level
  for a continuous scale, so cached tiles are shrunk, never blown up.

## Map pyramid

- The pyramid is a quadtree over the world grid, not a 1-D list of LCN bins: a level-k bin covers
  2^k x 2^k cells, exactly one pixel of a zoom-level-k tile, so coarse tiles are copied straight from
  bins. (A 1-D bin of 2^k clusters cannot summarise a square pixel spanning many rows; at the
  whole-volume zoom of a 30 TB drive a pixel is about 32 x 32 cells.) The plan's "256 clusters per
  bin, ~32 M bins" at 30 TB is the 16 x 16 base level of this quadtree.
- `MapBin` is 8 bytes: used clusters in 24 bits (scaled by `usedShift()` once a full bin no longer
  fits) sharing a word with the size value, dominant class (largest contributor at the base; the child
  with most used clusters above), fragmented share, heat and size as 0-255 averages weighted by used
  clusters, and flags (unmovable, directory index, plan source/destination). 24 bits of count are far
  more than any palette shade needs; the size mode needed the byte.
- The base is filled in parallel per band of base-bin rows; each band is one contiguous LCN range,
  so it needs a single range query and one sweep. Levels above merge 2 x 2 bins. Results do not
  depend on the thread count; `update(dirty)` recomputes only the bands over a changed LCN range
  and their parents, and equals a full rebuild.
- Measured on the synthetic 30 TB / 20 M-file volume: 0.86 s on 8 threads, 0.55 s on 32
  (target 5 s on 8), 320 MB for all levels.

## Tile rasterizer

- `renderTile()` draws 256 x 256 BGRA tiles. Levels the pyramid stores copy one bin per pixel; finer
  levels summarise each pixel (or each cell, past one cell per pixel) from the extent index with the
  same `BinAccumulator`, so both paths give identical pixels at the base level (tested). Tiles carry no
  grid lines (see Grid lines below).
- Color modes are `Palette` implementations over a `PixelSummary` (bin, level, capacity); see Color
  modes and legend below.
- Speed: per-record owner flags (`extentMapFlags()`) and color values (`extentColorValues()`), one
  byte each per extent (~55 MB each at 30 TB), are read in record order instead of random lookups into
  the 20 M-entry file columns. Each tile row starts from a coarse LCN index (`recordBlocks()`: first
  record per 2,048-cluster block, 14 MB at 30 TB) and gallops from there, instead of galloping ~1,000
  records from the previous row. A piece adds its partial end units, then whole units in a tight loop
  (`BinAccumulator::add()` is inline). Runs of identical unit totals reuse the previous color without
  finishing again; unit capacity is a multiplication except at the volume's edges; scratch buffers
  are per thread (fresh multi-MB blocks cost a page fault per 4 KB).
- Measured by `map-bench` (T033) on the 30 TB synthetic volume, one thread per tile, worst of the
  three color modes: coarse levels 4-9 at 0.65-0.8 ms (target 5 ms); fine level 3 1.5 ms (median 1.8),
  level 2 0.8 ms, levels 1 and below 0.2-0.5 ms (target 2 ms).

## Map rendering backend (T026 spike)

- Measured with `map-render-spike` on a 3840 x 2160 canvas (a child window, so the 4K target is real
  even on this machine's 2560 x 1440 screens), RTX 4070, 30 TB synthetic volume, tiles pre-rendered,
  scripted zoom from the whole volume to 8 px per cluster with panning, ~290 tiles per frame:

  | Backend | avg | p95 | max | sustained |
  | --- | --- | --- | --- | --- |
  | Direct2D on the HWND (`ID2D1HwndRenderTarget`, one GPU bitmap per tile) | 0.29 ms | 0.49 ms | 15.8 ms | 3,390 fps |
  | wx Direct2D renderer (context per frame, `wxGraphicsBitmap` per tile) | 54.7 ms | 66.5 ms | 69.7 ms | 18 fps |
  | GDI `StretchDIBits` | 1.89 ms | 3.28 ms | 8.9 ms | 528 fps |

- **Chosen: Direct2D directly on the map control's HWND.** wx's renderer re-creates the device
  bitmaps of every tile for each new graphics context (each paint), which makes it ~190x slower and
  far from 60 fps. GDI looks fast here only because it draws just the visible part of the oversized
  canvas and scales on the CPU; it stays the fallback for machines without Direct2D hardware.
- The map control (T027) keeps one render target and one `ID2D1Bitmap` per cached tile, uploads new
  tiles as workers finish them, and draws with nearest-neighbour scaling. Frame pacing comes from the
  present (vsync) rather than from `PRESENT_OPTIONS_IMMEDIATELY` used in the spike to measure
  throughput. A DXGI swap chain is only needed if the HWND target cannot pace frames well.

## Map control

- `core::MapData` bundles the snapshot, its fixed world, the pyramid and the extent flags. It is
  immutable and shared: the canvas and every worker rendering a tile hold a `shared_ptr`, so a tile
  still rendering keeps its data alive after the user moved to another drive.
- `ui::MapCanvas` paints with `ui::D2DRenderer` (Direct2D on its HWND, the T026 choice). The paint
  path only draws cached GPU tiles and asks for missing ones; it never touches analysis data. Missing
  tiles render on the shared pool (`Executor::submit`) and come back through a `TileMailbox` (the
  canvas detaches it in its destructor; `CallAfter` only queues an event the window drops if
  destroyed). A generation number drops results for an earlier map or lost device.
- Uploaded tiles live in a `core::LruCache` capped at 256 MB of GPU memory. Lost devices
  (`D2DERR_RECREATE_TARGET`) drop every tile and recreate the target on the next paint.
- The view is a scale (cells per pixel) and a world center. A new map is fitted once the window has
  a real size; afterwards resizing keeps scale and center, so the world never re-flows (plan.md 7.1).
- With the fake drive list (`ICD_FAKE_DRIVES=1`), Analyze runs `services::makeSyntheticAnalysis()`: a
  synthetic volume shaped like the fake drive (size, fill, fragmentation) plus its `MapData`. Measured: the
  4 TB fake drive in 2.1 s, the 30 TB one (20.5 M files, 48.9 M extents, 384 MB map data) in 16.6 s; the
  fitted view's tiles arrive within 40 ms. Real drives run the analysis job (see Analysis below).
- `PrintWindow` does not capture Direct2D output of a child window; scripted screenshots of the map
  need a real screen capture.

## Map navigation

- View math is pure core code (`core/map/Viewport`): a continuous scale (cells per pixel) and a world
  center; zoom keeps the world point under the anchor pixel fixed; limits run from 32 px per cluster
  to 1.5 times the fitted view; the center stays inside the world.
- Motion is computed from elapsed time (`ZoomMotion` eases in log-scale space, 150 ms per wheel step up
  to 1.2 s for big jumps; `PanInertia` decays exponentially), so timer jitter never shows. An 8 ms
  `wxTimer` only computes the next view and asks for a repaint; Direct2D's present waits for vsync and
  paces the frames.
- `ui::MapNavigator` maps input: drag pans (with inertia on release), the wheel zooms at the pointer
  (fractional notches from precision touchpads zoom smoothly; the horizontal wheel pans), pinch and
  two-finger pan gestures, double-click zooms 4x in (Shift: out), arrows/PgUp/PgDn/Home/End pan, and
  the View commands animate zoom in/out, fit and one cluster per pixel. A new zoom continues from the
  current animated view towards the combined target.
- While exact tiles render, the nearest cached tile up to four levels coarser is drawn scaled. At most
  96 tiles render at once and at most 16 are uploaded per frame, so bursts never stall a frame.
- Measured on the 30 TB synthetic volume at 2400 x 1400 on a 75 Hz display: zoom from the whole volume
  to one cluster per pixel at 71.8 fps (p95 16.1 ms, worst 24 ms); other zooms 72-82 fps (target 60).
  Drag, inertia, wheel and pinch were not exercised by script (they need a real pointer).

## Minimap and rulers

- The minimap is drawn inside the map canvas's own Direct2D frame, not as a child window over the GPU
  surface. Its image is the coarsest tile (the whole world fits in one), kept by `ui::Minimap` outside the
  LRU cache so deep zooms cannot evict it. It hides when the window is less than three minimaps wide or
  high, and with View > Minimap (Ctrl+M; the panel owns the state and the menu check follows it).
- Pressing on the view marker drags it with the grab offset kept; pressing elsewhere on the minimap jumps
  the view there and keeps dragging. Wheel zoom over the minimap anchors at the window center.
  Placement, marker and drag math are `core::Overview` (tested).
- Rulers are plain GDI windows beside the canvas (left: row starts with byte offset and % of volume;
  top: offset within a row), in a 2 x 2 grid so their pixel axes match the canvas. The canvas calls a
  view observer on every view or map change; the panel turns it into ruler refreshes. Tick math is
  `core::rowTicks()` / `columnTicks()`: power-of-two steps (labels stay round in binary units) at least
  one label apart, labels in a unit that writes the step exactly with at most three decimals ("704 GB",
  not "0.688 TB"). "% of disk" appears once the scanner supplies the partition placement (Phase 3).
- The 30 TB zoom from the whole volume to one cluster still runs at 73 fps with the rulers repainting
  every frame.

## Hover tooltip

- `core::hitTest()` turns a window point into the cell, its LCN and the extent holding it (one binary
  search), the fragment i/n of the owner (split runs that continue on disk count once, as in
  `fragmentCount()`) or the free run, and the pixel's coverage: at coarse zoom a pixel stands for many
  clusters, so the tooltip also says how many and how much of them is used (one range query per covered
  row). `describeHit()` makes the lines, so the wording is tested; file times use the local zone from
  `std::chrono::current_zone()` (UTC if the zone database is unavailable).
- Until classification (Phase 5) the tooltip's type line comes from file flags (file, directory, NTFS
  metadata, plus compressed, sparse, unmovable, ...).
- `ui::MapHover` shows a `wxPopupWindow` (never takes focus) after the pointer rests 350 ms, then follows
  it; a press, drag, wheel, key or leaving the map hides it. It is bound after the navigator so it sees
  events first, and always passes them on. Posted mouse messages cannot drive it from a script: wx
  sends a leave event when the real cursor is not over the window.

## Grid lines

- Grid lines were first baked into deep-zoom tiles (1 px on cells of 8 px and more). Tiles exist only at
  power-of-two zooms and are drawn shrunk with nearest-neighbour sampling between them, so a 1 px line
  was dropped or kept depending on where it fell and the grid flickered while zooming and panning.
- Now the canvas draws the grid every frame at screen resolution (`core::gridLines()`): a 1 px line on
  the last pixel of each visible cell across and down, found with the same rounding nearest-neighbour
  drawing uses (a cell starts at the first pixel whose center is at or past its edge), so lines always
  sit on the drawn cell borders and are always exactly one pixel. Lines stop at the last cluster.
- The grid fades in with the on-screen cell size (smoothstep from 5 to 10 px), so it never pops in or
  out at a zoom step. All lines are filled in one Direct2D layer at that opacity, so crossings are not
  brighter. At most a few hundred rectangles per frame (window size / 5 px each way).
- The same aliasing hit details smaller than a pixel: where a cell is under a pixel, a tile shrunk with
  nearest-neighbour sampling drops whole texel rows and columns, so single clusters blinked in and out.
  From one cell per pixel on, the canvas draws the next coarser level (its pixels already average their
  clusters) and fades the finer level over it by how far the scale lies between the two, both filtered
  linearly (`World::blendFor()`, mipmapping). Where cells are a pixel or more, one level is drawn crisp
  with nearest-neighbour sampling: nothing is lost there.
- The render target draws without anti-aliasing (pixel-center rule): tiles meeting at a fractional
  position share the edge exactly instead of leaving a light seam.

## Selection and go to file

- A click (press and release within 4 DIP, not on the minimap) selects the file owning the cluster under
  the pointer and makes the clicked fragment current; a click on free space clears the selection.
  `ui::MapSelection` keeps the fragments in file (VCN) order, numbered like the tooltip's i/n, and moves
  the view with `core::focusView()`: centered, the fragment filling half the window (whole rows across
  90% when it wraps), never closer than 8 px per cluster. F3 / Shift+F3 step through the fragments.
- The highlight is part of the tile: workers paint `core::highlightRuns()` over the rendered tile, so
  every pixel holding a selected cluster shows it, even a one-cluster fragment at the widest zoom.
  Rendered tiles do not depend on the selection otherwise. On a change, the canvas tests each cached and
  rendering tile with `core::runsTouch()` against the old and the new runs and marks only those dirty
  (with a selection version); a dirty tile is still drawn and re-requested, and the mark clears when a
  tile drawn with that version or newer arrives. Nothing flickers and untouched tiles stay cached. Tests
  check that the refreshed tiles are exactly the tiles whose pixels change.
- "Go to file" (Ctrl+F, or Enter in the toolbar's "Find file") is a dialog: `core::searchFiles()` runs on
  the pool as the user types, ranks exact names, then prefixes, then other matches, and lists up to 500.
  Without a backslash the query matches names; with one it matches path pieces (the last part starts the
  name, the rest ends the parent's path, compared name by name up the parent chain without building
  strings). Each distinct name is tested once, then the 4-byte name column is scanned.
- Measured on the 30 TB synthetic volume (20.5 M files, 399 K distinct names, 32 threads): name searches
  3-4 ms, a path search 19 ms; a selection change's dirty test over a full 1,024-tile cache 0.05 ms.

## Color modes and legend

- `core::ColorMode`: fragmentation (`OccupancyPalette`: free, contiguous, fragmented, unmovable), age
  (time since the owning file's last change, from under a day to three years or more) and size (under
  64 KB, then 16x steps to 4 GB or more). File class, zone fit and plan preview come with their features.
- Age and size are six buckets each, one byte per file (`fileColorValues()`) as `bucket * 51`; the pyramid averages them per bin like the fragmented share, and the
  palette shows the nearest bucket's color blended with free space by the used share. A cell of one
  file keeps its exact bucket, so deep-zoom colors equal the legend colors (tested).
- Ages are measured from the newest modification in the snapshot (`newestModification()`), so a
  snapshot shows the same ages whenever it is drawn; heat is stored hot-high (new = 255) so later
  activity signals (plan.md 8.2) can feed the same byte.
- Switching modes changes only the palette: the canvas marks every cached and rendering tile dirty
  (the selection-change path), keeps showing them until each re-renders, and never rebuilds map data.
- The legend (`ui::Legend`, a strip under the map, View > Legend / Ctrl+L) counts clusters per entry
  exactly from the extent index (`countLegend()`): the visible cell rectangle becomes one LCN range
  for whole rows or one per row, cut into 4 M-cluster pieces summed in parallel; equal to a per-cluster
  count (tested). It runs on the pool 150 ms after the view rests, cancelled by a newer view or mode,
  and keeps the old numbers until the new ones arrive. Whole 30 TB volume: ~85 ms.
- The per-file values feed the pyramid build only; the map keeps them per extent record
  (`extentColorValues()`, packed heat and size steps in one byte), which tiles read in record order.
  Per-file lookups had slowed level-3 tiles from 4.4 to 6.5 ms.

## Map performance (T033, Phase 2 exit)

- `map-bench` builds the 30 TB / 20 M-file synthetic map the way the app does (`MapData::build`),
  checks the pyramid on 8 threads, map memory and tile times at every level in every color mode, then
  drives the real `MapCanvas` (compiled into the bench) in a 1920 x 1080 window: zoom from the whole
  volume to 8 px per cluster and back, and pans at 2,000 px/s at 16 clusters per pixel, one cluster
  per pixel and 8 px per cluster.
- The bench turns off power throttling for itself. A background console process otherwise gets
  EcoQoS and, on this hybrid CPU, efficiency cores: single-thread tile times doubled and swung from
  run to run. The app in the foreground is not throttled.
- Results (RTX 4070, 32 threads, 120 Hz display), all plan.md 7.5 targets met:

  | Check | Result | Target |
  | --- | --- | --- |
  | Pyramid build, 8 threads | 0.74-0.87 s | 5 s |
  | Map memory | 441 MB (pyramid 320, flags 55, color values 55, block index 14) | 512 MB |
  | Coarse tile (levels 4-9) | 0.65-0.8 ms | 5 ms |
  | Fine tile (levels 3 to -5) | 1.5 ms at level 3, 0.2-0.8 ms below | 2 ms |
  | Zoom, whole volume to 8 px/cluster and back | 119 fps, p95 10.8 ms, max 17.7 ms | 60 fps |
  | Pans at three zooms | 120 fps, p95 <= 10.8 ms, max 13.4 ms | 60 fps |

  Frames run at the display rate (Direct2D presents with vsync). 2-7% of frames show a coarser
  stand-in for some tiles while the exact ones render.

## Test bed

- Test volumes are dynamic VHDX files made by `tools/testvol/make-testvol.ps1` through diskpart (always
  present; the Hyper-V cmdlets are not), GPT with one NTFS partition of the chosen cluster size. The Storage
  cmdlets (`Get-DiskImage`, `Set-Partition`) only find the attached disk and fix its drive letter. Files live
  outside the repository (`%LOCALAPPDATA%\IroncladDefrag\testvol` or `ICD_TESTVOL_DIR`).
- A read-only attach (`-ReadOnly`) is how analysis proves it writes nothing: the volume refuses writes.
- `verify-testvol.ps1` manifests hash main streams and alternate streams with .NET (ADS paths work in
  PowerShell 7), record reparse targets instead of following them, and skip `System Volume Information`
  and `$RECYCLE.BIN`, which change on their own. Verify adds `chkdsk /scan` (exit 0 = clean). Entries are hashed
  in batches of 500 per parallel runspace (per-item `ForEach-Object -Parallel` cost dominated 56 K-file volumes).
- `gen-scenario` (C++, `tools/testvol/gen-scenario`) writes the plan.md 12 scenarios. All decisions and file bytes
  come from `core::SynthRandom` (per-file content seeds), and writes run on one thread in a seeded order, so a seed
  gives the same files, sizes and bytes every time. Fragmentation comes from how real drives get it: several files
  growing at once in small appends (`WriteMode{streams, chunk}`), deletes that leave holes for later files, and
  files appended to long after they were created (packs, logs, catalogs).
- The physical layout is only nearly reproducible: on scenarios with tens of thousands of small files (`dev`,
  `mixed`, `almost-full`, `vss`) NTFS places a few files differently from run to run (e.g. 2,385 vs 2,388
  fragmented files), timing inside NTFS (lazy writer, metadata) the generator cannot control. `special`,
  `archive`, `photos` and `video` reproduce their fragment counts exactly. Tests therefore compare a scan with the
  volume it scanned, never with a stored layout.
- `almost-full` measures the volume before its fill phase (cluster slack and NTFS metadata add about 3% on 4 GB that
  file sizes miss) and rounds that budget down to 16 MB, so the measurement noise does not change the files.
- Safety: it refuses the Windows drive, non-NTFS volumes and volumes whose label does not start with `ICD-TEST`
  (unless `--force`).
- On fresh 4 GB volumes (seed 1): `special` 1,324 fragments in one file (attribute list), compressed, sparse, links,
  junction, ADS, EFS, resident and a 3,000-entry directory; `dev` 4% of 56 K files fragmented; `photos` 75% of 355;
  `archive`, `video` every big file fragmented; `almost-full` 96.8% used; `vss` creates a shadow copy through
  `Win32_ShadowCopy.Create` and changes files after it.

## Parallel execution

- Core algorithms take a `core::Executor&` and never create threads. Services owns one
  `ThreadPool` sized from `ResourceCaps::workerThreads`; tests use `InlineExecutor` as the reference.
- `parallelFor(ChunkedRange{count, grain}, body)` cuts the range by `grain` only, so the work split
  never depends on the thread count; `parallelReduce` combines per-chunk results in chunk order. This
  is how parallel runs give bit-identical results to serial ones (plan.md 6.2.8).
- The pool: one deque per worker (owner takes the newest, thieves the oldest), a condition variable
  for idle workers, `std::jthread` stop tokens for shutdown. The thread calling `parallelFor` claims
  chunks too, so nested loops from pool tasks cannot deadlock. Cancellation stops new chunks; running
  chunks finish, then `OperationCancelled` is thrown. Pending tasks are dropped when the pool is
  destroyed, so job owners cancel and wait first.

## Resources

- `platform::detectSystemResources()` reads what this process may use (processors over all groups,
  narrowed by a single-group affinity mask and a hard job-object CPU rate; RAM narrowed by job memory
  limits). `core::computeCaps()` turns that plus the user's settings into the worker count and memory
  budget, each with a `CapReason` for the UI. Caps change speed only, never results (plan.md 6.2.8).
- Auto: workers = min(32, processors - 1, >= 1); memory = min(8 GB, 60% of available RAM). Manual
  values are capped to what was detected. The startup log records both.

## Quality gate

- Lizard and clang-tidy, pinned in a Python venv, so results do not depend on the installed LLVM.
- clang-tidy uses its own Ninja + clang-cl configure in `build/quality` with precompiled headers off:
  the pinned clang-tidy (22) cannot read `.pch` files from the installed clang-cl (23). It runs in cl
  driver mode inside the vcvars64 environment, which supplies the STL and SDK include paths.

## Third-party

- `tools/dev-env.cmd` clears `PSModulePath`: a cmd started from PowerShell 7 passes its module path
  on, and Windows PowerShell then fails to load cmdlets (`fetch-wx.ps1` also hashes with .NET).
- wxWidgets is used as prebuilt DLLs only (no source build). `tools/fetch-wx.ps1` is the only way the
  tree under `3rdparty/wxWidgets` is produced, so every machine builds against the same pinned files.
