#!/usr/bin/env bash
# Builds biscuit-swapd for x86_64 Linux in a Debian 11 container (glibc 2.31,
# the same as the Guix builds), so the helper runs on the same systems as the
# AppImage. Output: contrib/biscuit-swapd/bin/biscuit-swapd-x86_64-linux-gnu
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/external/biscuit-swapd"
IMAGE="docker.io/library/debian@sha256:6f519a81440354a85eb592c5f32109ab80605f6b892455983a6f618bf87fabe9"   # debian:bullseye
RUST_VERSION="$(sed -n 's/^channel = "\(.*\)"/\1/p' "$SRC/rust-toolchain.toml")"
OUT="biscuit-swapd-x86_64-linux-gnu"

[ -f "$SRC/Cargo.toml" ] || { echo "external/biscuit-swapd is missing: git submodule update --init external/biscuit-swapd" >&2; exit 1; }
mkdir -p "$HERE/bin"

# Named volumes keep the Rust toolchain, the crates and the build between runs.
podman run --rm \
    -v "$SRC":/src:ro,Z \
    -v "$HERE/bin":/out:Z \
    -v biscuit-swapd-rustup:/root/.rustup \
    -v biscuit-swapd-cargo:/root/.cargo \
    -v biscuit-swapd-target:/build/target \
    -e RUST_VERSION="$RUST_VERSION" -e OUT="$OUT" \
    "$IMAGE" bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends build-essential cmake curl ca-certificates git \
            pkg-config autoconf automake libtool patch bison flex gperf python3 perl m4 file \
            xz-utils bzip2 >/dev/null
        export PATH="/root/.cargo/bin:$PATH"
        if ! command -v rustup >/dev/null; then
            curl --proto "=https" --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal --default-toolchain none
        fi
        rustup toolchain install -q --profile minimal "$RUST_VERSION"
        # The source is mounted read-only; build from a copy (the monero-sys
        # build script patches files in place).
        rm -rf /build/src && mkdir /build/src
        (cd /src && tar cf - --exclude=./target .) | tar xf - -C /build/src
        cd /build/src && ln -s /build/target target
        cargo +"$RUST_VERSION" build --release --locked -p biscuit-swapd
        strip -o "/out/$OUT" target/release/biscuit-swapd
        cd /out && sha256sum "$OUT"
    ' < /dev/null
