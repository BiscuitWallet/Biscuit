#!/usr/bin/env bash
# Ad-hoc signs the macOS builds of a release, on a Mac, before publish.sh.
#
#   contrib/release/macos-adhoc-sign.sh <guix output dir>
#
# Apple Silicon runs no unsigned code, and the Guix build strips the binaries
# after the linker signed them. An ad-hoc signature needs no Apple account
# and carries no identity: macOS still asks the user to allow Biscuit once
# (see the download page). Replaces biscuit-<version>-mac.zip and
# biscuit-<version>-mac-arm64.zip in place.
set -euo pipefail

OUTPUT="$(cd "${1:?usage: $0 <guix output dir>}" && pwd)"
[ "$(uname)" = Darwin ] || { echo "run this on a Mac (codesign)" >&2; exit 1; }

shopt -s nullglob
zips=("$OUTPUT"/*-apple-darwin/biscuit-*-mac*.zip)
[ "${#zips[@]}" -gt 0 ] || { echo "no macOS zip in $OUTPUT" >&2; exit 1; }

for zip in "${zips[@]}"; do
    work="$(mktemp -d)"
    ditto -x -k "$zip" "$work"
    app="$work/Biscuit.app"
    [ -d "$app" ] || { echo "$zip: no Biscuit.app inside" >&2; exit 1; }

    # Nested code first (Tor, libevent, the swap helper), then the bundle.
    while IFS= read -r -d '' file; do
        if file -b "$file" | grep -q '^Mach-O'; then
            codesign --force --sign - --timestamp=none "$file"
        fi
    done < <(find "$app/Contents" -type f -print0)
    codesign --force --sign - --timestamp=none "$app"
    codesign --verify --deep --strict "$app"

    (cd "$work" && ditto -c -k --keepParent Biscuit.app "$zip.new")
    mv "$zip.new" "$zip"
    rm -rf "$work"
    echo "signed (ad hoc): $(basename "$zip")"
done
