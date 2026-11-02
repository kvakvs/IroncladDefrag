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
- View commands are enabled once a map is shown; color modes whose features are still to come (zone
  fit, plan preview) stay disabled (`MapPanel::commandAvailable()`). The toolbar color
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

## Saved snapshots

- `core/snapshot/SnapshotIO` (T064, plan.md 6.2 principle 6): one file per volume. Header: magic `ICDSNAP\x1A`,
  format version (any other version is rejected and the volume rescanned, never migrated), the meta section
  (`SnapshotMeta`: volume GUID name and NTFS serial, scan time, map aspect of plan.md 7.1, scanner, counts) and the
  payload size, both under one CRC-32. `readSnapshotMeta()` reads only that much (badges, the catch-up decision).
- The payload is the snapshot's arrays as they are in memory (geometry, USN cursor, name offsets and text, the seven
  file columns, extent records, per-file positions, free runs), cut into 4 MB blocks; each block carries its raw
  size and CRC-32 and is compressed by a pluggable `BlockCodec` (stored as is when that is not smaller). Blocks are
  encoded and decoded in parallel batches of eight on the executor; the bytes are the same for any executor. The
  private parts are reached through `SnapshotAccess`, a friend of the snapshot classes; the directory tree and the
  free total are rebuilt on load.
- Loading checks every block checksum and every invariant the builder guarantees (column sizes, name ids, parents,
  sorted disjoint extents inside the volume owned by real files, cluster sums, the per-file index, sorted free runs)
  before anything is used, so a file either loads identical to what was written or throws `SnapshotFormatError`.
  Tests flip every header byte and hundreds of random payload bytes and truncate at hundreds of lengths: all caught.
- `platform::XpressCodec` uses the Windows Compression API (XPRESS Huffman, cabinet.dll), so no library is added.
  `services::SnapshotStore` keeps the files in `%LOCALAPPDATA%\IroncladDefrag\snapshots\Volume{GUID}.icds`,
  writes a `.tmp` and renames it over the old file (a crash mid-save keeps the previous snapshot), and turns damaged or
  old files into "no snapshot" with a reason for the log.

## Settings

- `services::SettingsStore` (owned by `AppServices`, UI thread only) keeps `%LOCALAPPDATA%\IroncladDefrag\settings.json`:
  versioned JSON (`version` 1; other versions are refused, not migrated), read once at start-up, written whole after
  each change through a `.tmp` and a rename. A missing file gives defaults; a damaged one is logged, kept as
  `settings.json.bad` and replaced by defaults, so it never blocks start-up. Drive entries that cannot be used are
  dropped one by one; unknown members are ignored.
- `services::Settings` is plain data with `settingsJson()` / `parseSettings()`, testable without files. It holds the
  per-drive strategy choice (preset id and effort level) keyed by `driveSettingsKey()`: the volume GUID name, so a
  letter change keeps the choice, or the root for drives without one (fake drives). Presets are stored by their
  stable `StrategyPreset::id`, never by display name.
- `WorkflowBinding` restores the remembered choice when a drive is selected (unknown preset -> default preset, effort
  kept) and remembers the Strategy tab after each user change. Quick still runs the default preset at Fast without
  touching the remembered choice. T130/T131 extend the same file (budget multiplier, caps, custom strategies).

## USN catch-up

- The MFT merge has two stages (T065): `draftFiles()` turns rows into `FileDraft`s (name, parent reference, size,
  times, attribute flags, stream ordinals, whether clusters and the main stream were seen) with their extents, and
  `finishDrafts()` resolves parents, metadata, pinned, residency and extent flags, reconciles and builds. A full scan
  is `mergeRows()` = both stages; `draftsOfSnapshot()` turns a saved snapshot back into drafts (derived flags dropped,
  VCNs renumbered in the saved per-file order) and finishing them gives the same snapshot. So a catch-up and a full
  scan share the final stage and agree field by field.
- `services::catchUpSnapshot()` (`services/snapshot/UsnCatchUp`): `catchUpBlocker()` sends to a rescan when the
  snapshot has no cursor, the volume no journal, the journal id changed (recreated), records since the cursor were
  purged (wrapped) or the volume size changed. Otherwise `platform::readUsnJournal()` reads from the saved USN to the
  current next USN (queried first, so later changes stay for the next run) and collects every file and parent record
  named; records 0-15 and the snapshot's metadata files are always added, because changes to `$MFT`, `$Secure`,
  `$UsnJrnl` and the like are not journaled. `readMftRecords()` reads just those records through the MFT pipeline (a
  "wanted" bitmap instead of the in-use one, 64 KB skips), then the extension records their attribute lists name
  (`missingListedRecords()`), and the second pass joins them. When lists name records not read yet, the whole wanted set
  is read again in one new plan: a second plan over the same stretches would give chunks whose rows interleave with
  the first plan's (found at 10 M files, where `$MFT`'s own extension records sit among records 0-15). `RowIndex` orders
  chunks by their first row. The saved drafts of every re-read record are replaced
  by the fresh ones (`combineDrafts()`), free space is the saved free runs plus the re-read files' old clusters
  (reconcile takes out their new ones), and the result gets the new cursor. Dirty ranges: old and new clusters of the
  re-read files.
- The finish is a patch of the saved snapshot (T069, `core::patchSnapshot()` via `mft::finishCatchUp()`): finishing
  every kept file again took 32 s at 20 M files (reconcile 8 s, drafting kept files with their extents 7.5 s, name
  interning 5 s, builder sort and grouping 5 s). Now the kept files get only their new parent and flags, worked out by
  the same rules as the full finish (`resolveParent()`, `placementFlags()` shared with it; residency stays as saved),
  10 bytes per file; the fresh drafts are finished in full. The core patch keeps the kept files' columns and name ids,
  copies their extent records in one parallel pass per 64 K-record chunk with file indexes remapped and the new
  records merged in by LCN, maps their per-file positions, frees the clusters of files that were not kept, keeps the
  rest of the `$Unattributed` runs (minus whatever the new extents took, VCNs counted along as the full finish does),
  interns only new names into the saved pool and rebuilds the directory tree. Result: the full finish's snapshot,
  name ids aside (`compareExact()` compares names as text).
- The patch refuses (nullopt, saved snapshot untouched) wherever a fresh build could come out differently, and the
  catch-up then finishes every file as before: fresh extents over a kept file's extents (overlaps are resolved by
  file index there; only possible when a move was not journaled), a kept file whose flags change (a directory moved
  into or out of the metadata tree), or a name pool holding more than twice as many names as files (deleted files'
  names stay in a patched pool; the full finish builds a fresh one).
- Measured with `catchup-bench` (synthetic 30 TB / 20 M files, 48 M extents, 5,000 re-read records, a third of them
  deleted): patch 1.9 s on 31 workers, 2.0-2.8 s on 8, against 44-53 s for the full finish, same snapshot. The re-read
  records' old clusters come from the per-file index instead of a pass over every extent.
- Last-access times are the one thing a catch-up cannot follow: NTFS does not journal access-time updates, so the
  catch-up keeps the saved ones. `compareExact()` can leave them out (`ExactOptions::accessTimes`), which
  `icd-cli check-catchup` does.
- Raw reads see what is on disk: a change still in the cache when the cursor is taken is read stale and, with the
  cursor already past it, not re-read next time. The tests flush the volume first (`Write-VolumeCache`); in the app
  this is the same window a full raw scan has and it closes with the next change to those files.
- Analysis (`analyzeVolume()`) now tries the saved snapshot first: same volume GUID and NTFS serial, made by the MFT
  scanner, caught up when the journal allows; otherwise the full path. Every analysis saves its snapshot
  (`AnalysisOptions::saveSnapshot`), and the map keeps the saved world aspect (`savedSnapshotMeta()`); the report says
  what happened (`savedSnapshot`, `caughtUp`). After a catch-up of the volume the GUI shows,
  `MapData::update()` copies the shown map's pyramid and recomputes only the dirty ranges (a full pyramid when the
  newest modification time moved); the result equals a fresh build.
- `tests/integration/catchup/check-catchup.ps1`: a `mixed` volume with a USN journal, analysed, then three rounds of
  seeded changes (small and large files, interleaved appends, truncation, deletions, moves, renames of a directory,
  a 400-file directory, named streams, hard links, a deleted tree): each catch-up equals a full rescan (about 1,750
  journal records and 490 records read per round, 0.04 s), the analyses in between catch up and save, an unchanged
  volume is up to date, and a recreated journal forces a rescan.

## Memory modes

- `core::estimateSnapshot()` (T066, plan.md 6.6) predicts from the MFT record count, before a scan: files (one per
  record), extents (the design ratio of three per file), the snapshot, the part mapped files can take (extent index,
  name pool) and the scan's peak (rows, drafts, pending extents). At 20 M records the snapshot is about 4.5 GB.
  `planMemory()` walks the give-way ladder (`MemoryRung`: full, no speed extras, smaller map cache, levels one at a
  time, mapped storage) and takes the first rung whose predicted use fits the budget (0: no limit), the last one when
  none does; `giveWay()` steps one rung down. `MemoryPlan` answers what each consumer may do (speed extras, tile
  cache size, parallel levels, mapped storage); today the scan uses the last one, the planner and the map will read
  the others.
- The extent index and name pool are `LargeVector`/`LargeString` (`core/util/LargeArray.h`): an allocator that holds a
  shared reference to a `std::pmr::memory_resource`, moves with its container (so move-assigning a snapshot keeps its
  storage, unlike `polymorphic_allocator`), sends copies to the heap and has no move operations (containers keep
  using moved-from allocators, and MSVC debug containers free a proxy through them). `SnapshotBuilder` takes the
  storage; the arrays keep it alive for as long as any of them exists.
- `platform::MappedFileResource` puts every allocation of 1 MB or more into its own temporary file under
  `%LOCALAPPDATA%\IroncladDefrag\cache` (`FILE_FLAG_DELETE_ON_CLOSE`, so nothing stays behind even after a crash),
  mapped read-write; smaller ones go to the heap. `services::storageFor()` gives it to the MFT scanner
  (`MftScanOptions::storage` -> `mergeRows()` -> the builder) on the last rung; `MemoryGovernor` checks Windows'
  low-memory signal and steps down a rung when it fires.
- Analysis plans before the full scan (`AnalysisOptions::memoryBudgetBytes`, from the caps in the GUI;
  `forceMappedStorage` for checks) and reports the mode (`memoryPlan`). `icd-cli analyze --memory-budget B --mapped`
  and `icd-cli check-caps <vol> [--threads N] [--memory-budget B]` (default 2 workers, 2 GB, mapped): on the special
  volume and on the 1 M-file scale volume the capped scan equals the uncapped one exactly (1,001,029 files, 1,450,698
  extents, 0.52 s either way).

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

## MFT record parser

- `core/ntfs` (T060) parses raw `$MFT` records with no Win32: it is the CPU half of the MFT scanner (plan.md 6.3)
  and is tested on handcrafted records (`tests/core/NtfsRecordBuilder.h` writes them the way NTFS does, update
  sequence included) and on records captured from the `special` scenario (`tests/data/ntfs/*.bin`, made by
  `tools/testvol/capture-mft-records.ps1`).
- `parseRecord()` applies the update sequence fixups in place (512-byte stride whatever the sector size), checks
  the header (signature, used bytes, first attribute, the record's own number) and walks the attributes. It keeps
  `$STANDARD_INFORMATION` (times, DOS attributes), every `$FILE_NAME` (parent, namespace, UTF-8 name), every
  non-resident attribute of any type with its decoded run list (they own clusters), and the resident `$DATA`
  (size only), `$ATTRIBUTE_LIST` and `$BITMAP` (values). One `ParsedRecord` is reused per worker; `clear()` keeps
  capacity.
- Corrupt input never crashes or reads out of bounds: every offset is checked with `fits()` before `load()`, and
  any inconsistency returns a `RecordStatus` (Empty, BadSignature, BadFixup, BadHeader, BadAttribute) so the
  scanner skips that record. Run lists must cover exactly `[startVcn, lastVcn]`; LCN deltas may not leave
  `[0, int64 max]`. Fuzzing 20,000 damaged copies of captured records and random run lists is part of the tests.
- Names convert UTF-16 to UTF-8 with lone surrogates as U+FFFD, the same as `WideCharToMultiByte`, so raw names
  equal the API path's. `parseAttributeList()` reads list entries (type, start VCN, holding record, id, name); the
  scanner uses them to accept extension-record segments.

## MFT scanner (fast path)

- `services::scanFilesMft()` (T061, plan.md 6.3): record 0 is read at `MftStartLcn` and parsed; its `$DATA` segments
  (following its attribute list into extension records when `$MFT` is that fragmented, in rounds over the part
  already mapped) give the map of `$MFT`, and `$MFT::$BITMAP` (resident or read through its runs) which records are
  in use. `core::ntfs::planMftReads()` turns that into reads: cluster-aligned chunks of whole records, at most 16 MB,
  unused stretches of 1 MB or more skipped, sorted by volume offset so the head moves one way; a record cut by a run
  boundary (clusters smaller than records) is read in pieces.
- `platform::MftReader` opens the volume a second time with `FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING` and
  streams the reads with 3 outstanding (a ring of OVERLAPPED slots, completed oldest first). Buffers come from a
  `BufferPool` (VirtualAlloc, page-aligned): in flight plus up to 8 parsing plus one. `acquire()` blocks while all are
  out, which is the bounded queue of plan.md 6.2: when parsers fall behind, the reader stops issuing reads and the
  time shows as `starvedSeconds`. On cancel or error the reader waits for outstanding reads before buffers go.
- Each completed buffer becomes a parser task on the job's executor (inline when the executor has one thread: a queued
  task would wait behind the blocked reader). `mft::parseChunk()` writes the records of one chunk into that chunk's
  own `ChunkRows` (thread-local output, no locks): record rows, names, segments and extents with the record number as
  provisional file and the segment index as provisional stream.
- `mft::mergeRows()`: `RowIndex` sorts chunks by first record (so the result does not depend on read order or worker
  count) and maps record numbers to rows. Base records with a name, and records 0-11, become files in record order.
  Name: the first that is not a DOS alias (the root's is empty); hard links count non-alias names. Parents by
  reference with sequence check, unknown or stale parents become the root (as on the API path). Metadata: records 0-11
  but the root and everything below them; pinned root names unmovable. Streams: unnamed `$DATA` or a directory's `$I30`
  index is 0, named `$DATA` 1, 2, ... in record order (NTFS collation order, the same as `FileStreamInfo`), any other
  non-resident attribute (`$BITMAP`, `$ATTRIBUTE_LIST`, `$REPARSE_POINT`, `$EFS`, view indexes `$R`, `$O`, `$Q`, ...)
  `0x8000 + n` and unmovable. A directory's size is its `$I30` allocation size, which is what `FileStandardInfo`
  reports. The rules shared with the API path live in `services/scan/ScanRules` (pinned names, attribute bits to flags,
  extent flags, residency). Extents and free runs are reconciled and built like on the API path, `$Unattributed`
  last; parallel steps write to disjoint slots, so any executor gives the same snapshot.
- Files spread over several records (T062, `mft::joinExtensionRecords()` in `MftSecondPass`): extension records are
  ordinary in-use records, so the main pass has already parsed them; the second pass only decides which belong where.
  For every base row with an `$ATTRIBUTE_LIST` it parses the list (resident value from the rows, or read from its
  clusters through a `ListReader`, which the scanner backs with `MftReader::read()`) and accepts each listed record
  that is in use, names that base record (record and sequence) and has the sequence number the list names. Unlisted
  and stale records are rejected and their clusters stay with `$Unattributed`, so a reused record never adds clusters
  to the wrong file. A list that cannot be read leaves the file with its base record (and it is then not called
  resident). The merge folds accepted parts into their base file: names (hard links in extension records count),
  main size and `$I30` size from whichever part holds the first segment, segments and extents in VCN order. Parent
  resolution is the merge's parallel per-file step (reference with sequence check against the row index).
- Cross-check on quiet volumes: `compare-scanners` on the `special` volume mounted read-only finds nothing for 1, 2
  and 8 workers (the 1,324-fragment file over five extension records and the compressed logs included). Last-access
  times are the one field that may differ: NTFS updates them in memory and writes the record later, so the raw record
  can be older than what the API reports (never newer); these count as deferred, not as differences. Mounting a
  volume touches the access time of the root and `System Volume Information`. On X: (live, 112 K files) the scans
  agree completely when nothing writes in between.
- Default since T063: analysis uses the MFT scanner and falls back to the API scanner when it fails (not on
  cancel); `icd-cli analyze` follows the default unless `--scanner` names one, which then has no fallback. The MFT scan
  hands partial snapshots to the live map like the API path: every 3 s the chunks parsed so far are merged (no second
  pass) on a pool task, at most one at a time, so the reader keeps issuing reads; a failed partial only skips one map
  update. `PartialScanSink` moved to `ScanStats.h`, shared by both scanners.
- Cross-check (T063, `tests/integration/scanner-equivalence/check-scanners.ps1`): every scenario on a fresh read-only
  volume, `compare-scanners --threads 1,2,0`: all eight agree (the MFT scan was up to 42x faster on these small
  volumes). Real drives E: (15 K files, 621 extension records) and X: (112 K files) agree too; real-drive read tests use
  only those two storage drives. On the 1 M-file scale volume: API scan 23.5 s, MFT scan 0.48 s (49x; the VHDX sits on an SSD, $MFT 1 GB read with the device 99% busy).
- Disk busy time comes from the reader (time with a read outstanding; an upper bound) and from the device's own
  counters (`IOCTL_DISK_PERFORMANCE`, 100 - idle %). Measured on X: (2.3 TB HDD, 112 K files, 116 MB of `$MFT`):
  read in 1.64 s with the disk 100% busy (device 99.9%); 1.87 s for the whole MFT scan against 7.3 s for the API scan.
- `AnalysisOptions` choose the scanner; a failed MFT scan falls back to the API path unless asked not to
  (`AnalysisResult::fallbackReason`). `icd-cli analyze --scanner api|mft --threads N` picks them by hand, and
  `icd-cli compare-scanners <vol> [--threads 1,2,8]` scans with both and diffs them (`services/scan/ScanCompare`):
  field by field for every file the API path could read, free runs exactly, the remaining clusters as a total, and the
  MFT runs among themselves exactly. `$Extend` metadata `FSCTL_ENUM_USN_DATA` does not list (`$Quota`, `$ObjId`,
  `$Reparse`, `$Repair`, `$Deleted`) is an accepted extra of the MFT path.

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
- The tooltip's type line comes from file flags (file, directory, NTFS
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
  (time since the owning file's last change, from under a day to three years or more), size (under
  64 KB, then 16x steps to 4 GB or more) and file class (see Classification). Zone fit and plan preview come with the
  planner.
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

## Scale volumes

- `make-scalevol <drive> --files N` (T063a, plan.md 12) fills an ICD-TEST volume (normally a thin VHDX made with
  `make-testvol.ps1`) with files that are allocated with `SetEndOfFile` + `SetFileValidData` and never written:
  log-uniform sizes from `--min-size` to `--max-size` rounded to whole clusters, 1,000 per directory under
  `\scale\aN\bM\`, a `--fragmented` share grown in eight interleaved steps four at a time. Writers take whole
  directories, seeded per directory, so the files are the same for any thread count (their placement is NTFS's).
- NTFS still writes once where each new file's data begins (when it turns non-resident; buffering, valid data length
  and reserving the allocation first make no difference), so the VHDX grows by about one block per file: small files
  cost their full size on the host, large ones 1-2 MB each. A 1 M-file volume (sizes up to 1 MB, 150 GB allocated)
  takes 294 s to fill and 139 GB on the host. Fills at the 30 TB scale must keep files few and large accordingly.

## Scan benchmarks (T067, Phase 4 exit)

- `scan-bench <drive>` (tools/bench) on a mounted ICD-TEST volume with a USN journal: bitmap and free runs, full MFT
  scan with the disk's busy share, snapshot memory, compressed save and load, a "typical day" of 10,000 changes (new,
  grown, deleted, renamed files) followed by the USN catch-up checked against a full rescan, and the scan capped to 2
  workers, a 2 GB budget and mapped storage checked against the uncapped one.
- NTFS cannot address 30 TB with 4 KB clusters (2^32 clusters is 16 TB), so the 30 TB scale volume uses 8 KB clusters
  (3.66 G clusters, a 458 MB bitmap); plan.md 6.1's 4 KB figures hold up to 16 TB.
- Results, 30 TB thin VHDX on the C: SSD, 2,002,031 files (2 M chosen to keep the host file at 51 GB; filled by
  `make-scalevol` in 8 min), 2,455,110 extents, 31 workers:
  - bitmap and free runs 0.16 s (target 15 s);
  - full MFT scan 1.58 s; `$MFT` 1.91 GB read in 0.33 s with the disk 100% busy (device counters 98.7%) (target
    90%); the parsers keep up with 6 GB/s, far above an HDD's 180 MB/s, so on the target HDD the scan is bounded by
    the read (about 2 min for 20 GB);
  - snapshot memory 181 MB; the estimate for 20 M files and 60 M extents is about 4.5 GB (target 8 GB);
  - save 0.22 s, load 0.25 s, 34 MB compressed;
  - catch-up after 10,000 changes (50,002 journal records, 5,030 records read) 1.14 s and equal to a full rescan
    (target 10 s);
  - capped scan (2 workers, mapped storage) 1.37 s and identical.
  On the 1 M-file volume (4 KB clusters): MFT scan 0.54 s, catch-up 0.54 s, capped identical; there the API scan took
  23.5 s against 0.48 s for the MFT scan.
- Catch-up time grew with the snapshot while every kept file was finished again (20 M files extrapolated to about
  11 s); since T069 the saved snapshot is patched (see USN catch-up).
- T069 on a 10 M-file scale volume (30 TB thin VHDX on the E: HDD, 8 KB clusters, filled by `make-scalevol` in 50 min,
  215 GB on the host; large test volumes stay off the SSD): full MFT scan 50.6 s, `$MFT` 9.55 GB read at 226 MB/s with
  the disk 100% busy, snapshot 889 MB, save 0.65 s / load 0.79 s; catch-up after 10,000 changes (52,500 journal records,
  5,031 records read) 3.51 s, patched, and equal to a full rescan (target 10 s); capped mapped scan identical. The
  finish stage alone at 20 M files: see `catchup-bench` under USN catch-up.

## Classification

- `core::classify::RuleEngine` (T070, plan.md 8.2): ordered rules, the first match gives the class (`ClassId`, one byte
  per file, an index into the rule set's class names); no match gives the fallback ("Unknown"). A rule's conditions
  all have to hold: path globs (any of them), extensions (any), size range, age range (days since the last change,
  from a fixed "now" so results are reproducible), file flags that must be set or clear, and churn (any, churning,
  stable). Specific rules go first (repository packs before sources, build folders before extensions).
- Path globs work on components (`/` or `\`): `*` and `?` inside a name, `**` for any number of folders, anchored at
  the volume root unless they start with `**`; ASCII letters compare case-insensitively, as NTFS names do. Paths are
  never built: all globs of a rule set are one automaton (state i of a glob = its first i parts matched; `**` stays on
  a part and is skipped by closure), its state set (bit words) computed once per directory, level by level from the
  roots in parallel, and stepped once more per file name. Extensions are looked up once per distinct name in the pool
  (a table of all rule extensions, a bitset per rule). Classifying a snapshot is then one parallel pass over the files;
  the result is the same for any executor.
- Per-directory overrides (plan.md 8.2): a directory path and a class; the deepest override above a regular file beats
  every rule. Directories keep their rule class, because directory indexes are placed by their own rules (plan.md 8.3).
  Overrides are found by walking names from the roots and inherited down the same level pass.
- `classifyPath()` classifies one path with facts, by the same automaton and override rules; tests require it to give
  every file of a snapshot exactly what `classify()` gives (hand-made dev tree and a 20 K-file synthetic volume).
- The classes are data (T071): `data/classes.json` lists the classes of plan.md 8.3-8.6 (source and config, repository
  data, dependencies, build output, temp, archives, disc images, VM disks, backups, photos, catalogs and thumbnails,
  video, unknown) plus NTFS metadata and directories, each with an id, display name, color and description, and the
  ordered rules (paths, extensions, sizes as bytes or "100 MB", flags, churn). CMake embeds the file into `icd_core`
  (`cmake/EmbedText.cmake` writes a char array, so no data file is looked up at run time and an edit rebuilds it);
  `classify::builtinCatalog()` parses it once. `parseCatalog()` refuses unknown keys, classes, flags, bad sizes, colors
  and patterns and other format versions with a message saying where.
- Notable rule decisions: loose `.git/objects/xx/*` files are temp (churn), packs repository data; a `bin` folder is
  build output but a `.bin` file elsewhere is a disc image; videos count as video from 100 MB, smaller ones stay
  unknown; a large `.ts` is video, a small one TypeScript; anything the journal shows as churn is temp, unless an
  earlier rule (packs, build output) already matched. `CatalogTest` has one test per class with typical and edge cases.
- `core/format/JsonReader` is the JSON reader the data files need (a small value tree with members in file order,
  RFC 8259 numbers and escapes, line and column in errors, 64 levels deep at most).
- Last-access times (T072): `platform::readLastAccessSetting()` reads `NtfsDisableLastAccessUpdate` (no administrator
  rights needed) and decodes 0x80000000-0x80000003 (user or system managed, on or off) and the older 0 and 1. The setting
  is system-wide, so every NTFS drive gets the same "Last access on/off" badge from it (this machine: system managed,
  on, which `LastAccessSettingTest` checks against `fsutil behavior query disablelastaccess`).
- The snapshot adds evidence (`classify::gatherAccessEvidence()`): regular files whose access time lies more than an
  hour (NTFS's update granularity) after both creation and the last change were read after writing; a copied file
  (created now, modified earlier, accessed at copy time) is not. `assessAccessTimes()`: off means unreliable whatever
  the files show (old accesses survive turning updates off); on with any such reads means reliable; without a setting,
  5% or more is reliable and under 0.5% unreliable, and fewer than 100 files never decide. The analysis puts the
  verdict and its reason on the Summary tab ("Last-access times") and into the JSON report.
- USN churn (T073, `services/activity/ChurnStats`): `collectChurn()` reads the journal's whole retained window and
  counts sessions, not records: creations and data changes come from close records, whose accumulated reason bits sum
  up the session. Deletes count from their first record (the close record waits until every other handle closes, and a
  scanner such as Defender often holds a new file), once per file reference, and for the directory the file was last
  seen in: a POSIX delete with another handle open first moves the file into `\$Extend\$Deleted`, and the delete's
  records name that directory (found on the dev scenario: a third of a temp folder's deletes landed there). Counts are
  kept per file reference and per directory reference (the files directly in it, deleted ones included).
- A directory churns when its files were deleted at least 10 times and at least a quarter as often as files were
  created there (build output, temp; sources and culled photo folders do not), or, with at least two deletes at that
  share, when most of its sibling directories churn (the last, small folder of a build tree). A file churns with 10 or
  more sessions of its own (a log appended all the time). Metadata directories never count. `churnFlags()` marks churning files, churning directories and everything below
  them, per snapshot file, for the rule engine (`ClassifyInputs::churn`).
- The `dev` scenario rebuilds the same object files in each of its three build cycles (fresh contents, as a real
  rebuild) and now writes and deletes temp files in `repo-N\.cache` in five cycles. `icd-cli churn <vol> [--json]`
  scans a volume and lists its churning directories; `tests/integration/churn/check-churn.ps1` creates the journal
  before writing the dev scenario and requires every build output and temp directory, and no source, dependency or
  repository directory, to be churn.
- Classes in the analysis and on the map (T074): after the summary, `analyzeVolume()` reads the journal's churn (when
  the volume keeps a journal; a failure only drops the churn signal), classifies every file with the built-in catalog
  (`services::classifyVolume()`, ages from the scan time) and fills the Summary tab's class table (files and bytes on
  disk per class in catalog order, "In target zone" shown as "-" until a strategy exists) and the JSON report
  (`summary.classes`; each dumped file's `class`). The final map is built with the classes; partial maps during the
  scan have none. Fake drives classify their synthetic volume for the map too.
- The class color mode (`ColorMode::FileClass`, View > Color mode > File class, Alt+1) colors a bin by the class of its
  largest contributor (the pyramid's `kind`), blended with free space by its fill, in the catalog's colors; the legend
  lists the classes and counts them exactly like the other modes. Tiles read a per-record class byte
  (`MapFileFacts::extentKinds`) in record order. `MapData::update()` takes the new classes and adds the ranges whose
  class changed on extents that did not move (a renamed folder reclassifies what is below it, though the catch-up only
  re-read the folder), so an updated map still equals a fresh build. The hover tooltip adds a "Class:" line.
- `tests/integration/classes/check-classes.ps1` (Phase 5 exit): on a read-only `mixed` volume every file below `\mixed`
  has the class the scenario's layout calls for (written out independently of the rule engine), the summary's bytes
  per class equal the dumped files' clusters, and the class counts cover every file. On 2 GB volumes the scenario's
  videos stay under 100 MB and are rightly unknown; 4 GB volumes have video. Tile colors equal legend colors in the
  class mode (`ColorModesTest`).

## Strategies (zone scripts)

- `core::strategy::ZoneScript` (T080, plan.md 8.1) is a strategy as data: zones in disk order, each with a selector
  (catalog classes, path globs, extensions, size and age ranges, file flags, churn; no condition = every file; files go
  to the first zone that matches), an anchor (front only for the first placed zone; back zones last, and only
  pack-to-back zones are anchored at the back), an order with optional per-level overrides (video: none at Fast and
  Moderate, age at Maximum), an action, `fragmentOk` (64 MB by default, "whole" = one fragment) and a gap (the largest
  of a share of free space, a share of the volume, a size and a multiple of some classes' bytes; the churn gap of 8.3 is
  `max(5% of volume, 2 x build + temp)`). Keep-in-place and defragment-only zones have no place on the volume.
- Every order has one direction (size: smallest first; age, modified, created, accessed: newest first, oldest at the
  end), so "position increases" means the same in every zone, front or back.
- `parseZoneScript()` is strict like the catalog reader (unknown keys, values, classes, flags, bad sizes and globs) and
  reports every issue at once, each with its zone, through `ZoneScriptError`; `validateScript()` gives the same issues
  for an edited script (Strategy tab). `zoneScriptJson()` leaves defaults out and reads back to an equal script; sizes
  are written as "64 MB" (`core/format/ByteSize`: `parseSizeSpec()`, `sizeSpecText()`).
- `core::strategy::Effort` is core's copy of the effort level, so the planner needs no services types.

## Planner

- `core/planner` (Phase 6, plan.md 9) builds plans from a snapshot, its classes and a zone script. It never touches
  the disk; services run it on the pool (6.4) and the executor (Phase 7) carries a plan out.
- Zone assignment (T081, `ZoneAssignment`): every file goes to the first zone whose selector matches (class table,
  size, age, flags, churn; globs and extensions through one rule engine per zone that has them) and gets a role:
  Fixed (unmovable, metadata, resident, encrypted or no movable clusters; encrypted files move only after they pass
  on the test bed, plan.md 10.2), InPlace (keep-in-place zones), Defragment (defragment-only zones and files no zone
  takes) or Placed. Movable clusters exclude unmovable extents, so a file with a pinned attribute still moves its data.
- Target layout (T081, `TargetLayout`): obstacles are unmovable extents, every file that is not Placed (in-place and
  defragment-only files stay near where they are) and reserved runs (the MFT zone). `AvailableAxis` numbers the
  remaining clusters 0..N in LCN order, so a zone of N clusters gets exactly N usable ones and zones flow around
  obstacles. Forward zones fill from the front in script order, each followed by its gap; back zones end at the end
  of the volume (the last one exactly there, its gap dropped). What is left is slack; `LayoutInputs::slackBefore`
  decides how much of it precedes each forward zone (plan.md 9.3.2, chosen by the lower bound), by default all of it
  follows the last forward zone. Gaps shrink in proportion when free space is short, and the MFT zone is given up
  when the files only fit with it; both leave a note.

- Plans (T082, `Plan`): ordered moves, each taking all movable clusters of one file (sources in VCN order, so the
  executor maps them onto VCNs by re-reading retrieval pointers) into destination runs in the same order. Runs live in
  one flat array. A plan accounts for itself against the lower bound: moved clusters = lower bound - skipped
  (files that could not move, with a reason) - deferred (left for a later run by a budget) + extra (parking and
  evictions). A parked file has a Park move and later a move `fromParking`.
- `HoleSet` is the mutable free-run set shared by the validator and the allocators: sorted, merged holes in blocks of
  256-1024 runs with a max-length segment tree over the blocks, so 17 M free runs cost 16 bytes each and take, add and
  first/last-fit queries in either direction stay logarithmic plus one block scan.
- `validatePlan()` replays a plan on a copy of the free runs and the moved files' runs (plan.md 9.5): each
  destination must be free when its move runs, the sources must be the file's current movable runs (so unmovable
  files and streams are never touched and a stale or reordered source list fails), clusters are kept, moves into a
  zone land inside it (given a layout), each file moves once (a parked file returns exactly once), the accounting
  above holds exactly, and the plan's totals equal the replay's. Tests break a plan for every rule.

- Lower bound (T083, `LowerBound`): a Placed file must move unless all its movable runs lie in its zone's pieces and its
  fragments are good enough (one fragment, or every fragment at least the zone's `fragmentOk`; at Maximum the whole
  file must be one fragment); a defragment-only file (or one without a zone) only when its fragments are not; fixed and
  in-place files never. Their clusters are the lower bound every plan reports against.
- Target fitting (T083, `fitTarget()`, plan.md 9.3.2): a forward zone's position is its slack-free start plus the slack
  before it, so a file in that zone stays for one interval of slack values. The slack per zone must not decrease from
  zone to zone; a dynamic programme over candidate values (0 and every interval's low end, which provably contain an
  optimum; thinned to 1,024 evenly spread ones on large volumes) picks the split that keeps the most clusters in place.
  Front zones never shift. On random small volumes it equals brute force over every split.

- Good-enough filter (T084, `filterCandidates()`): the must-move files of the lower bound split per zone, plus the
  defragment-only ones; everything else (good enough, churn, fixed) is never looked at again.
- Placement (T084, `Placement`): first, last and nearest fits on a `HoleSet` that skip the file's own runs (a move's
  destination may not overlap its sources), and large pieces (each at least `fragmentOk`, the last one too) when no
  single run holds a file. "Nearest" covers the point itself when the hole there is big enough.
- Defragment-only (T084, `planDefragment()`): largest files first, each to the free run nearest its first cluster,
  else large pieces when its zone allows them, else skipped (no space). What a move frees goes back into the pool for
  later files, which keeps the moves valid in the order they were planned.

- Destinations come before moves (plan.md 6.4 step 5): zone planners return `Destination`s (file, new runs, reason,
  zone) taken from one pool, the space free after the plan (free runs plus every candidate's runs); ordering into moves
  is a separate step. Zone ranges are disjoint, so zone planners never compete for space.
- Fast-fill (T085, `fastFill()`): walks the region's holes from the packed end and fills each with the largest waiting
  file that fits (best fit by size; a union-find over the size-sorted files finds it in near-constant time), skipping
  files whose own runs are in the way. Files no hole holds try any single run of the region, then large pieces when
  `fragmentOk` allows, else overflow to the caller. The region is the zone's exact target, so when everything fits the
  bytes moved equal the lower bound. On volumes whose free space is scattered small gaps, large fragmented files find
  no hole inside an exact zone and overflow (measured: a fifth of the files, most of the bytes, on a 60% full synthetic
  volume); they are placed next to the zone or make room by evicting small files (T088).

- Sorted zones (T086, `SortZone`): `SortKeys` gives every file one 64-bit key per order with the file index as tie-break
  (directory+name: the parent's rank in a depth-first walk with children in name order, then the name's rank; names
  compare ASCII case-insensitively). `applySortBounds()` takes a sorted zone's files that are already in place, finds
  the byte-heaviest subsequence already in key order (`weightedLis()`, Fenwick tree, O(n log n), equal to brute force)
  and adds the rest to the lower bound; at Moderate a zone is sorted only when that moves at most the tolerance share of
  its in-place bytes, otherwise it is fast-filled. Never at Fast. `sortZone()` puts each mover into the gap between the
  anchors that bracket its key and packs every gap in key order; a gap that cannot hold its movers gives up the anchor
  that ends it (the zone's last gap the one before it), which then moves as extra. `Move::extra` marks such moves,
  parking and evictions, and the validator sums them into the plan's extra.

- Pack-to-back (T087, `packToBack()`): the zone's target already ends at the volume end, so packing is fast-fill from
  the back (holes nearest the end first, closing the holes deleted videos left) or, when the zone has an order at the
  level (video: age at Maximum, newest first so the oldest end up last), the sorter. Videos already packed with
  good-enough fragments are not candidates, so they never move at any level.

- Move ordering (T088, `orderMoves()`): move j depends on move i when j's destination overlaps i's sources. Kahn's
  algorithm emits ready moves in destination order (a min-heap of indexes), so the plan is deterministic. When only
  cycles remain, Tarjan (iterative) finds the components; every component nothing else feeds is a cycle to break. At
  Maximum its smallest file is parked first in scratch space (free runs no destination touches), which frees its
  sources at once; its return waits for the move's own dependencies (a parked file's edges are ignored from then
  on). Elsewhere, or without scratch space, the cycle is dropped with every move that waited on it (skipped as Cycle
  or Dependency; extra moves are dropped without a skip entry, they were never in the bound).
- Move the smaller thing (T088, `findRoom()`): for a file no free run holds, the window of its size inside the region
  whose clusters are free after the plan or belong to files that may move, evicting the fewest clusters within a
  limit; windows start at holes and at evictable files, a bounded number per piece.

- Budgets (T089, `applyBudget()`): each move gets a benefit (4 per fragment removed, 2 for reaching its zone, 1 for
  any hole it closes; extra moves 0). Moves are taken by benefit per cluster, each together with the earlier moves it
  depends on, while the cluster and time budgets last (time = 2 x bytes / throughput + a seek per move, `TimeModel`);
  extra moves come only with the moves that need them. The kept moves are reordered with the most valuable ready one
  first, so every prefix is valid and leaves the volume better. Deferred clusters are reported; plans with parking
  (Maximum, unbudgeted) are left whole.
- `orderMoves()` also drops, before ordering, any destination that lands on a file that does not move (a zone planner
  could not place a candidate whose clusters were offered to others), with everything that waits on it.

- VSS (T090, plan.md 9.4): `platform::countShadowCopies()` runs Windows' own listing (`vssadmin list shadows`, the
  fallback the plan names; the VSS API would start the writer machinery for a read-only question) and counts the
  volume's GUID path, which does not depend on the display language; exit codes above 1 (no rights) give "unknown".
  `core::planner::VssAlign`: the copy-on-write unit is 16 KB / cluster size (4 at 4 KB, 1 from 16 KB, where every move
  is aligned); a move is aligned when every piece keeps its offset in the unit and it moves at least 16 KB.
  `alignDestinations()` shifts an unaligned single-run destination by up to unit - 1 clusters into after-plan free
  space when it stays in its zone and off the file's own runs; `vssReport()` counts unaligned moves and estimates
  shadow storage growth as the bytes they rewrite.

- Levels (T091, `LevelPlanner`): `PlanInput` (snapshot, script, catalog, classes, churn, now, reserved MFT zone, shadow
  copies, Fast budget multiplier, time model) is shared by the levels; `assignInput()` assigns once. `planZones()` is
  the Moderate/Maximum pipeline: fitted target, lower bound with sorting, candidates, an after-plan pool without the
  MFT zone, every zone placed in parallel on its own slice of the pool (zone ranges are disjoint, so the result does
  not depend on the thread count), defragment-only files near where they are, overflow handled by evicting small files
  of the same zone (when the level allows) or next to the zone's end, VSS alignment, ordering, budget, metrics
  (`PlanMetrics`: fragmented files, zone fit, free runs before and after, time).
- Fast (T091, `levels/FastLevel`, plan.md 9.2.1): stripes are scored 0.4 x churn share (journal churn or in-place files)
  + 0.3 x small holes (under 1 MB, 32 give the full score) + 0.3 x share held by fragmented files; stripes scoring 0.25
  or more are hot, a single cold stripe between hot ones is bridged, and runs of them are hot spots. Only files lying
  wholly inside a hot spot move: each hot spot is packed towards its head (its files that are not inside the first
  "need" usable clusters, or fragmented under fragment_ok, are fast-filled there), so its free space ends up as one run
  at its tail. A big long-lived file (64 MB or more, 30 days unchanged) with free space on both sides goes straight to
  its zone. No overflow handling (that would touch other areas); budget 2% of used space or 15 minutes times the
  multiplier; the plan carries the "run again" note.

- Moderate and Maximum (T092, `levels/ModerateLevel`, `levels/MaximumLevel`): policies over `planZones()`. Moderate:
  zones' fragment_ok, sorted zones only within a 20% tolerance, small files evicted for large ones, no parking, budget
  20% of used space. Maximum: whole files, exact order, eviction, parking, no budget. On synthetic volumes (50-80%
  full) both validate, Moderate moves no more than Maximum and reaches no more zone fit; Maximum reaches about 90% zone
  fit and no fragmented files at 50-65% fill, with about 1.0-1.09 x the lower bound moved (the rest is parking and
  evictions).
- Placement runs in up to three rounds: a candidate that finds no space at all stays where it is and the round is
  planned again without it, so no other file is promised its clusters (at 80% fill this stopped cascades of dropped
  dependents). At levels that may not park, a cycle is broken by sending its smallest file to scratch space for good
  (one move, outside its target; pieces of 64 MB or more when no single run holds it); a parked file whose return can
  no longer happen stays in scratch space, its park becoming its one move.

- Planning job (T093, `services/plan/PlanJob`): `planLevels()` assigns zones once and plans Fast, Moderate and Maximum
  in parallel on the pool (or one after another when memory is short), each using the pool inside, then validates each
  plan; the plans equal the single-threaded ones move for move (`PlanJobTest`). `makePlanRequest()` builds a request
  from a drive (MFT zone reserved, shadow-copy badge, ages from now); `makePlanJob()` wraps it as a job with a
  `PlanOutput`; `planSummary()` gives the Plan tab's figures.
- Scale (T093, `plan-bench`, 8 workers, validation off): 800 K files 7 s, 2 M files 11.6 s, 6 M files 42 s for all three
  levels; Maximum moves 1.02-1.05 x its lower bound. 30 TB / 20 M files: 199 s (Fast 131 s, Moderate 172 s, Maximum
  199 s, every plan valid, Maximum 1.039 x its bound), so the plan.md 6.5 target of 60 s is not met yet (T093b). Found on the way and fixed: one strongly connected component holding thousands of cycles broke one file per
  round (now every move with an edge back into the Tarjan walk is a candidate: the smallest quarter is parked per round,
  all are redirected at once below Maximum); leftover placement searched every zone piece per file (fast-fill now works
  on its own slice and searches the region's span once; room finding walks holes lazily); pools are built in one sorted
  pass; `HoleSet` rebuilt its tree by rescanning every run whenever a block emptied (empty blocks now stay, searchable by
  a non-decreasing stored end, and are dropped in bulk); budget closures stop as soon as they exceed the budget.
- Placement keeps re-planning (up to eight rounds) until no file fails for lack of space, and Maximum holds back a
  scratch reserve (the largest free run outside every zone, 1% of the volume or the largest must-move file) before
  overflow and defragment-only files take free space, so parking has room.

- Presets (T094, `core/strategy/Presets`, `data/strategies/*.json`): 14 built-in zone scripts embedded at build time (one
  generated source each through `cmake/EmbedText.cmake`) and parsed once against the built-in catalog: the storage
  presets of plan.md 8.3-8.7 (mixed first as the default, development with the churn gap, archive, photos, video) and the
  general strategies of 8.8 (defragment only, hot zone, cold zone, directory clustering, type segregation, size placement,
  access-time sort, free-space consolidation, read-ahead packing). Ids are stable settings keys; the six ids of the Phase 1
  picker stay. Property tests plan every preset at every level on six random synthetic volumes (4 KB and 16 KB clusters,
  40-85% full, every free pattern, an MFT zone): every plan validates and never leaves more fragmented files; below
  two-thirds full Moderate never moves more than Maximum (above, Maximum can leave cycles it cannot park, plan.md 9.3.9).

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

## Test runs

- Unit tests carry a CTest label (T068): `slow` for the tests that read real volumes and disks (administrator rights,
  mounted ICD-TEST volumes, the Windows drive), listed in `ICD_SLOW_TESTS` in `tests/CMakeLists.txt` (two
  `gtest_discover_tests` calls with opposite `TEST_FILTER`s), `fast` for all others. `test.bat` runs only `fast` unless
  asked (`slow`, `all`): about 260 fast tests in seconds, 17 slow ones in a few more. The USN enumeration test walks
  scenario volumes only (at most 100 K MFT records), not scale volumes.
- The integration scripts under `tests/integration` run only by hand. By default they take a quick pair of scenarios
  (`special` for every special case, `mixed` for a realistic volume) on 2 GB volumes; `-Scenarios all` runs every
  scenario. Quick runs: `check-scanners.ps1` about 5 min, `check-catchup.ps1` (2 rounds) about 1.5 min.
- Found by the quick run: files below `$Extend` metadata the API path does not list (TxF's
  `$Extend\$RmMetadata\$Txf\...`) sit under the root there and are not metadata; the MFT path places them right, so
  `compareWithApi()` compares their other fields and runs only (`underUnlistedMetadata`).

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
