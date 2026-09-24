#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: The Biscuit developers
#
# Copies a Tor binary and the non-system libraries it needs into
# <app>/Contents/bin, rewrites the library paths so the bundle is
# self-contained, then signs the copies ad hoc (required on Apple Silicon
# after install_name_tool).
#
# Usage: bundle-tor.sh <path/to/tor> <path/to/Biscuit.app>

set -euo pipefail

TOR_SRC="$1"
APP="$2"
DEST="$APP/Contents/bin"

mkdir -p "$DEST"
cp -f "$TOR_SRC" "$DEST/tor"
chmod 755 "$DEST/tor"

# Non-system dependencies (Homebrew, etc.) of a Mach-O file.
deps() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/|@)' || true
}

# Copy dependencies recursively.
queue=("$DEST/tor")
while ((${#queue[@]})); do
    file="${queue[0]}"
    queue=("${queue[@]:1}")
    for dep in $(deps "$file"); do
        name="$(basename "$dep")"
        if [[ ! -f "$DEST/$name" ]]; then
            cp -fL "$dep" "$DEST/$name"
            chmod 644 "$DEST/$name"
            install_name_tool -id "@loader_path/$name" "$DEST/$name"
            queue+=("$DEST/$name")
        fi
        install_name_tool -change "$dep" "@loader_path/$name" "$file"
    done
done

for file in "$DEST"/*; do
    codesign --force --sign - "$file" >/dev/null 2>&1
done

echo "Bundled Tor into $DEST: $(ls "$DEST" | tr '\n' ' ')"
