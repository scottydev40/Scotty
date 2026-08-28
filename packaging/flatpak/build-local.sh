#!/usr/bin/env bash
# Reproduce the Scotty Flatpak locally (source-built, offline-in-sandbox — the
# same shape that goes to Flathub). Generates the two large local sources (a
# clean repo snapshot + the bazel `vendor` dir) and fetches the pinned bazel
# binary, then runs flatpak-builder. Fetching the vendor dir needs network once;
# the flatpak-builder *build* itself runs with no network (proving Flathub-legal).
#
# For a Flathub submission these three local sources become url+sha256 sources
# (repo tag tarball, a released vendor tarball, the upstream bazel release).
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REPO="$(git -C "$HERE" rev-parse --show-toplevel)"
OUT="${1:-$HERE/_build}"
BZVER="$(cat "$REPO/.bazelversion")"

BAZEL_FLAGS=(
  --check_visibility=false --spawn_strategy=standalone
  --cxxopt=-std=c++20 --host_cxxopt=-std=c++20
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true
  --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden
)
ENGINE_TARGET=//sharing/linux:nearby_sharing_api_shared

mkdir -p "$OUT"

echo ">> 1/4  clean repo snapshot"
git -C "$REPO" archive --format=tar --prefix=scotty/ HEAD -o "$OUT/scotty-src.tar"

echo ">> 2/4  bazel vendor dir (network used here, once)"
( cd "$REPO" && bazel vendor --vendor_dir="$OUT/vendor" "${BAZEL_FLAGS[@]}" "$ENGINE_TARGET" )
tar -C "$OUT" -cf "$OUT/vendor.tar" vendor

echo ">> 3/4  pinned bazel $BZVER"
if [ ! -x "$OUT/bazel-$BZVER" ]; then
  curl -fL -o "$OUT/bazel-$BZVER" \
    "https://github.com/bazelbuild/bazel/releases/download/$BZVER/bazel-$BZVER-linux-x86_64"
  chmod +x "$OUT/bazel-$BZVER"
fi

echo ">> 4/4  flatpak-builder (build runs offline)"
cp "$HERE/dev.scotty.Scotty.yaml" "$OUT/"
# The two big sources are local unchecksummed archives (a fresh `git archive`
# each run). flatpak-builder does not re-hash those, so it will "Cache hit,
# skipping build" and ship a STALE binary even after the code changed. `--force-clean`
# only wipes the output dir, not the `.flatpak-builder` cache — so drop the cache
# to guarantee the rebuild picks up new commits. (The Flathub manifest uses
# url+sha256 sources, which are content-addressed, so this trap is local-only.)
rm -rf "$OUT/.flatpak-builder"
( cd "$OUT" && flatpak-builder --force-clean --disable-rofiles-fuse --user --install \
    build-dir dev.scotty.Scotty.yaml )

echo ">> done — run:  flatpak run dev.scotty.Scotty"
