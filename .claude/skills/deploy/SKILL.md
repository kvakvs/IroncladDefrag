---
name: deploy
description: Build and publish the Ironclad Defrag product site (static Next.js export of website/, built with pnpm on Windows into website/out) and/or the installers from dist/ to https://ironclad.clau.se on the server pupa, through WSL with ssh and rsync. Use when the user asks to deploy, publish or upload the site or a release, or types /deploy. Also covers the one-time server setup (site user, nginx host, certificate).
---

# Deploy to ironclad.clau.se

The site and its downloads live on **pupa** (Ubuntu, nginx, certbot), reached from WSL with `ssh pupa`; the ssh
config there logs in as root. Files go to the home folder of the site user `defrag`:

| What | Local | Remote | Deletes remote extras |
| --- | --- | --- | --- |
| Site (static Next.js export) | `website/out/`, built from `website/` | `/home/defrag/www/` (except `downloads/`) | yes |
| Installers + `.sha256` | `dist/*-setup.exe` | `/home/defrag/www/downloads/` | never: old installers stay |

The site is built on Windows with the native Node and pnpm (never npm; WSL has no Node). Uploads run through
`deploy/publish-web.sh` from WSL; never hand-write rsync commands for this.

Publishing is public: everyone can see what lands there. Invoking `/deploy` (or asking to deploy) is the user's
go-ahead for that one deploy, but always show the dry run first and stop to ask if it does anything unexpected.

## Steps

1. **Pick what to publish** from the user's request: `site`, `downloads` or `all` (default `all` when unclear and
   both exist).
2. **Build the site and check the inputs.**
   - Site (`site` or `all`): always build first, on Windows (PowerShell, from the repository root):
     ```
     pnpm --dir website install --frozen-lockfile
     pnpm --dir website build
     ```
     Next.js `output: "export"` writes `website/out/`. Stop if either command fails, and check that
     `website/out/index.html` exists: the site upload deletes remote files that are not in the export, so a stale or
     partial one must never go up. If `pnpm` answers "pnpm is not installed for this Node.js version", the nvm shim is
     ahead of it on PATH: the winget pnpm folder (`%LOCALAPPDATA%\Microsoft\WinGet\Packages\pnpm.pnpm_*`) must come
     before `nvm\.shim` in the user PATH.
   - Downloads: `dist/IroncladDefrag-<version>-<commit>-setup.exe` from `make-installer.bat`. Prefer a build whose name
     has no `-dirty` (a clean commit); if only a dirty one exists, ask before uploading it.
3. **Dry run** and read the itemized changes:
   ```
   wsl -e bash deploy/publish-web.sh --dry-run <site|downloads|all>
   ```
   Lines starting `*deleting` are remote files the site upload would remove. Expected: old build files (`_next/`
   chunks, renamed pages). Unexpected, so stop and ask: anything under `downloads/` (it is excluded, so this means
   something is wrong), or most of the site being deleted.
4. **Publish** with the same arguments without `--dry-run`:
   ```
   wsl -e bash deploy/publish-web.sh <site|downloads|all>
   ```
5. **Verify** from WSL and report the result:
   ```
   wsl -e bash -lc 'curl -sS -o /dev/null -w "%{http_code}\n" https://ironclad.clau.se/'
   wsl -e bash -lc 'curl -sS https://ironclad.clau.se/downloads/<setup>.exe | sha256sum; cat dist/<setup>.exe.sha256'
   ```
   The site answers 200, and each uploaded installer's served SHA-256 equals its `.sha256` file (which the site's
   download section shows).

## One-time server setup (done 2026-11-04; rerun only to repair)

Done: user `defrag`, the nginx host, a Let's Encrypt certificate (auto-renewed), the placeholder page. A rerun changes
the server as root, so the user runs it (or approves it) themselves.

```
wsl -e bash deploy/setup-server.sh
```

It creates the user `defrag` with `www/` and `www/downloads/`, lets nginx through the home folder by ACL
(`u:www-data:--x`), places `deploy/placeholder/index.html` while no site exists, installs `deploy/nginx/ironclad.clau.se`
into `sites-available` and enables it, and runs certbot (`--nginx --redirect`); renewals come from the snap's
`snap.certbot.renew.timer`. Every step that is already done is skipped. The server's nginx file is the live one
(certbot edited it): to change the host, edit `deploy/nginx/ironclad.clau.se`, apply the same change on the server by
hand, run `nginx -t` and `systemctl reload nginx`, and keep the certbot lines.

## Notes

- Requests: `/x` is served from `x.html`, `x/index.html` or `x/` (both Next.js `trailingSlash` settings work), the
  export's `404.html` for anything else; `/_next/static/` is cached for a year (hashed names); `/downloads/` is
  always a download, never a listing.
- Another host: `DEPLOY_HOST=<host> wsl -e bash deploy/publish-web.sh ...` (WSL passes only what `WSLENV` lists, so
  set it inside the WSL command if needed).
