#!/usr/bin/env bash
# One-time server setup for ironclad.clau.se on pupa (T143). Run from WSL: bash deploy/setup-server.sh
#
# Creates the site user `defrag` (no login shell) with /home/defrag/www and www/downloads, lets nginx (www-data)
# through the home folder by ACL as the other sites do, puts a placeholder page there while the site does not exist,
# installs and enables deploy/nginx/ironclad.clau.se, reloads nginx after `nginx -t`, and gets the certificate with
# certbot (TLS and the HTTP -> HTTPS redirect are added to the server copy). Safe to run again: every step that is
# already done is skipped, and an existing nginx site file is never overwritten (certbot has edited it).
#   DEPLOY_HOST  ssh host (default pupa; its ssh config logs in as root)
set -euo pipefail

HOST=${DEPLOY_HOST:-pupa}
DOMAIN=ironclad.clau.se
HERE=$(cd "$(dirname "$0")" && pwd)

echo "== Uploading the nginx site and placeholder to $HOST:/tmp"
rsync -t "$HERE/nginx/$DOMAIN" "$HOST:/tmp/$DOMAIN.nginx"
rsync -t "$HERE/placeholder/index.html" "$HOST:/tmp/$DOMAIN.placeholder.html"

ssh "$HOST" DOMAIN="$DOMAIN" bash -s <<'REMOTE'
set -euo pipefail
if id defrag >/dev/null 2>&1; then
    echo "== User defrag exists"
else
    echo "== Creating user defrag"
    useradd --create-home --shell /usr/sbin/nologin defrag
fi
install -d -o defrag -g defrag -m 755 /home/defrag/www /home/defrag/www/downloads
setfacl -m u:www-data:--x /home/defrag # nginx may pass through the 750 home folder, nothing more.

if [ -e /home/defrag/www/index.html ]; then
    echo "== www/index.html exists, placeholder not needed"
else
    echo "== Placing the placeholder page"
    install -o defrag -g defrag -m 644 "/tmp/$DOMAIN.placeholder.html" /home/defrag/www/index.html
fi

if [ -e "/etc/nginx/sites-available/$DOMAIN" ]; then
    echo "== /etc/nginx/sites-available/$DOMAIN exists, left as it is"
else
    echo "== Installing the nginx site"
    install -o root -g root -m 644 "/tmp/$DOMAIN.nginx" "/etc/nginx/sites-available/$DOMAIN"
fi
ln -sfn "/etc/nginx/sites-available/$DOMAIN" "/etc/nginx/sites-enabled/$DOMAIN"
rm -f "/tmp/$DOMAIN.nginx" "/tmp/$DOMAIN.placeholder.html"
nginx -t
systemctl reload nginx

if [ -e "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" ]; then
    echo "== Certificate for $DOMAIN exists"
else
    echo "== Requesting the certificate"
    certbot --nginx -d "$DOMAIN" --redirect --non-interactive --agree-tos
fi
nginx -t
systemctl reload nginx
echo "== Done: https://$DOMAIN/"
REMOTE

echo "== Checking from here"
curl -sS -o /dev/null -w "http  -> %{http_code} %{redirect_url}\n" "http://$DOMAIN/"
curl -sS -o /dev/null -w "https -> %{http_code}\n" "https://$DOMAIN/"
