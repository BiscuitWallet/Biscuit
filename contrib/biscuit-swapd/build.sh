#!/usr/bin/env bash
# Builds biscuit-swapd in an Ubuntu 22.04 container, the environment
# eigenwallet uses for its releases:
#
#   contrib/biscuit-swapd/build.sh linux     -> bin/biscuit-swapd-x86_64-linux-gnu
#   contrib/biscuit-swapd/build.sh windows   -> bin/biscuit-swapd-x86_64-w64-mingw32.exe
#   contrib/biscuit-swapd/build.sh macos     -> bin/biscuit-swapd-arm64-apple-darwin
#                                               bin/biscuit-swapd-x86_64-apple-darwin
#
# Linux: needs glibc 2.34 at run time (Ubuntu 22.04, Debian 12, Tails 6 and
# later); Monero's Boost does not build against older glibc here.
# Windows: cross-compiled with the MinGW-w64 GCC that eigenwallet's script
# builds from source (signatures checked), as for eigenwallet's releases.
# macOS: built natively on a Mac (Apple Silicon), for both processors, to run
# on macOS 14 and later. Needs the external/biscuit-swapd submodule with its
# own submodules (Monero), or SWAPD_SRC=<checkout at the same commit>, and
# rustup with both apple-darwin targets.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/external/biscuit-swapd"
# macos: another checkout of biscuit-swapd may be used (SWAPD_SRC), at the
# commit this repository pins.
if [ "${1:-}" = macos ] && [ -n "${SWAPD_SRC:-}" ]; then
    PIN="$(git -C "$ROOT" ls-tree HEAD external/biscuit-swapd | awk '{print $3}')"
    [ "$(git -C "$SWAPD_SRC" rev-parse HEAD)" = "$PIN" ] \
        || { echo "SWAPD_SRC is not at the pinned commit $PIN" >&2; exit 1; }
    [ -z "$(git -C "$SWAPD_SRC" status --porcelain --untracked-files=no)" ] \
        || { echo "SWAPD_SRC has local changes" >&2; exit 1; }
    SRC="$(cd "$SWAPD_SRC" && pwd)"
fi
IMAGE="docker.io/library/ubuntu@sha256:281c5745f657873d78e5531fc5ba8575f46ab7769b94550ac99543f122679986"   # ubuntu:22.04
RUST_VERSION="$(sed -n 's/^channel = "\(.*\)"/\1/p' "$SRC/rust-toolchain.toml")"
case "${1:-}" in
    linux)   TARGET="";                       OUT="biscuit-swapd-x86_64-linux-gnu" ;;
    windows) TARGET="x86_64-pc-windows-gnu";  OUT="biscuit-swapd-x86_64-w64-mingw32.exe" ;;
    macos)   TARGET="";                       OUT="" ;;
    *) echo "usage: $0 linux|windows|macos" >&2; exit 1 ;;
esac

if [ "$1" = macos ]; then
    [ "$(uname)" = Darwin ] || { echo "macos: run this on a Mac" >&2; exit 1; }
    mkdir -p "$HERE/bin"
    # Monero's depends target macOS 10.8, which current libc++ refuses, and
    # monero-sys runs make with an empty environment but PATH: a make first
    # in PATH sets the minimum version instead.
    WRAP="$(mktemp -d)"
    trap 'rm -rf "$WRAP"' EXIT
    printf '#!/bin/sh\nexec /usr/bin/make OSX_MIN_VERSION=14.0 "$@"\n' > "$WRAP/make"
    chmod +x "$WRAP/make"
    for arch in aarch64 x86_64; do
        # Cargo's default strip breaks the proc-macro libraries on recent macOS.
        (cd "$SRC" && PATH="$WRAP:$HOME/.cargo/bin:$PATH" MACOSX_DEPLOYMENT_TARGET=14.0 \
            CARGO_PROFILE_RELEASE_STRIP=false CARGO_PROFILE_RELEASE_BUILD_OVERRIDE_STRIP=false \
            rustup run "$RUST_VERSION" cargo build --release -p biscuit-swapd --target "$arch-apple-darwin")
        out="$HERE/bin/biscuit-swapd-${arch/aarch64/arm64}-apple-darwin"
        cp "$SRC/target/$arch-apple-darwin/release/biscuit-swapd" "$out"
        strip "$out"
        codesign --force --sign - "$out"
        echo "$(cd "$HERE" && shasum -a 256 "bin/$(basename "$out")")"
    done
    exit 0
fi

[ -f "$SRC/Cargo.toml" ] || { echo "external/biscuit-swapd is missing: git submodule update --init --recursive external/biscuit-swapd" >&2; exit 1; }
mkdir -p "$HERE/bin"

# Named volumes keep the Rust toolchain, the crates and the build between runs.
podman run --rm \
    -v "$ROOT":/repo:ro,z \
    -v "$HERE/bin":/out:z \
    -v biscuit-swapd-rustup:/root/.rustup \
    -v biscuit-swapd-cargo:/root/.cargo \
    -v "biscuit-swapd-target-${1}":/build/target \
    -v biscuit-swapd-mingw:/root/opt \
    -e RUST_VERSION="$RUST_VERSION" -e OUT="$OUT" -e TARGET="$TARGET" \
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
        if [ -z "$TARGET" ]; then
            cargo +"$RUST_VERSION" build --release --locked -p biscuit-swapd
            strip -o "/out/$OUT" target/release/biscuit-swapd
        else
            # MinGW-w64 GCC with POSIX threads, built once and kept in the
            # biscuit-swapd-mingw volume. The script calls sudo apt.
            printf "#!/bin/sh\nexec \"\$@\"\n" > /usr/local/bin/sudo && chmod +x /usr/local/bin/sudo
            apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends wget texinfo \
                libgmp-dev libmpfr-dev libmpc-dev libisl-dev zlib1g-dev libbz2-dev libffi-dev \
                gnupg dirmngr jq >/dev/null
            MINGW=/root/opt/gcc-mingw-14.3/bin
            if [ ! -x "$MINGW/x86_64-w64-mingw32-g++" ]; then
                (cd dev-scripts && ./ubuntu_build_x86_86-w64-mingw32-gcc.sh)
            fi
            export PATH="$MINGW:$PATH"
            export CC_x86_64_pc_windows_gnu="$MINGW/x86_64-w64-mingw32-gcc"
            export CXX_x86_64_pc_windows_gnu="$MINGW/x86_64-w64-mingw32-g++"
            export AR_x86_64_pc_windows_gnu="$MINGW/x86_64-w64-mingw32-ar"
            # Link the MinGW runtimes (libstdc++, winpthread) statically, so
            # the helper needs no DLL beyond those of Windows. rustc puts
            # -Bdynamic before the -lstdc++ that the cc crate asks for: drop
            # that one (empty CXXSTDLIB) and add -lstdc++ at the end of the
            # link, static. Joined with the rustflags of the workspace config.
            export CXXSTDLIB_x86_64_pc_windows_gnu=""
            export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_RUSTFLAGS="-C link-arg=-static -C link-arg=-Wl,-Bstatic -C link-arg=-lstdc++ -C link-arg=-lpthread -C link-arg=-lmingwex -C link-arg=-Wl,-Bdynamic -C link-arg=-lucrt -C link-arg=-lkernel32"
            rustup target add --toolchain "$RUST_VERSION" "$TARGET"
            cargo +"$RUST_VERSION" build --release --locked -p biscuit-swapd --target "$TARGET"
            x86_64-w64-mingw32-strip -o "/out/$OUT" "target/$TARGET/release/biscuit-swapd.exe"
            # Every DLL it needs must ship with Windows.
            x86_64-w64-mingw32-objdump -p "/out/$OUT" | sed -n "s/.*DLL Name: //p" | sort -u
        fi
        cd /out && sha256sum "$OUT"
    ' < /dev/null
