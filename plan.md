# IroncladDefrag Development Plan

## Goal

Build IroncladDefrag into a Windows x64 GUI disk analyser and defragmenter with a strong focus on data-drive optimization. The primary workflow is:

1. Show drives selection. For mechanical drives user can choose a further action:
    1. Analyse a selected drive.
    2. Classify files by size, type, directory, access patterns, and current physical layout.
    3. Choose a configurable optimization mode.
    4. Produce a dry-run move plan that minimizes moved data.
    5. Execute the plan with progress, cancellation, and clear error reporting.
2. For SSD drives offer running a TRIM action using default Windows defrag tool.

The first production-quality target should be safe, read-only analysis and planning. Actual file movement should come after the model, planner, UI, and validation paths are solid.

## Product Focus

The main optimization scenario is a data drive containing a mixture of large and small files. The application should support sorting and packing those files into configurable disk zones:

- Fast zone: preferred area for frequently accessed files, small random-access-heavy files, indexes, project files, and user-selected hot directories.
- Balanced zone: default area for ordinary files that do not strongly benefit from hot or cold placement.
- Slow zone: preferred area for rarely accessed files, archives, old media, backups, and user-selected cold directories.
- Contiguous large-file zone: preferred area for large files that benefit from sequential layout, such as videos, images, disk images, VM files, archives, and game assets.
- Free-space reserve zone: configurable contiguous free-space target used to reduce future fragmentation.

The zone layout must be configurable because different drives and users will disagree on what belongs near the start, middle, or end of the drive. The system should expose presets first, then advanced settings.

## Architecture Direction

Keep the existing layering:

- `src/ui`: wxWidgets windows, controls, menus, view state, progress display, and user commands.
- `src/controller` or `src/app`: orchestration, current drive selection, active analysis result, selected strategy, job lifecycle, cancellation, and UI-facing view models.
- `src/model`: value types for disks, zones, files, fragments, free space, classification, strategy settings, and move plans. This layer should not depend on wxWidgets.
- `src/analysis`: read-only drive enumeration, volume metadata, file scanning, cluster lookup, free-space scanning, and fragmentation scoring.
- `src/strategy`: optimization modes that turn analysis data and settings into a target layout.
- `src/planner`: conversion of target layout into a minimal move plan.
- `src/execution`: privileged Windows file movement, progress, cancellation, error recovery, and post-run verification.
- `src/platform/windows`: narrow wrappers around Win32 volume, file, handle, USN, privilege, and FSCTL APIs.

When adding these directories, update `CMakeLists.txt` explicitly.

## Phase 1: Project Foundation

Deliver a stable skeleton before adding disk operations:

- Add testable model constructors, accessors, and invariants for existing model classes.
- Add domain models for `DriveInfo`, `VolumeInfo`, `DiskZone`, `FileClass`, `OptimizationProfile`, `OptimizationSettings`, `AnalysisResult`, `PlacementPlan`, `MovePlan`, `MoveOperation`, and `JobProgress`.
- Add a controller/service boundary so `MainFrame` does not own analysis or movement logic.
- Add a background job abstraction with cancellation and progress callbacks.
- Add logging suitable for diagnosing drive scans and move planning.
- Add basic unit tests for model and strategy code if a test framework is introduced.
- As the modern drives can contain a very large amount of sectors and clusters, this analysis should be running separately from the UI thread.

Exit criteria:

- The GUI can start a fake analysis job without blocking the UI.
- The controller can receive progress and completion events from a worker.
- Model code can be tested without wxWidgets.

## Phase 2: Read-Only Drive Analysis

Implement drive discovery and safe analysis before any movement:

- Enumerate local fixed drives and display volume label, file system, capacity, free space, cluster size, and privilege/capability status.
- Turn Analyze menu into a popup submenu with available mechanical disk drives. Other non-mechanical and network drives can be shown in 'disabled' state.
- Read drive geometry and map it to logical performance zones.
- Scan files and collect path, size, timestamps, attributes, file type, directory, and cluster extents.
- Build file fragmentation metrics from cluster extents.
- Build a free-space map and identify large contiguous free regions.
- Detect unmovable, risky, excluded, or system-managed files.
- Persist analysis snapshots in memory for planning without rescanning immediately.

Exit criteria:

- The GUI can show all drives, run analysis on one drive, and display summary metrics.
- No write or move operations are performed.
- Large drives can be scanned on a worker thread with cancellation.

## Phase 3: Data-Drive Classification

Create the classification system used by all optimization modes:

- Classify files by size buckets: tiny, small, medium, large, huge.
- Classify files by extension and broad type: media, archive, document, executable, source/project, backup, virtual disk, unknown.
- Classify files by recency: hot, warm, cool, cold, stale.
- Classify files by directory rules, including user-defined hot and cold directory overrides.
- Classify files by fragmentation cost and expected benefit from movement.
- Mark excluded files and files that should only be moved if explicitly allowed.

Default data-drive classification should prefer:

- Small hot files in the fast zone.
- Frequently accessed project or document trees in the fast or balanced zone.
- Large sequential media files in a contiguous large-file zone.
- Old archives, backups, installers, and rarely accessed media in the slow zone.
- Free space gathered into one or more large contiguous reserves.

Exit criteria:

- An analysed drive can be summarized by file class, size bucket, type, recency, and expected placement.
- Classification is deterministic and independent from the UI.

## Phase 4: Configurable Optimization Profiles

Add user-selectable modes and settings. Each mode should share a common settings model and strategy interface.

Initial profiles:

- Balanced data-drive optimization: sort hot, normal, cold, large, and free-space regions while minimizing moved data.
- Large-file contiguous optimization: prioritize contiguous placement for large sequential files.
- Small-file fast-zone optimization: place small hot files and active directories in the fastest zone.
- Cold archive optimization: move stale archives, backups, and rarely accessed files to the slow zone.
- Directory clustering: keep selected directory trees physically close together.
- File-type segregation: group files by type, such as media, archives, documents, and project files.
- Size-based placement: sort files by size buckets across configurable zones.
- Free-space optimization: consolidate free space while moving as little file data as possible.
- Single-file defragmentation: defragment one selected file or directory subtree.

Settings should include:

- Zone boundaries as percentages or absolute sizes.
- Fast, balanced, slow, large-file, and free-space zone enablement.
- Size thresholds for small, large, and huge files.
- Recency thresholds for hot, warm, cool, cold, and stale files.
- File type rules and extension groups.
- Directory override rules.
- Minimum benefit threshold before a file is moved.
- Maximum bytes to move per run.
- Whether to preserve directory locality.
- Whether to allow large-file moves.
- Whether to prioritize free-space consolidation.
- Dry-run only mode.

Exit criteria:

- The UI can choose a profile and edit its core settings.
- Strategy code can produce a target placement intent without executing moves.
- Settings are serializable for future persistence.

## Phase 5: Move Planning

Convert target placement into a safe dry-run move plan:

- Compare current extents with desired zones and identify candidate moves.
- Estimate benefit, cost, and risk for each candidate.
- Prefer moves that improve many fragmented or misplaced files with minimal total bytes moved.
- Avoid moving files that are already good enough.
- Reserve temporary workspace and destination ranges before planning dependent moves.
- Support partial plans when the full ideal layout is too expensive.
- Detect impossible plans before execution.
- Produce user-visible summaries: files affected, bytes moved, expected zone changes, fragmentation improvement, free-space improvement, and skipped files.

The planner should be conservative. A good first result is not a perfect global optimizer; it is a transparent plan that improves layout without excessive churn.

Exit criteria:

- Every optimization profile can produce a dry-run plan from an analysis snapshot.
- Plans are inspectable before execution.
- Plans include cancellation boundaries and rollback notes where applicable.

## Phase 6: Execution Engine

Add actual file movement only after planning is reliable:

- Implement Windows privilege detection and clear elevation requirements.
- Wrap volume and file handles with RAII.
- Use narrow Windows API boundary code for file extent lookup and movement.
- Execute move operations with progress and cancellation checkpoints.
- Revalidate files before moving because the drive may have changed after analysis.
- Skip changed, locked, unmovable, or unsafe files with clear reasons.
- Verify extents after each move or batch.
- Keep an execution log for audit and troubleshooting.
- Support dry-run as the default mode until the user explicitly starts execution.

Exit criteria:

- Single-file defragmentation works on controlled test data.
- Data-drive optimization can execute a small bounded plan.
- Failures are reported per file without crashing the app.

## Phase 7: GUI Workflow

Build the full user workflow around the service layer:

- All-disks panel with status, capacity, file system, and capability badges.
- Drive map panel showing zones, occupied regions, free space, fragmented files, selected file class, and planned moves.
- Analysis summary panel with fragmentation, free-space, hot/cold/large-file distribution, and recommendations.
- Optimization controls with profile selection and settings.
- Plan preview with affected files, bytes moved, estimated benefit, and warnings.
- Execution controls for start, pause/cancel where supported, and progress.
- Error and skipped-file views.
- Settings dialog for global defaults and per-profile configuration.

The first GUI implementation can be simple, but long-running operations must remain off the UI thread.

Exit criteria:

- A user can select a drive, analyse it, choose a data-drive profile, review a dry-run plan, and optionally execute it.

## Phase 8: Persistence And Safety

Add state persistence and guardrails:

- Save user settings and optimization profiles.
- Save recent analysis summaries if useful, but never trust stale snapshots for execution without revalidation.
- Add configurable exclusions for system paths, file types, directories, and size ranges.
- Add maximum moved-data limits.
- Add power-loss and interruption guidance in the UI.
- Add warnings for SSDs, removable drives, encrypted volumes, network drives, and unsupported file systems.
- Add a prominent dry-run path for every strategy.

Exit criteria:

- Users can configure repeatable optimization behavior.
- Risky drives or unsupported scenarios are blocked or clearly warned.

## Phase 9: Validation

Validate incrementally:

- Unit-test model invariants, classification rules, settings serialization, and strategy decisions.
- Test planners with synthetic drive maps and known free-space layouts.
- Test Windows API wrappers on controlled test volumes.
- Add large synthetic datasets for performance checks.
- Add GUI smoke tests where practical.
- Manually test with disposable volumes before any real data drive.

Important test scenarios:

- Mostly large media files with scattered free space.
- Many small files in active project directories.
- Cold backup/archive drive.
- Mixed drive with hot directories and stale large files.
- Almost-full drive with limited temporary free space.
- Drive with locked, hidden, system, sparse, compressed, or reparse-point files.

## Suggested Milestone Order

1. Model constructors/accessors, controller boundary, background job skeleton.
2. Drive enumeration and read-only analysis summary.
3. Cluster extent and free-space map scanner.
4. Data-drive classification rules.
5. Optimization settings and profile UI.
6. Dry-run placement planner for balanced data-drive optimization.
7. Plan preview UI.
8. Single-file defragmentation execution on controlled data.
9. Bounded data-drive plan execution.
10. Advanced profiles, persistence, and broader validation.

## Non-Goals For Early Versions

- Perfect global disk layout optimization.
- Automatic unattended movement of large amounts of data.
- Moving files without dry-run preview.
- Supporting every Windows file-system feature in the first implementation.
- Running disk analysis or movement on the UI thread.

