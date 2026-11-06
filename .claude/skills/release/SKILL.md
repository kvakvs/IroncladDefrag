---
name: release
description: Release a new Ironclad Defrag version - bump the version, write its changelog, build the NSIS installer, record it in the site's versions list (website/data/releases.json) and upload installer and site to https://ironclad.clau.se. Use when the user asks to release, cut or ship a version, or types /release (optionally with a version like 1.0.1, or patch, minor, major).
---

# Release a version

A release is one version number with one installer and one changelog. Every released installer stays on the server
under `/downloads/` (uploads never delete there), and the site's versions page `/releases/` lists them all, newest
first, each with size, SHA-256 and a changelog that opens on click.

| Piece | Where |
| --- | --- |
| Version number | `project(IroncladDefrag VERSION x.y.z ...)` in `CMakeLists.txt` (goes into the app and the installer) |
| Versions and changelogs | `website/data/releases.json`, newest first: `version`, `date`, `changes`, then the installer fields |
| Installer fields | `commit`, `file`, `bytes`, `sha256`, filled by `deploy/record-release.ps1` from the built installer |
| Installer | `dist/IroncladDefrag-<version>-<commit>-setup.exe` from `make-installer.bat` (Release build + NSIS) |
| Upload | the `/deploy` skill's steps (`.claude/skills/deploy/SKILL.md`) |

Releasing is public. Invoking `/release` is the go-ahead for this one release; the changelog text still needs the
user's OK (step 2), and the upload follows `/deploy`'s rule: show the dry run, stop if it does anything unexpected.
Never push to git or create tags unless the user asks.

## Steps

1. **Check and pick the version.**
   - On `master` with a clean tree (`git status --porcelain` empty), else stop and say what is uncommitted: the
     installer must be built from a commit, not a "-dirty" tree.
   - The new version comes from the argument: an exact `x.y.z`, or `patch` / `minor` / `major` applied to the newest
     version in `releases.json`. Without an argument, ask (AskUserQuestion) with patch first and recommended, showing
     the resulting numbers. It must be greater than every version in `releases.json`: a released version is never
     rebuilt or replaced, a fix gets the next number.
2. **Write the changelog.**
   - Read what changed since the last release: `git log --oneline <newest commit in releases.json>..HEAD`, and the
     bullets of those tasks in `.agents/00-finished.md`.
   - Write 2-8 short bullets for users of the app, in the site's plain style: what they can do now or what works
     better, no task ids, file names or internals. Leave out changes users never see (deploy, docs, tests, tooling).
   - Show the version and the bullets, and ask (AskUserQuestion: publish as written / edit). Apply the user's edits.
3. **Release commit.**
   - Set the version in `CMakeLists.txt` (`project(IroncladDefrag VERSION x.y.z LANGUAGES CXX RC)`).
   - Add the entry at the top of `website/data/releases.json`: `{"version": "x.y.z", "date": "<today, YYYY-MM-DD>",
     "changes": [...]}`. Leave the installer fields out; step 5 fills them.
   - Run `test.bat` (fast tests); stop if anything fails.
   - Commit only these two files: `Release x.y.z` (no Co-Authored-By line). The installer is built from this commit.
4. **Build the installer:** `make-installer.bat` (several minutes: Release build, then NSIS). It prints
   `Installer: ...\dist\IroncladDefrag-x.y.z-<commit>-setup.exe`; the commit must be HEAD's short hash with no
   `-dirty`. Stop on any failure.
5. **Record it:** `pwsh -NoProfile -File deploy/record-release.ps1 -Version x.y.z`. It takes the installer built from
   HEAD, writes its `.sha256` next to it and fills `commit`, `file`, `bytes` and `sha256` into the entry. Check the
   diff touches only that entry, then commit `releases.json`: `Release x.y.z: installer <commit>`.
6. **Upload** with the `/deploy` steps: build the site with pnpm, then dry runs, then publish. Upload `downloads`
   before `site`, so the new link never points at a missing file. In the dry runs expect: the new installer and its
   `.sha256` added, nothing deleted under `downloads/`, and the site's changed pages and build files. Older
   installers already on the server show no change.
7. **Verify** from WSL: `https://ironclad.clau.se/` and `/releases/` answer 200, `/releases/` names the new version,
   and the served installer's SHA-256 equals its `.sha256` (as in `/deploy` step 5).
8. **Report:** the version, the installer link (`https://ironclad.clau.se/downloads/<file>`), its size and SHA-256,
   the changelog, and the two commits (not pushed).

## Notes

- The site shows only entries whose installer fields are filled, so an entry between steps 3 and 5 never shows a dead
  link, and the home page's Download buttons always point at the newest recorded installer.
- If the upload fails after step 5, the commits stay; rerun `/deploy` (`all`) to finish. If the build fails after
  step 3, fix the problem and continue from step 4 only if the fix needs no code change; a code fix means a new commit,
  so build from that (the entry's date and changes stay, the installer then names the new commit).
- Installers built before this skill (two 1.0.0 builds) are on the server; only `218867e` is listed as 1.0.0.
