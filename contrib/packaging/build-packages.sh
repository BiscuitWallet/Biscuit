#!/usr/bin/env bash
# Linux packages from a Guix build: .deb, .rpm, Arch (.pkg.tar.zst) and a
# plain .tar.gz, all holding the same biscuit and biscuit-swapd binaries as
# the AppImage.
#
#   contrib/packaging/build-packages.sh guix/guix-build-<version>/output/x86_64-linux-gnu [VERSION]
#
# VERSION defaults to 0.0.0 with a git<commit> pre-release suffix. Output
# goes to <guix output dir>/packages, with a SHA256SUMS file. Needs podman.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NFPM_IMAGE="docker.io/goreleaser/nfpm@sha256:74f890d72b1198cab1535b1d73edac3edb91fd6a63c63014d0dac5c766ab6211"   # nfpm 2.47.0

OUTDIR="$(cd "${1:?usage: $0 <guix output dir for x86_64-linux-gnu> [VERSION]}" && pwd)"
ZIP="$(ls "$OUTDIR"/biscuit-*-linux.zip | head -1)"
DISTNAME="$(basename "$ZIP" -linux.zip)"          # biscuit-<version or commit>
BUILD_ID="${DISTNAME#biscuit-}"

VERSION="${2:-0.0.0}"
PRERELEASE=""
if [ -z "${2:-}" ]; then
    PRERELEASE="git${BUILD_ID:0:12}"
fi

# Same timestamps as the Guix build: the commit time.
if COMMIT_TIME="$(git -C "$ROOT" log -1 --format=%ct "$BUILD_ID" 2>/dev/null)"; then
    export SOURCE_DATE_EPOCH="$COMMIT_TIME"
else
    export SOURCE_DATE_EPOCH="$(git -C "$ROOT" log -1 --format=%ct)"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"
PKGDIR="$OUTDIR/packages"
mkdir -p "$STAGE/icons" "$PKGDIR"

unzip -q "$ZIP" -d "$WORK/zip"
install -m 0755 "$WORK/zip/$DISTNAME" "$STAGE/biscuit"
install -m 0755 "$WORK/zip/biscuit-swapd" "$STAGE/biscuit-swapd"
install -m 0644 "$ROOT/src/assets/biscuit.desktop" "$STAGE/biscuit.desktop"
for size in 32 48 64 96 128 256 512; do
    install -m 0644 "$ROOT/src/assets/images/appicons/${size}x${size}.png" "$STAGE/icons/"
done
install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
install -m 0644 "$ROOT/external/biscuit-swapd/LICENSE" "$STAGE/LICENSE.biscuit-swapd"
{
    echo "Biscuit: BSD-3-Clause, see /usr/share/licenses/biscuit/LICENSE"
    echo "biscuit-swapd: GPL-3.0, see /usr/share/licenses/biscuit/LICENSE.biscuit-swapd"
    echo "Source code: https://biscuitwallet.com"
} > "$STAGE/copyright"
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

# nfpm does not expand variables in file paths: fill in the configuration.
sed -e "s|\${STAGE}|/stage|g" -e "s|\${VERSION}|$VERSION|g" -e "s|\${PRERELEASE}|$PRERELEASE|g" \
    "$HERE/nfpm.yaml" > "$WORK/nfpm.yaml"
for packager in deb rpm archlinux; do
    podman run --rm \
        -v "$WORK/nfpm.yaml":/cfg/nfpm.yaml:ro,z -v "$STAGE":/stage:ro,z -v "$PKGDIR":/out:z \
        -e SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
        "$NFPM_IMAGE" package --config /cfg/nfpm.yaml --packager "$packager" --target /out/ < /dev/null
done

# Plain archive: the two binaries, the desktop entry, an icon and the licenses.
TARNAME="biscuit-${VERSION}${PRERELEASE:+~$PRERELEASE}-linux-x86_64"
mkdir -p "$WORK/$TARNAME"
cp -p "$STAGE/biscuit" "$STAGE/biscuit-swapd" "$STAGE/biscuit.desktop" "$STAGE/LICENSE" "$STAGE/LICENSE.biscuit-swapd" "$WORK/$TARNAME/"
cp -p "$STAGE/icons/256x256.png" "$WORK/$TARNAME/biscuit.png"
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
    -C "$WORK" -cf - "$TARNAME" | gzip -9n > "$PKGDIR/$TARNAME.tar.gz"

cd "$PKGDIR"
rm -f SHA256SUMS
sha256sum -- *.deb *.rpm *.pkg.tar.zst *.tar.gz > SHA256SUMS
cat SHA256SUMS
