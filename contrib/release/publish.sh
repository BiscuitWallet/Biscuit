#!/usr/bin/env bash
# Publishes a Biscuit release on biscuitwallet.com, where the in-app updater
# finds it:
#
#   files/releases/<platform>/biscuit-<version>-<platform>.zip   (Guix builds)
#   files/releases/packages/<version>/…                          (.deb, .rpm, Arch, .tar.gz)
#   files/releases/source/biscuit-<version>.tar.gz               (source archive)
#   files/releases/hashes-<version>-plain.txt                    (SHA-256 list, clearsigned)
#   files/releases/biscuit-release-key.asc                       (public release key)
#   updates.json                                                  (latest version per platform)
#
#   contrib/release/publish.sh <version> <guix output dir> [--dry-run]
#
# Run on the Mac that holds the release signing key ("Biscuit Wallet
# releases"): gpg asks for its passphrase. The Guix output dir is
# guix/guix-build-<version>/output, copied from the build machine; the Linux
# packages are taken from its x86_64-linux-gnu/packages directory.
# updates.json is written last, once every file it points to is online.
set -euo pipefail

VERSION="${1:?usage: $0 <version> <guix output dir> [--dry-run]}"
OUTPUT="$(cd "${2:?usage: $0 <version> <guix output dir> [--dry-run]}" && pwd)"
DRY_RUN="${3:-}"
KEY="Biscuit Wallet releases"
HOST="biscuit"
SITE="/srv/biscuit-site"

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must be x.y.z" >&2; exit 1; }
gpg --list-secret-keys "$KEY" >/dev/null 2>&1 || { echo "release signing key \"$KEY\" not found" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/files/releases/packages/$VERSION"

# Update archives: biscuit-<version>-<platform>.zip, one directory per platform.
PLATFORMS=()
while IFS= read -r zip; do
    name="$(basename "$zip")"
    platform="${name#biscuit-"$VERSION"-}"
    platform="${platform%.zip}"
    mkdir -p "$STAGE/files/releases/$platform"
    cp "$zip" "$STAGE/files/releases/$platform/"
    PLATFORMS+=("$platform")
done < <(find "$OUTPUT" -maxdepth 2 -name "biscuit-$VERSION-*.zip" | sort)
[ "${#PLATFORMS[@]}" -gt 0 ] || { echo "no biscuit-$VERSION-*.zip in $OUTPUT" >&2; exit 1; }

# Linux packages, if built (contrib/packaging/build-packages.sh).
if [ -d "$OUTPUT/x86_64-linux-gnu/packages" ]; then
    find "$OUTPUT/x86_64-linux-gnu/packages" -maxdepth 1 -type f \
        \( -name "*.deb" -o -name "*.rpm" -o -name "*.pkg.tar.zst" -o -name "*.tar.gz" \) \
        -exec cp {} "$STAGE/files/releases/packages/$VERSION/" \;
fi

# The source archive of the release (git ls-files, submodules included).
if [ -f "$OUTPUT/dist-archive/biscuit-$VERSION.tar.gz" ]; then
    mkdir -p "$STAGE/files/releases/source"
    cp "$OUTPUT/dist-archive/biscuit-$VERSION.tar.gz" "$STAGE/files/releases/source/"
else
    echo "source archive dist-archive/biscuit-$VERSION.tar.gz missing" >&2
    exit 1
fi

# The release key, next to the files it signs.
gpg --export --armor "$KEY" > "$STAGE/files/releases/biscuit-release-key.asc"

# SHA-256 of everything, by file name, signed (the updater checks the
# signature against the key built into the app, then the archive's hash).
(
    cd "$STAGE/files/releases"
    find . -type f ! -name "hashes-*" ! -name "*.asc" -print0 | sort -z | xargs -0 shasum -a 256 \
        | sed -E 's#  \./([^/]+/)*#  #' > "hashes-$VERSION.txt"
)
gpg --local-user "$KEY" --digest-algo SHA256 --clearsign \
    --output "$STAGE/files/releases/hashes-$VERSION-plain.txt" "$STAGE/files/releases/hashes-$VERSION.txt"
rm "$STAGE/files/releases/hashes-$VERSION.txt"
gpg --status-fd 1 --verify "$STAGE/files/releases/hashes-$VERSION-plain.txt" 2>/dev/null | grep -q "^\[GNUPG:\] GOODSIG" \
    || { echo "signature check failed" >&2; exit 1; }

# updates.json: the platforms of this release now point to it.
python3 - "$STAGE/updates.json" "$VERSION" "${PLATFORMS[@]}" <<'EOF'
import json, sys
path, version, platforms = sys.argv[1], sys.argv[2], sys.argv[3:]
json.dump({"platform": {p: {"version": version} for p in platforms}}, open(path, "w"), indent=1)
EOF

echo "Release $VERSION: ${PLATFORMS[*]}"
find "$STAGE" -type f | sed "s#$STAGE/##" | sort
cat "$STAGE/files/releases/hashes-$VERSION-plain.txt"

if [ "$DRY_RUN" = "--dry-run" ]; then
    echo "Dry run: nothing uploaded."
    exit 0
fi
# Files first, updates.json last: the app never sees a version whose files
# are not online yet. Existing releases are kept.
# (macOS ships openrsync, which has no --chmod: permissions are set after.)
rsync -rlt "$STAGE/files/" "$HOST:$SITE/files/"
ssh "$HOST" "chmod -R u=rwX,go=rX '$SITE/files'"
rsync -t "$STAGE/updates.json" "$HOST:$SITE/updates.json"
ssh "$HOST" "chmod 644 '$SITE/updates.json'"
echo "Published: https://biscuitwallet.com/updates.json"
