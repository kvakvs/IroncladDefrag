# IroncladDefrag Development Plan

## 1. Goal

A Windows x64 GUI disk analyser and defragmenter for **mechanical (HDD) NTFS data drives of 4-30 TB**.
The whole design targets HDDs. It has:

- a very detailed, Google-Maps-style drive map: pan, wheel/keyboard zoom, smooth zoom from the whole
  volume down to a single cluster;
- placement strategies tuned to what the drive stores (development trees, archives and disk images,
  photos, video);
- plans that move as little data as possible and are always shown before anything is moved;
- scanning, map building and planning spread across all CPU cores, because these drives hold billions
  of clusters (section 6).

SSDs only ever need TRIM. ICD lists them and offers one "TRIM" action that calls Windows. There is no
analysis, map or strategy for SSDs, and no design effort goes into them beyond that.

The window layout: top menu, all-disks panel, drive map panel, bottom controls and info panel.

## 2. Ground Rules

- Stay on wxWidgets 3.3.x. The build and app start must work reliably (Debug and Release, DLLs copied)
  before any feature work.
- Core logic lives in a wx-free library with tests from Phase 0. Each phase ends only when its exit
  criteria have been checked.
- The UI shell and workflow are built with fake data first (Phase 1) and agreed before real features
  go in, so panels, tabs and status are not duplicated.
- A plan is always built and shown first; execution needs explicit confirmation. There is no separate
  "dry-run mode".
- Scanning has a simple API path for correctness and a parallel raw MFT reader for speed, checked
  against each other.
- Snapshots are saved and caught up from the USN journal; a full rescan happens only when needed.
- The map is a zoomable tile engine with a level-of-detail pyramid (Phase 2), not a fixed grid.
- No move runs on a real drive before it has passed on disposable VHDX test volumes.

## 3. Key Facts From Research

These drive the design. Sources are listed at the end.

1. **Outer tracks are faster.** With zone bit recording, low LBAs (start of the disk) sit on outer
   tracks, which read roughly 1.8-2.4x faster than the innermost tracks. "Front" = fast, "back" = slow.
   LCNs are volume-relative, so a partition's offset on the physical disk must be taken into account.
2. **Fragments of 64 MB or more barely matter.** Windows' own defragmenter ignores fragments larger
   than 64 MB by default: moving them costs more time than they lose. This is the default
   "good enough" rule for large files.
3. **Shadow copies (VSS) are sensitive to moves.** Copy-on-write works on 16 KB blocks. A move whose
   source and destination differ by a multiple of 16 KB (and is at least 16 KB long) does not grow the
   shadow storage. Other moves can grow it until the oldest restore points are deleted. With 4 KB
   clusters: keep `source LCN mod 4 == destination LCN mod 4`.
4. **`FSCTL_MOVE_FILE` rules.** Needs administrator rights and a volume handle. Supports `$DATA`,
   `$ATTRIBUTE_LIST`, `$REPARSE_POINT`, `$EA`, `$LOGGED_UTILITY_STREAM` on any file,
   `$INDEX_ALLOCATION`/`$BITMAP` on directories, and a fixed list of NTFS metadata streams (including
   `$MFT`). Everything else is unmovable. The volume bitmap can be out of date: a move can target a
   cluster that has just been allocated, and the code must handle that failure. Don't move clusters
   beyond the cluster-rounded file size.
5. **Last-access time is often off.** On Windows 10/11 it is "system managed" and usually disabled
   on volumes over 128 GB (`fsutil behavior query disablelastaccess`). Hot/cold rules cannot rely on
   it alone. They should also use modification time, path patterns and the USN change journal.
6. **The MFT zone.** NTFS keeps a reserved region (12.5% by default) after `$MFT` so the MFT can
   grow. User data goes there last. Treat `$MFT` and its zone as a fixed obstacle; don't fill the zone.
7. **Fast scanning.** WizTree and Everything read `$MFT` directly from the volume, which is orders of
   magnitude faster than walking directories. `FSCTL_ENUM_USN_DATA` alone gives names but not sizes.
   Both need administrator rights.
8. **Proven layout (JkDefrag/MyDefrag).** Directories first, then regular files, then large
   "space hogs", with gaps of about 1% of the volume between zones so temporary and new files land
   in fast, contiguous free space. Its author argues against pushing space hogs to the very end. Here
   the user wants video at the back, so the plan does that but packs it contiguously against the end
   (no wasted gap behind it).
9. **SSDs only need TRIM.** Windows uses `ReTrim` for SSDs (`defrag /L`, `Optimize-Volume -ReTrim`).
   ICD calls that and does nothing else on SSDs.
10. **HDD I/O must stay sequential.** One sequential stream reads at 150-280 MB/s. Parallel random
    reads make the head seek back and forth and are much slower. Parallelism goes into CPU work
    (parsing, sorting, building, planning) fed by one large sequential reader, not into concurrent disk reads.

## 4. Scope

**Platform:** Windows 10 22H2 or later, x64; designed and measured on 8-core machines. The app always
runs elevated (manifest `requireAdministrator`), because scanning, the volume bitmap and moves all need
administrator rights.

**In scope (v1):** NTFS on fixed HDDs of 4-30 TB (smaller ones work too); a TRIM button for SSDs that
calls Windows; the drive map; the five strategies in section 8 plus the general ones listed in `AGENTS.md`;
a test bed on VHDX volumes; an NSIS installer.

**Out of scope (v1):** any SSD analysis, map or strategy; analysing or moving on FAT/exFAT/ReFS;
boot-time defrag of locked system files
(pagefile, hiberfil, `$LogFile`); moving `$MFT` itself; RAID/Storage Spaces/dynamic disks (analyse,
warn, no moves); a globally optimal layout; unattended scheduled runs.

## 5. Architecture

```
src/
  core/          wx-free, Win32-free C++23: model, classification, strategy, planner,
                 plan validator, map LOD pyramid and tile rasterizer
  platform/win/  narrow Win32 wrappers: volumes, storage properties, MFT reader,
                 USN journal, bitmap, retrieval pointers, move, privileges
  services/      jobs, cancellation, analysis/plan/execute orchestration, progress
  ui/            wxWidgets windows, map widget, panels, dialogs
  cli/           icd-cli: analyze / plan / execute / verify, JSON output
tests/           unit + property tests for core; integration tests on VHDX
tools/           VHDX scenario generator and verifier scripts
```

CMake targets: `icd_core` (static lib), `icd_platform` (static lib), `icd_services` (static lib),
`IroncladDefrag` (GUI exe), `icd-cli` (console exe), `icd_tests`.

Rules (in addition to `AGENTS.md`):

- `core` is pure and deterministic: the same snapshot and settings always give the same plan.
- The CLI and the GUI call the same services. The CLI is used to test strategies and planning on
  real volumes without the GUI.
- Long jobs run on worker threads with cancellation tokens. Progress reaches the UI through
  `wxQueueEvent`/`CallAfter`, throttled to about 10 Hz.
- 64-bit cluster numbers in all interfaces. Data structures are sized for 30 TB volumes (section 6).
- `core` has a shared work-stealing thread pool (`services` owns it, `core` takes it as a parameter,
  so tests can run single-threaded and compare results).

Dependencies:

- wxWidgets 3.3.x prebuilt MSVC x64 (Dev + ReleaseDLL), downloaded by a script into `3rdparty/`
  (git-ignored), with the version pinned in one CMake variable.
- GoogleTest through CMake `FetchContent`, version pinned.
- A small JSON library (for example nlohmann/json, pinned) for the CLI output and settings.

## 6. Scale And Parallel Processing

### 6.1 The numbers

| Volume (4 KB clusters) | Clusters | 1-bit bitmap | Typical files | Typical extents | `$MFT` size (1 KB records) |
|---|---|---|---|---|---|
| 4 TB | 1.0 billion | 122 MB | 0.5-5 M | 1-15 M | 0.5-5 GB |
| 12 TB | 2.9 billion | 366 MB | 1-10 M | 2-30 M | 1-10 GB |
| 30 TB | 7.3 billion | 915 MB | 1-20 M | 3-60 M | 1-20 GB |

Design targets: **30 TB, 20 M files, 60 M extents**. Video- and archive-heavy drives sit at the low end
of the file counts; development drives at the high end.

### 6.2 Principles

1. **One disk reader, many CPU workers.** Every disk read (MFT, bitmap) is a large (8-16 MB),
   sequential, overlapped read with 2-4 requests in flight, issued by one reader thread. Buffers go
   to worker threads through a bounded queue. The disk is the bottleneck; workers must keep up with it.
2. **Partition by LCN stripes.** The volume is cut into fixed stripes (e.g. 256-1024 per volume). Each
   stripe owns its free runs, extent slice, map bins and planning state. Most work runs per stripe in
   parallel; a short sequential step stitches stripe boundaries.
3. **Thread-local buffers, then merge.** Workers append to their own vectors and merge at the end with
   a parallel sort. No shared locks on hot paths.
4. **Compact data.** Structure-of-arrays, 32-bit file indexes, an interned name pool, and
   directories stored as a parent-index tree instead of full path strings. An extent record is 16 bytes
   or less (40-bit LCN, 24-bit length with very long runs split, 32-bit file index, flags).
   Memory is not the main limit: up to **8 GB** for the snapshot at the design target is acceptable,
   capped to what the machine actually has (6.6).
   Where extra memory buys speed (per-stripe indexes, cached paths, lookup tables), spend it.
   Compactness still matters for cache efficiency.
5. **Never hold a full bitmap.** Read the bitmap in chunks and convert each chunk to free runs in
   parallel. Stitch runs that cross chunk edges.
6. **Avoid full rescans.** Save the snapshot (compressed) with the USN journal ID and USN at scan time.
   Next time, read the journal from that USN and re-read only the files it names, using the same
   parallel pipeline on a smaller set. A full rescan is needed only if the journal ID changed, the journal
   wrapped past the saved USN, or the user asks. During execution the snapshot is updated from each
   move's verified result, so no rescan is needed after a run.
7. **Show results early.** The bitmap is read first (seconds), so the map shows used and free space
   almost at once. File ownership and colors fill in stripe by stripe while the MFT is parsed.
8. **Deterministic results.** The parallel output must equal the single-threaded output: same sort
   keys and tie-breaks, and per-stripe results merged in LCN order. Tests run both and compare.

### 6.3 Parallel scan pipeline

```
reader thread:  $MFT extents (sorted by LCN), skipping ranges unused per $MFT::$BITMAP
                -> 8-16 MB buffers -> bounded queue
parser workers: fixups + parse FILE records (independent per record)
                -> thread-local file rows, name pool chunks, extent rows
merge:          parallel sort extents by LCN -> extent index; split into stripes
paths:          resolve parent references (directory table first, then files in parallel)
bitmap:         chunked FSCTL_GET_VOLUME_BITMAP -> parallel chunk -> free runs -> stitch
stats:          per-stripe fragmentation, free-space and class statistics -> reduce
```

`$ATTRIBUTE_LIST` records that point to other MFT records are handled in a second, smaller pass after
the main parse.

### 6.4 Parallel planning

1. **Target layout** (sequential, fast): sum each zone's bytes and resolve zones to LCN ranges.
2. **Candidate filter** (parallel per file): drop "good enough" and churn files (9.3 rule 3). On
   typical drives this removes most files before any expensive step.
3. **Per-level planning** (parallel per effort level when the levels are compared, 9.2.2).
   **Per-zone planning** (parallel per zone, and per stripe inside large zones; hot spots in parallel at Fast level): LIS for sorted zones,
   fast-fill, defragment-only. Each zone produces its intake (files coming in), evictions (files going
   out) and the moves within the zone.
4. **Reconcile** (sequential, works on aggregates only): match evictions with free capacity in target
   zones, decide the order in which zones are processed, reserve scratch space.
5. **Destination assignment and ordering** (parallel per stripe): assign exact LCNs from the stripe's
   free runs with VSS alignment, then order moves so each destination is free when its move runs.
6. **Validation** (parallel per stripe, plus a sequential ledger for moves across stripes).

Execution stays single-threaded for disk I/O, since NTFS serialises moves on a volume and parallel moves
would make the head seek. A helper thread prepares the next moves (re-reading retrieval pointers,
checking the snapshot) while the current one runs.

### 6.5 Scale targets (8-core machine, HDD at about 180 MB/s sequential)

These numbers are measured on 8 cores with at least 16 GB of RAM. On smaller machines the app caps
itself to what is available (6.6) and runs slower, but it still finishes with the same results.

- Bitmap read and free runs for 30 TB: 15 s or less; map shows used/free by then.
- Full MFT scan, 20 M files: bounded by reading `$MFT` sequentially (about 2 min for 20 GB); CPU parsing
  must keep the disk 90% or more busy.
- USN catch-up after a typical day of changes: 10 s or less.
- Plan for 30 TB / 20 M files: 60 s or less; at least 5× faster on 8 cores than on 1.
- Snapshot memory: 8 GB or less at the design target.

A synthetic volume generator in `core` produces 30 TB / 20 M-file layouts so that scaling is tested
without real drives.

### 6.6 Resource detection and caps

The 8 GB and 8-core figures are targets, not requirements. At startup, and again before each job,
the app detects what the machine has and caps itself to it.

**Detection**

- CPU: logical processors available to the process across all processor groups
  (`GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`, the process affinity, and any job-object CPU
  limit), plus physical core count (`GetLogicalProcessorInformationEx`).
- Memory: total and available physical memory (`GlobalMemoryStatusEx`), and any job-object memory limit.
- Memory pressure while running: `CreateMemoryResourceNotification(LowMemoryResourceNotification)`.

**Caps (defaults, all "Auto")**

- Worker threads = `min(target 8 or more, available logical processors − 1)`, at least 1. One
  processor stays free for the UI thread and the disk reader. With many cores, more workers are used
  as long as they help (the MFT parse is limited by the disk anyway).
- Memory budget = `min(8 GB target, 60% of available physical memory at job start)`, never less than
  what the job needs at minimum (below).
- Reader buffers, queue depth, tile cache size and stripe count scale with these caps.
- Settings can lower or raise both caps by hand ("Auto" stays the default). A manual value above
  what was detected is capped to the detected value, with a note.

**Predicting before scanning.** The MFT record count (from `FSCTL_GET_NTFS_VOLUME_DATA`) and the
cluster count give a good estimate of snapshot size before any scan starts. The app picks a memory
mode up front instead of discovering the problem halfway.

**What gives way when memory is short**, in this order:

1. Speed-only extras: cached full paths, per-stripe lookup indexes, extra planning tables. They are
   rebuilt on demand instead.
2. Map: smaller tile cache, and a coarser first stored pyramid level. Fine levels are still rasterised
   from the extent index, so zooming down to a cluster still works.
3. Level comparison (9.2.2): the three effort levels are planned one after another instead of in parallel.
4. Last resort: the extent index and name pool move to memory-mapped files in
   `%LOCALAPPDATA%\IroncladDefrag\cache`, and planning works stripe by stripe. Slower, but it finishes.

If memory pressure is signalled during a job, the app drops items in the same order without stopping
the job.

**Same results.** Caps change only speed, never results: the same snapshot and settings give the same
plan whatever the thread count or memory mode (6.2 principle 8).

**Shown to the user.** The Progress tab and the About box show the detected resources and the active
caps, for example: "6 of 8 threads, memory budget 5.1 GB (limited by available RAM), map cache reduced".

## 7. Drive Map

The headline feature. It is built early, against synthetic volumes, so it does not wait for the scanner.

### 7.1 World layout

- The volume is a 1-D cluster sequence laid out row by row (left to right, top to bottom), like
  classic defrag maps.
- Each drive gets one fixed **world** whose height-to-width ratio matches the screen the window is on
  when the drive is first analysed: `aspect = screen width / screen height` (from
  `wxDisplay::GetFromWindow`), row width `W = round_to_nice(sqrt(N × aspect))` clusters, height
  `ceil(N / W)`. A 16:9 screen gives a 16:9 world, a portrait screen a tall one.
- The world never re-flows: not on window resize, not on zoom, and not when the window is dragged to
  another screen. Places stay where the user saw them (Google Maps behaviour). The aspect is saved
  with the drive's snapshot, so the layout stays the same across restarts. "View → Reset map layout to
  this screen" recomputes it on request.
- At the deepest zoom one cluster is a square cell of up to about 32 px, with 1 px grid lines once
  cells are 6 px or more.
- A ruler shows byte offset, % of volume and, when the partition offset is known, % of the physical
  disk. Zone boundaries are drawn as markers.

### 7.2 Level-of-detail pyramid

- Level `k` bins cover `2^k` clusters. Each bin stores a compact summary: occupied count, dominant
  class, fragmented flag, unmovable flag, plan state (source / destination / both) and an age/heat
  bucket. About 8 bytes per bin.
- Coarse levels are precomputed from the extent index and free runs, starting at the first level
  with no more than ~32 M bins (~256 MB maximum; at 30 TB that is 256 clusters per bin), and built
  by merging upward.
- The pyramid is built in parallel per LCN stripe (6.2). Each stripe fills its own bins, then the upper
  levels are merged in parallel. Stripes are filled in as the scan finishes them (6.2 principle 7).
- Fine levels (few clusters per pixel) are not stored. Their tiles are rasterised directly from the
  extent index with a binary search on the tile's cluster range, which is cheap because the range is
  small.
- Updates (moves during execution, plan overlays) mark the affected bins and tiles dirty. Only those
  are rebuilt.

### 7.3 Rendering

- 256×256 px tiles per discrete zoom level (powers of two), rendered on a worker pool into 32-bit
  pixel buffers and kept in an LRU tile cache with a memory cap.
- Smooth zoom: the view scale is continuous. Between levels the nearest finer cached level is drawn
  scaled while the exact tiles render, as Google Maps does. Wheel zoom animates for about 150 ms and
  stays anchored at the cursor.
- The UI thread only composes cached tiles. The paint path never touches analysis data.
- **wxWidgets hosts the map; the GPU draws it.** wx provides the window, input events, timers and
  DPI handling. Drawing goes through Direct2D: first through wx's own Direct2D renderer
  (`wxGraphicsRenderer::GetDirect2DRenderer()`), so tiles are GPU bitmaps that are scaled and
  composed in hardware. If that is too slow at 4K, the map control renders with Direct2D directly on
  its `HWND` (a DXGI swap chain), still inside the wx layout. Plain GDI scaling (`StretchBlt`) runs
  on the CPU and is only a fallback.
- Painting: `SetBackgroundStyle(wxBG_STYLE_PAINT)` with no background erase, so there is no flicker.
  Direct2D does its own double buffering.
- **Animation is time-based.** `wxTimer` on Windows ticks in steps of about 15 ms and jitters, so zoom and
  pan inertia compute their position from elapsed time (`QueryPerformanceCounter`), not from tick count.
  Frames are paced to display refresh (`DwmFlush`, or the swap chain's present interval with direct D2D).
- Input: `wxEVT_MOUSEWHEEL` with fractional wheel deltas (precision touchpads send small steps, which
  give smooth zoom), `wxEVT_GESTURE_ZOOM`/`wxEVT_GESTURE_PAN` for touchpad pinch and two-finger pan.
- Per-monitor DPI (wx 3.3): moving the window to a screen with a different DPI changes only the pixel
  scale of drawing, never the world layout (7.1).
- Phase 2 starts with a spike that measures wx's Direct2D renderer against direct Direct2D on the
  `HWND` with 4K, 30 TB synthetic data, and picks the simplest one that holds 60 fps.

### 7.4 Interaction

- Drag to pan, with inertia. Wheel to zoom at the cursor. `+`/`-`, arrows, PgUp/PgDn, Home/End,
  `0` = fit volume, `1` = one cluster per pixel. Double-click zooms in; Shift+double-click zooms out.
- A minimap overview with the viewport rectangle, which can be dragged.
- Hover tooltip: LCN, byte offset, owning file path, stream, fragment `i/n`, class, size, times.
- Click selects a file and highlights all its fragments. "Go to file", file search, and "next/previous
  fragment" jump the view.
- Color modes: file class, fragmentation, age/heat, size, zone fit (in its target zone or misplaced),
  and plan preview (sources hollow, destinations solid, optional move arrows at deep zoom).
- A legend with live counts for the visible area.

### 7.5 Performance targets

- 60 fps while panning and zooming on a synthetic 30 TB / 20 M-file volume.
- A coarse tile renders in 5 ms or less; a fine tile in 2 ms or less.
- Full pyramid build for 30 TB: 5 s or less on 8 cores.
- Map memory: 512 MB or less for the 30 TB synthetic volume (on top of the snapshot).

## 8. Strategies

### 8.1 Strategy model

A strategy is an ordered **zone script** in the spirit of MyDefrag, kept as data so presets and user
edits share one engine:

```
Zone {
  name, selector (rules over class/path/extension/size/age/heat),
  anchor: front | after-previous | back,
  order: none | directory+name | name | size | age (oldest at the end) | modified | created,
  order_by_level: optional per-level override (e.g. video: none at Fast/Moderate, age at Maximum),
  action: keep-in-place | defragment-only | fast-fill | sort | pack-to-back,
  fragment_ok: minimum fragment size accepted (default 64 MB),
  gap_after: % of free space | absolute size | 0
}
```

- `keep-in-place`: never move. Used for churning files.
- `defragment-only`: join fragments, place each file near where it is, don't relocate it to another zone.
- `fast-fill`: compact the zone by moving files from its tail into holes near its head. Order is ignored.
- `sort`: bring the zone into the given order, moving as few bytes as possible (see 9.3).
- `pack-to-back`: pack contiguously so the zone ends at the end of the volume.

`$MFT`, the MFT zone, and unmovable files are fixed obstacles. Zones flow around them.

### 8.2 Classification

Each file gets one class from ordered rules: path patterns, then extension, then size, then
signature (magic bytes, opt-in only). Users can override per directory. Activity signals:

- modification and creation time (always reliable);
- last-access time, only if the volume is detected to update it (check the `disablelastaccess`
  setting and look for evidence in the timestamp distribution);
- USN journal churn: count of create/delete/overwrite records per file and directory within the
  journal's retained window. A directory that gets files created and deleted all the time is "churn".

### 8.3 Development storage

The problem: temp and object files are created, rewritten and deleted constantly, so any work
spent placing them is wasted, and they fragment whatever free space they land in.

Classes:

- **Source and config**: sources, headers, scripts, project and solution files, docs. Small, stable,
  read often.
- **Repository data**: `.git/objects/pack/*.pack` and `*.idx`, which are large, read often and
  rewritten rarely. Loose `.git/objects/xx/*` files are churn.
- **Dependencies**: `node_modules`, NuGet/Cargo/Maven/pip caches, vendored SDKs. Many small files,
  semi-stable.
- **Build output (churn)**: `obj/`, `bin/`, `build/`, `out/`, `target/`, `Debug/`, `Release/`, `x64/`,
  `.vs/`, `CMakeFiles/`, and `*.obj *.o *.pch *.ipch *.ilk *.pdb *.idb *.tlog *.lastbuildstate`.
- **Temp (churn)**: `*.tmp`, `~*`, `*.swp`, `.cache/`, `%TEMP%`-like folders, plus anything USN shows as
  churn.

Layout:

```
| MFT + directories | source + repo packs (sorted dir+name) | dependencies (fast-fill) | CHURN GAP | rest |
```

- Directories (`$INDEX_ALLOCATION`) go first, because every path lookup reads them.
- Source trees are sorted by directory, then name, so a build, grep or IDE index reads them close to
  sequentially and benefits from read-ahead.
- Churn files are `keep-in-place`. They are only defragmented if they have been stable for N days
  (default 14), which means they aren't really churn.
- **Churn gap**: a reserved contiguous free region (its size is edited in the Strategy tab's zone
  table; the map shows it but does not edit it) right after the dev zones, default
  `max(5% of volume, 2 × current build output + temp size)`, so new build output finds fast,
  contiguous space. Whether NTFS actually allocates new files into such a gap is checked on the test
  bed (Phase 7). If it doesn't, the gap still has value as consolidated free space but is shrunk to the
  1% default.

### 8.4 Archive, disk image and unknown-file storage

Classes:

- archives: `.zip .7z .rar .tar .gz .tgz .bz2 .xz .zst .cab`
- disc images: `.iso .img .bin/.cue .mdf/.mds .nrg .cdi`
- VM disks: `.vhd .vhdx .avhdx .vmdk .vdi .qcow2`
- backups: `.bak .wim .esd .tib .vbk`
- unknown: anything not matched by another class

Rules:

- Written once, read sequentially: make each file contiguous, or leave it if all fragments are at
  least 64 MB. Pack the zone with `fast-fill`, middle of the volume.
- VM disks: fixed-size disks are fully defragmented, since guest file-system speed depends on it.
  Dynamic or differencing disks grow, so they go at the end of the zone with a gap after them. Files
  modified in the last N minutes (likely attached to a running VM) are skipped and reported.
- Unknown files: `defragment-only`. Their access pattern is unknown, so they are not relocated.

### 8.5 Photo storage (files up to 10-20 MB)

- Classes: `.jpg .jpeg .png .heic .heif .webp .tif .tiff .psd`, RAW (`.cr2 .cr3 .nef .arw .orf .raf
  .rw2 .dng`) and sidecars (`.xmp`). Catalogs and previews (`.lrcat`, `.lrdata`, `Thumbs.db`, other
  thumbnail caches) are hot.
- Catalogs and thumbnail caches go in the fast zone.
- Photos are sorted by directory, then name. Cameras name files in shooting order, and albums are
  folders, so browsing a folder becomes one sequential read. Name order also keeps `IMG_0001.CR3` next
  to `IMG_0001.xmp`.
- The zone is packed without holes, with a small gap after it (default 2% of free space) for new
  imports.

### 8.6 Video storage (large files, 100 MB-8 GB+)

- Classes: `.mp4 .mkv .mov .avi .m2ts .mts .ts .wmv .webm .vob`, with size 100 MB or more.
- `pack-to-back`: contiguous and packed against the end of the volume.
- Order depends on the effort level:
  - **Fast and Moderate:** no order, only packing. A video moves only if it is outside the back zone,
    is fragmented below 64 MB, or leaves a hole that must be closed.
  - **Maximum:** sorted by age, **oldest at the very end** (by modification time, or creation time
    when that is older). Folder and name order gives no benefit for video and is not used.
- Rarely moved: a video that is already in the back zone with fragments of 64 MB or more is not
  touched at Fast or Moderate level. Holes left by deleted videos are closed at Moderate and Maximum.
- Trade-off shown to the user: the inner tracks read about half as fast, which is still far above
  playback bitrates (4K HEVC needs roughly 10-15 MB/s).

### 8.7 Mixed data drive (default preset)

```
LCN 0 (outer, fast)                                                        LCN max (inner, slow)
| MFT+dirs | hot: dev sources, catalogs | churn gap | photos | gap | archives, images, VMs, unknown | free | video |
```

### 8.8 General strategies (from AGENTS.md), expressed as zone scripts

Single-file/subtree defragment; hot zone (front); cold zone (back); directory clustering;
file-type segregation; size-based placement; access-time sort; free-space consolidation
(`fast-fill` over all movable files); read-ahead packing (sort by directory+name with no holes).

## 9. Planner

### 9.1 Pipeline

```
snapshot -> classify -> effort level -> target state (what "done" means at this level)
         -> least-movement solution -> order + VSS-align -> simulate + validate -> plan
```

The planner always works in two steps. First it decides the **target state** for the chosen strategy and
effort level. Then it computes the **least amount of data movement** that reaches that target (9.3). A
plan that moves more than it needs to is a planner bug, not a tuning choice.

### 9.2 Effort levels

Every strategy runs at one of three effort levels. The strategy says *where* things belong; the
effort level says *how far* to go towards that.

| | **Fast** | **Moderate** | **Maximum (full optimization)** |
|---|---|---|---|
| Purpose | Biggest gain in the shortest time | Good layout at a reasonable cost | The complete target layout |
| Areas touched | Only heavily modified areas (churn hot spots, 9.2.1) | All zones, misplaced files above a benefit threshold | Whole volume |
| Defragmentation | Files with many small fragments in hot spots; fragments of 64 MB or more ignored | All files with fragments under the zone's `fragment_ok` | Every movable file contiguous (`fragment_ok` = whole file) |
| Compaction | Local fast-fill: holes filled from files close by, so the head barely travels | Fast-fill per zone; zone gaps created | All zones packed; all free space gathered into the planned gaps |
| Sorting | None | Sorted zones only where nearly sorted (LIS with high tolerance) | Exact order in every sorted zone |
| Large files | Big long-lived files moved **only** if they sit inside a hot spot and block compaction; they go to their own zone | Moved when misplaced and the benefit per byte is high | Moved wherever the target layout needs them |
| Zone relocation of other files | No | Yes, by benefit-per-byte ranking | Yes, everything |
| Drastic moves (parking in scratch space, moving large parts of the volume) | Never | Never | Allowed |
| Default budget | 2% of used space or 15 minutes, whichever comes first (Settings: ×1, ×2, ×4, ×10) | ~20% of used space | None |

#### 9.2.1 Fast level in detail

1. **Find hot spots.** Score each LCN stripe by churn: USN create/delete/overwrite records, how many
   small free holes it has, and the fragment density of the files in it. Stripes above a threshold,
   merged with their neighbours, are hot spots.
2. **Defragment inside hot spots.** Files with many small fragments first, ranked by fragments removed
   per byte moved.
3. **Compact locally.** Fill the holes in each hot spot with the nearest files that fit, working from the
   hot spot's tail towards its head. Used space ends up packed and free space becomes one run at the
   end of the hot spot, which is where new churn files will land.
4. **Move big long-lived files out of the way.** A file is "big and long-lived" if it is at least
   64 MB and was not modified for N days (default 30). If one sits inside a hot spot and splits its
   free space, move it once, straight to its zone (for example video to the back). Nothing else is
   moved out of hot spots.
5. Stop at the budget. The plan is ordered so that stopping at any point leaves the volume better than
   before.
6. **Repeated runs.** The plan preview and the result screen tell the user that Fast can be run again
   to keep improving the drive: each run starts with the next most valuable moves.
7. **Budget setting.** The Settings window has a Fast budget multiplier of ×1 (default), ×2, ×4 or ×10.
   It scales the used-space share and the time limit together (×4 = 8% of used space or 60 minutes).

#### 9.2.2 Comparing levels

When the user picks a strategy, all three levels are planned in parallel (6.4) and shown side by
side: bytes to move, number of moves, estimated time, and the expected result (fragmentation, free
space runs, share of files in their target zone). The user picks a level with full knowledge of the
cost. The time estimate is `2 × bytes / measured throughput + moves × average seek cost`, and it is
corrected from real execution logs over time.

### 9.3 Least movement to reach the target

The target state is fixed first; the movement is then minimised against it.

1. **Lower bound.** For a given target, a file must move if it is not in its final place and is not
   "good enough" there. The sum of their bytes is the lower bound. Every plan reports
   `planned bytes / lower bound`. The aim is 1.0; anything above about 1.1 is investigated.
2. **Choose the target to fit what is already there.** Where the strategy leaves room for choice
   (exact zone start, which free run becomes a gap, the alignment of a sorted zone), try the
   candidates and keep the one with the lowest lower bound. Zone boundaries move towards where files
   already are, instead of files moving towards a fixed boundary.
3. **Good enough means no move.** Skip a file if it is in its target zone and every fragment meets the
   zone's `fragment_ok`, or if it is churn.
4. **Sorted zones use the longest increasing subsequence.** Find the set of files (weighted by bytes)
   that are already in the right relative order and leave them in place. Only the rest move.
   The tolerance depends on the effort level (9.2).
5. **Fast-fill.** Fill holes at the head of a zone with files from its tail, best-fit by size, so each
   move both removes a hole and shortens the zone. The bytes that must move are those beyond the
   packed boundary; choose the boundary and the hole filling that minimise them.
6. **Move the smaller thing.** When a large file needs contiguous space, prefer moving small blockers
   out of the way over moving the large file.
7. **Every file moves at most once.** Moves form a dependency graph (move A needs the space B is in).
   With enough free space, the graph is ordered topologically and each file moves exactly once.
   A cycle is broken by parking its **smallest** file in scratch space, which costs one extra move of
   that file. The total extra cost is reported. At Fast and Moderate level, a cycle is dropped
   instead of parked.
8. **Benefit per byte under a budget.** If the level has a budget, rank moves by benefit (fragments
   removed, zone fit, free-space runs joined) per byte moved, and take moves from the top, keeping their
   dependencies together.
9. **Little free space.** When free space is under ~5%, the plan uses only what is free and reports which
   goals it could not reach.

### 9.4 VSS awareness

- Detect existing shadow copies on the volume (VSS API, or by listing shadow copies as a fallback).
- If shadow copies exist, prefer destinations with `dest LCN ≡ source LCN (mod 16 KB / cluster size)`
  and moves of at least 16 KB. Report how many moves could not be aligned and the estimated shadow
  storage growth. Offer to block unaligned moves.

### 9.5 Validation

Each plan is simulated on a copy of the free-run map and extent index. Checks:

- every destination is free at the moment of its move;
- no unmovable file or stream is touched;
- the clusters of every file are preserved (same count, same VCN order);
- the result matches the target layout within its tolerances;
- bytes moved are no more than the reported lower bound plus the reported extra cost;
- the totals (bytes moved, files, fragments before and after) are recomputed and agree.

Property tests generate random synthetic volumes and check these invariants for every strategy.

## 10. Analysis And Execution

### 10.1 Drive discovery

- Volumes come from `FindFirstVolumeW`; the physical disk from `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`.
- Seek penalty (`StorageDeviceSeekPenaltyProperty`) and TRIM (`StorageDeviceTrimProperty`) decide
  HDD or SSD. Bus type and removable flags are recorded too. SSDs get only the TRIM action (10.5).
- File system, cluster size, partition offset, MFT location and size come from
  `FSCTL_GET_NTFS_VOLUME_DATA`.
- Badges: HDD/SSD, NTFS, admin needed, VSS present, last-access tracking on/off, Windows scheduled
  optimization on/off, saved snapshot available (and how old).

### 10.2 Scanning

- **API path (Phase 3):** `FSCTL_ENUM_USN_DATA` for the file list, then `FSCTL_GET_RETRIEVAL_POINTERS`
  per file and stream, plus metadata. Correct and simple, used as the reference.
- **MFT path (Phase 4):** the parallel pipeline in 6.3. One reader reads `$MFT` in large sequential
  chunks; parser workers apply update-sequence fixups and parse `$STANDARD_INFORMATION`,
  `$FILE_NAME`, `$DATA` (resident or run lists), `$ATTRIBUTE_LIST`, `$INDEX_ALLOCATION`; paths are
  rebuilt from parent references. It must give the same extent index as the API path on the test bed.
- The API path is parallel only in CPU work: one thread issues the per-file queries in MFT record
  order (to keep reads near each other), workers decode and store the results.
- Volume bitmap via `FSCTL_GET_VOLUME_BITMAP` in chunks, converted to free runs in parallel (6.2).
- Record the USN journal ID and current USN at scan time. Save the snapshot for USN catch-up next time (6.2).
- Special handling: resident files (no clusters, never moved); compressed files (move whole 16-cluster
  compression units); sparse files (allocated runs only); hard links (counted once); reparse points
  and alternate streams (shown, moved only where supported); encrypted files (moved only after they
  pass on the test bed); pagefile, hiberfil, `$LogFile`, `$BadClus`, `$Boot` (unmovable).

### 10.3 Revalidation before execution

Read the USN journal from the scan's USN. Files changed since the scan are re-read or dropped from the
plan. If the journal ID changed, or the scan is older than a configurable age, a full rescan is
required. Stale snapshots are never executed.

### 10.4 Execution engine

- The app always runs elevated (section 4). There is no non-admin mode.
- Moves go through `FSCTL_MOVE_FILE` in chunks (default 16 MB per call) so cancel and pause respond
  quickly. Each move re-reads the file's retrieval pointers immediately before it.
- "Destination now in use" (stale bitmap): refresh the bitmap locally, pick a new destination under
  the same rules, or skip the move with a reason.
- Locked, changed or failing files are skipped with a per-file reason. Execution never stops for one bad file.
- After each move, verify the new extents. The live map updates through the dirty-tile path.
- An append-only execution log (JSON lines) under `%LOCALAPPDATA%\IroncladDefrag\logs`. After a crash
  or power loss the plan can be resumed or rebuilt from the log plus a rescan.
- Optional low I/O priority (`FILE_IO_PRIORITY_HINT_INFO`) and a throughput limit.
- The app checks whether Windows scheduled optimization ("Optimize Drives") is enabled for the drive
  and shows a badge explaining that it may undo zone layouts. It offers a one-click option to remove
  that drive from the Windows schedule, applied only after the user confirms, and a way to put it back.

### 10.5 SSDs

SSDs appear in the all-disks panel with a single "TRIM" action, which runs Windows `defrag <vol> /L`
(or `MSFT_Volume.Optimize` with ReTrim) on a worker and shows its output. Nothing else: no scan, map,
strategy or plan.

## 11. UI

- **Top menu:** File, Drive (Analyze, Refresh, TRIM), Optimize (strategy, Build plan, Execute,
  Pause/Cancel), View (color mode, minimap, legend, zoom), Settings, Help.
- **All-disks panel:** one row per volume: letter/label, size bar (used/free), file system, HDD/SSD,
  fragmentation %, last analysed, and badges. SSD rows show only a TRIM button.
- **Drive map panel:** the map widget from section 7, with a toolbar for color mode, zone overlay,
  plan overlay and search.
- **Bottom panel**, with tabs that stay consistent through the workflow:
  - Summary: fragmentation, free-space runs, and per-class counts and bytes, with a zone-fit chart.
  - Strategy: preset picker, effort level (Fast / Moderate / Maximum), the zone list with an editable
    table, and gaps.
  - Plan: the three levels side by side (9.2.2); for the chosen one, files and bytes to move, the
    lower bound, estimated time, before/after metrics, warnings (VSS, low free space, skipped files).
  - Progress: stage, disk throughput, worker utilisation, ETA, current file.
  - Log: skipped and failed files with reasons.
- Workflow: **Analyze → Choose strategy → Build plan → Review on map → Execute**. A "Quick" button runs
  Analyze and a Fast-level plan for the drive's default strategy, but always stops at the review step. Status appears in one place only, the bottom panel's
  progress line.

## 12. Test Bed

- `tools/make-testvol.ps1` creates a VHDX of 4-64 GB with `diskpart`, formats it NTFS with a chosen
  cluster size, and mounts it.
- `tools/gen-scenario.ps1` (or a small C++ tool) fills it with reproducible, seeded scenarios. To
  create fragmentation it interleaves writes, deletes and appends:
  - `dev`: repos with sources, `.git` packs, `node_modules`, and repeated build/clean cycles;
  - `archive`: ISOs, zips, VHDXs, unknown blobs;
  - `photos`: thousands of 2-20 MB files in dated folders, with sidecars;
  - `video`: 100 MB-8 GB files written in parallel so they interleave;
  - `mixed`, `almost-full` (97% used), `vss` (with a shadow copy), `special` (compressed, sparse,
    hard links, ADS, reparse points, encrypted).
- `tools/verify-testvol.ps1` hashes every file before and after a run, compares them, and runs
  `chkdsk /scan`.
- **Scale volumes:** a dynamic (thin) VHDX of 30 TB, formatted NTFS, filled with millions of files
  that are allocated with `SetEndOfFile` + `SetFileValidData` and fragmented by interleaving, without
  writing their data. NTFS metadata is real and at full scale, while the host file stays small.
  These volumes are used for scan, USN catch-up, map and planning benchmarks, not for execution, since
  moves would write real data.
- Integration tests: analyse, plan, execute and verify on each scenario. The API and MFT scanners must
  agree; hashes must match; chkdsk must be clean; the plan's predicted layout must match a rescan.

## 13. Phases

Each phase ends only when every exit criterion has been checked, and the result is written into
`.codex/arch.md` and `.codex/map.md`.

### Phase 0 - Foundation
- CMake targets from section 5; a wx download script; Debug and Release build, copy their DLLs and start.
- GoogleTest wired up; one core test; `build.bat`, `test.bat`, `run-elevated.bat`.
- Logging, cancellation tokens, resource detection and caps (6.6), the work-stealing thread pool
  sized from them, a job runner with progress events that reach the UI.
- **Exit:** clean checkout, then script, then both configurations build, tests pass, the app opens
  an empty window, and a fake parallel job reports progress without blocking the UI.

### Phase 1 - UI shell with fake data
- The full window layout from section 11, with a synthetic drive list and a placeholder map.
- Workflow states, menus and keyboard shortcuts.
- **Exit:** the whole workflow can be clicked through on fake data; no duplicated status or panels;
  layout reviewed and agreed before Phase 2.

### Phase 2 - Drive map engine
- Rendering spike (wx Direct2D renderer vs direct Direct2D on the `HWND`, 7.3), world layout, LOD pyramid, tile cache and workers, smooth zoom,
  pan, minimap, hover, selection, color modes, synthetic 30 TB volume generator in `core`, compact
  snapshot structures (6.2 principle 4).
- **Exit:** the performance targets in 7.5 are met on synthetic volumes; tests cover the pyramid
  against brute force on random volumes, and parallel builds equal single-threaded ones.

### Phase 3 - Read-only analysis (API path)
- Drive discovery and badges, SSD TRIM action, parallel bitmap to free runs, file and extent scan,
  special files, USN cursor, `icd-cli analyze --json`, the map fed with real data stripe by stripe.
- **Exit:** on test volumes, results match `fsutil volume diskfree` and `fsutil file queryextents` for
  sampled files; no writes; cancel works; a real data drive analyses correctly.

### Phase 4 - Fast MFT scanner
- Parallel raw `$MFT` reader as in 6.3 and 10.2; the API path stays as fallback and reference.
- Saved snapshots and USN catch-up (6.2 principle 6).
- **Exit:** identical extent indexes on every test scenario, for any worker count; a 1 M-file volume
  scans at least 10× faster than the API path; the scale targets in 6.5 for scanning and catch-up are
  met on the 30 TB scale volume; the disk stays 90% or more busy during the MFT read; the 30 TB
  scale volume also scans and plans to identical results when capped to 2 threads and a 2 GB
  memory budget (memory-mapped mode).

### Phase 5 - Classification and activity
- Rule engine, the classes from section 8, last-access detection, USN churn statistics, per-directory
  overrides, a "class" color mode on the map, per-class summary.
- **Exit:** tests for rule order and every class; the `dev` scenario's churn directories are detected
  from USN data.

### Phase 6 - Strategies and planner
- Zone script engine, presets 8.3-8.8, the three effort levels (9.2), the least-movement planner
  (9.3) with the parallel stages from 6.4, VSS alignment (9.4), validator (9.5), plan overlay on the map,
  the side-by-side level comparison, `icd-cli plan --level fast|moderate|max`.
- **Exit:** property tests pass for every preset at every level; parallel plans equal single-threaded
  plans; on the scenarios `planned bytes / lower bound` is 1.1 or less, and Fast plans stay within
  their budget and touch only hot spots; on small synthetic volumes an exhaustive search confirms the
  planner's byte count is minimal; the 6.5 planning target is met on the 30 TB scale volume; plans
  are fully inspectable.

### Phase 7 - Execution on the test bed
- Execution engine from 10.4, `icd-cli execute`, revalidation from 10.3, execution log, resume.
- Check NTFS allocation behaviour into the churn gap (8.3) and record the result.
- **Exit:** every scenario passes the section 12 integration test; cancel and pause in the middle are
  safe; a simulated stale-bitmap conflict is handled.

### Phase 8 - GUI execution and live map
- Execute, Pause and Cancel in the GUI; live tile updates; progress and log tabs; elevation flow.
- **Exit:** the full workflow runs on a test VHDX from the GUI; the map shows the moves as they happen.

### Phase 9 - VSS and Windows integration
- VSS detection and alignment UI, scheduled-optimization detection with the confirmed opt-out (10.4),
  warnings for RAID, Storage
  Spaces, removable and encrypted volumes.
- **Exit:** on the `vss` scenario, aligned moves leave shadow storage size unchanged (measured with
  `vssadmin list shadowstorage`).

### Phase 10 - Settings and custom strategies
- Persisted settings (versioned JSON), user-edited zone scripts, import/export, per-drive defaults,
  the Fast budget multiplier (9.2.1), manual thread and memory caps (6.6).
- **Exit:** a custom strategy survives a restart and gives the same plan.

### Phase 11 - Deployment
- NSIS installer (`C:\Program Files (x86)\NSIS\makensis.exe`),
  semantic product version (1.0.0 for the first release, decision 11) plus the short git hash as build metadata,
  `make-installer.bat`, output in `dist/`.
- **Exit:** the installer installs, the app starts from the install folder, and uninstall removes it.

### First real-drive run
Only after Phase 8, on a data drive with a backup, with a byte budget, and using the `photos` or `video`
preset first, since those have the simplest file lifecycles.

## 14. Decisions

Answered on 2026-10-31:

1. **Video order:** no ordering at Fast and Moderate, only packing. Oldest at the very end at Maximum.
   Folder and name order is not used for video (8.6).
2. **Elevation:** the app always runs elevated (section 4).
3. **Windows scheduled optimization:** warn, and offer to remove the drive from the schedule after the
   user confirms (10.4).
4. **Memory:** up to 8 GB for a 30 TB / 20 M-file snapshot is fine; spend memory where it buys speed (6.2).
5. **Fast budget:** 2% of used space or 15 minutes; tell the user it can be run again; a ×2/×4/×10
   multiplier in Settings (9.2.1).
6. **Long-lived files:** 64 MB or more, unmodified for 30 days (9.2.1).
7. **Churn gap:** edited in the zone table only, not by dragging on the map (8.3).
8. **Platform:** Windows 10 22H2 or later; scale targets measured on 8 cores (section 4).
9. **Map shape:** the world's aspect ratio follows the screen the window is on at first analysis, and is
   not recalculated when the window moves to another screen (7.1).
10. **Resource caps:** threads and memory are detected automatically and capped to what is available;
    the 8 GB / 8-core figures are targets, not requirements (6.6).

Answered on 2026-11-04:

11. **Versioning:** the product is versioned 1.0 (`MAJOR.MINOR.PATCH`, first release 1.0.0); releases are few, so
    no year/month numbers. The short git hash is build metadata only.

## Sources

- Microsoft: [Defragmenting Files](https://learn.microsoft.com/en-us/windows/win32/fileio/defragmenting-files)
  (move rules, supported streams, 16 KB VSS alignment)
- Microsoft KB 312067: [Shadow copies may be lost when you defragment a volume](https://ftp.zx.net.nz/pub/archive/ftp.microsoft.com/MISC/KB/en-us/312/067.HTM)
- Microsoft: [Disk fragmentation and system performance](https://techcommunity.microsoft.com/blog/askperf/disk-fragmentation-and-system-performance/372921)
  and [KB942092 Vista defrag features](https://www.outsidethebox.ms/blog/wp-content/uploads/files/KB942092%20Features%20of%20the%20Windows%20Vista%20hard%20disk%20defragmentation%20utility.pdf) (64 MB fragment rule)
- Microsoft: [How NTFS reserves space for its MFT](https://support.microsoft.com/kb/174619),
  [Master File Table](https://learn.microsoft.com/windows/win32/fileio/master-file-table)
- Microsoft: [fsutil behavior](https://github.com/MicrosoftDocs/windowsserverdocs/blob/main/WindowsServerDocs/administration/windows-commands/fsutil-behavior.md),
  [Winaero: last access time updates](https://winaero.com/disable-ntfs-last-access-time-updates-in-windows-10)
- Microsoft: [Optimize-Volume](https://learn.microsoft.com/powershell/module/storage/optimize-volume)
  (ReTrim, SlabConsolidate)
- Microsoft: [Walking a buffer of change journal records](https://learn.microsoft.com/en-us/windows/win32/fileio/walking-a-buffer-of-change-journal-records)
- [Zone bit recording](https://en.wikipedia.org/wiki/Zone_bit_recording) (outer vs inner track speed)
- [JkDefrag / MyDefrag](https://en.wikipedia.org/wiki/JkDefrag), [MyDefrag usage guide](https://maketecheasier.com/defragment-hard-disks-with-mydefrag)
  (zones, gaps, space hogs)
- [UltraDefrag handbook](https://ultradefrag.net/handbook/v9/standard/Introduction.html) (quick vs full optimization)
- [WizTree](https://diskanalyzer.com/about), [voidtools forum on MFT vs USN enumeration](https://voidtools.com/forum/viewtopic.php?p=14475),
  [disktree MFT fast path notes](https://docs.rs/crate/disktree/latest/source/docs/mft-fast-path.md)
