#!/usr/bin/env bash
# Publish Scotty to the self-hosted Flatpak repo served from
# https://scottydev40.github.io/repo/ .
#
# Builds the flatpak from source (offline-in-sandbox, same as build-local.sh),
# exports it signed into the GitHub Pages OSTree repo, then commits + pushes so
# `flatpak update` picks it up. Runtime (org.kde.Platform) is NOT hosted here —
# clients pull it from Flathub's runtime remote, which nearly every desktop has.
#
# Requires: the release signing key in the local GPG keyring, and a checkout of
# the Pages repo (scottydev40.github.io) at $PAGES_DIR.
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

# The ed25519 release signing key ("Scotty Releases").
GPG_KEY="${SCOTTY_GPG_KEY:-FB5293E0EAEE66FB5235DE0278F60844B9C1E3F6}"
# Local checkout of the scottydev40.github.io Pages repo.
PAGES_DIR="${PAGES_DIR:-$HOME/Desktop/scotty-flatpak-pages}"
REPO_DIR="$PAGES_DIR/repo"

[ -d "$PAGES_DIR/.git" ] || { echo "PAGES_DIR ($PAGES_DIR) is not a git checkout" >&2; exit 1; }
mkdir -p "$REPO_DIR"
# OSTree enumerates refs/remotes and refs/mirrors on export; git does not track
# empty dirs, so a fresh Pages checkout (CI) lacks them and `flatpak
# build-export` dies with "opendir(refs/remotes): No such file or directory".
# Recreate them before building. (Local runs already have them, so this is a
# no-op there.)
mkdir -p "$REPO_DIR/refs/remotes" "$REPO_DIR/refs/mirrors"

echo ">> building + exporting signed flatpak into $REPO_DIR"
SCOTTY_FLATPAK_REPO="$REPO_DIR" SCOTTY_GPG_KEY="$GPG_KEY" "$HERE/build-local.sh"

echo ">> committing + pushing Pages repo"
git -C "$PAGES_DIR" add -A
if git -C "$PAGES_DIR" diff --cached --quiet; then
  echo ">> no repo changes to publish"
else
  git -C "$PAGES_DIR" commit -q -m "Publish Scotty $(date -u +%Y-%m-%dT%H:%MZ)"
  git -C "$PAGES_DIR" push
  echo ">> pushed — clients get it on the next 'flatpak update'"
fi
