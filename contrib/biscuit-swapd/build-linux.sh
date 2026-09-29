#!/usr/bin/env bash
# Builds biscuit-swapd for x86_64 Linux in an Ubuntu 22.04 container, the
# environment eigenwallet uses for its releases. The helper needs glibc 2.35
# (Ubuntu 22.04, Debian 12, Tails 6 and later); Monero's Boost does not build
# against older glibc here. Output: contrib/biscuit-swapd/bin/biscuit-swapd-x86_64-linux-gnu
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/external/biscuit-swapd"
IMAGE="docker.io/library/ubuntu@sha256:281c5745f657873d78e5531fc5ba8575f46ab7769b94550ac99543f122679986"   # ubuntu:22.04
RUST_VERSION="$(sed -n 's/^channel = "\(.*\)"/\1/p' "$SRC/rust-toolchain.toml")"
OUT="biscuit-swapd-x86_64-linux-gnu"

[ -f "$SRC/Cargo.toml" ] || { echo "external/biscuit-swapd is missing: git submodule update --init --recursive external/biscuit-swapd" >&2; exit 1; }
mkdir -p "$HERE/bin"

# Named volumes keep the Rust toolchain, the crates and the build between runs.
podman run --rm \
    -v "$ROOT":/repo:ro,z \
    -v "$HERE/bin":/out:z \
    -v biscuit-swapd-rustup:/root/.rustup \
    -v biscuit-swapd-cargo:/root/.cargo \
    -v biscuit-swapd-target:/build/target \
    -e RUST_VERSION="$RUST_VERSION" -e OUT="$OUT" \
    "$IMAGE" bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        # Rootless podman: tar cannot restore the archive owners.
        export TAR_OPTIONS=--no-same-owner
        apt-get -o Acquire::Retries=5 update -qq
        apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends build-essential cmake curl ca-certificates git \
            pkg-config autoconf automake libtool patch bison flex gperf python3 perl m4 file \
            xz-utils bzip2 >/dev/null
        export PATH="/root/.cargo/bin:$PATH"
        if ! command -v rustup >/dev/null; then
            curl --proto "=https" --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal --default-toolchain none
        fi
        rustup toolchain install --profile minimal "$RUST_VERSION"
        # Build from a fresh clone of the submodule at the commit Biscuit
        # pins: the repository is mounted read-only, and the monero-sys build
        # script patches Monero in place with git.
        git config --global --add safe.directory "*"
        git config --global protocol.file.allow always
        MODULES=/repo/.git/modules/external/biscuit-swapd/modules
        rm -rf /build/src
        git clone -q /repo/external/biscuit-swapd /build/src
        cd /build/src
        git checkout -q "$(git -C /repo/external/biscuit-swapd rev-parse HEAD)"
        for sm in monero-sys/monero monero-sys/monero-depends; do
            if [ -d "$MODULES/$sm" ]; then git config "submodule.$sm.url" "$MODULES/$sm"; fi
        done
        git submodule update -q --init --recursive
        ln -s /build/target target
        cargo +"$RUST_VERSION" build --release --locked -p biscuit-swapd
        strip -o "/out/$OUT" target/release/biscuit-swapd
        cd /out && sha256sum "$OUT"
    ' < /dev/null
