#!/usr/bin/env bash
# Publishes the product site to https://ironclad.clau.se (T143). Run from WSL:
#   bash deploy/publish-web.sh [--dry-run] site|downloads|all
#
#   site       web/out/ (the static Next.js export) -> pupa:/home/defrag/www/, deleting remote files the export no
#              longer has. www/downloads/ is excluded, so installers survive every site deploy.
#   downloads  dist/*-setup.exe and their .sha256 files -> pupa:/home/defrag/www/downloads/, never deleting: older
#              installers stay downloadable. A missing or stale .sha256 is written next to the installer first.
#   all        site, then downloads.
#   --dry-run  show what would change (rsync -n), change nothing.
# Files land owned by defrag, folders 755 and files 644, whatever the Windows permissions were.
#   DEPLOY_HOST  ssh host (default pupa; its ssh config logs in as root)
set -euo pipefail

HOST=${DEPLOY_HOST:-pupa}
REMOTE=/home/defrag/www
REPO=$(cd "$(dirname "$0")/.." && pwd)
SITE="$REPO/web/out"
DIST="$REPO/dist"

DRY=()
if [ "${1:-}" = "--dry-run" ]; then
    DRY=(--dry-run)
    shift
fi
WHAT=${1:-}
case "$WHAT" in site|downloads|all) ;; *)
    echo "Usage: bash deploy/publish-web.sh [--dry-run] site|downloads|all" >&2
    exit 2 ;;
esac

# Shared by both uploads: times kept, ownership and modes set on the server, a line per changed file.
RSYNC=(rsync -rlt --compress --itemize-changes --human-readable --stats --chown=defrag:defrag --chmod=D755,F644)

publish_site() {
    # An empty or missing export with --delete would wipe the live site, so refuse it.
    if [ ! -f "$SITE/index.html" ]; then
        echo "No $SITE/index.html: build the site first (cd web && npm run build)." >&2
        exit 1
    fi
    echo "== Site: $SITE/ -> $HOST:$REMOTE/ (deleting what the export no longer has; downloads/ kept)"
    "${RSYNC[@]}" "${DRY[@]}" --delete --exclude=/downloads/ "$SITE/" "$HOST:$REMOTE/"
}

publish_downloads() {
    shopt -s nullglob
    local setups=("$DIST"/*-setup.exe)
    if [ ${#setups[@]} -eq 0 ]; then
        echo "No $DIST/*-setup.exe: build the installer first (make-installer.bat)." >&2
        exit 1
    fi
    for setup in "${setups[@]}"; do
        local sum="$setup.sha256"
        if [ ! -f "$sum" ] || [ "$setup" -nt "$sum" ]; then
            (cd "$DIST" && sha256sum "$(basename "$setup")" > "$(basename "$sum")")
            echo "   wrote $(basename "$sum")"
        fi
    done
    echo "== Downloads: $DIST/ -> $HOST:$REMOTE/downloads/ (nothing deleted)"
    "${RSYNC[@]}" "${DRY[@]}" --include='*-setup.exe' --include='*-setup.exe.sha256' --exclude='*' \
        "$DIST/" "$HOST:$REMOTE/downloads/"
}

case "$WHAT" in
    site) publish_site ;;
    downloads) publish_downloads ;;
    all) publish_site; publish_downloads ;;
esac
[ ${#DRY[@]} -eq 0 ] && echo "== Published: https://ironclad.clau.se/" || echo "== Dry run: nothing changed"
