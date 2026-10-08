# Building Biscuit

Biscuit is a fork of Feather 2.9.1. Feather's online services are off (CMake option
`WITH_FEATHER_SERVICES=OFF`). The general developer instructions are in
[HACKING.md](HACKING.md); this file sums up what has been tested.

## macOS (Apple Silicon, tested on macOS 27.2)

Requirements: the Xcode Command Line Tools and [Homebrew](https://brew.sh).

```bash
brew install qt libsodium libzip qrencode unbound cmake boost hidapi openssl expat \
             libunwind-headers protobuf pkgconfig zxing-cpp tor

git clone https://github.com/BiscuitWallet/Biscuit.git biscuit
cd biscuit
git submodule update --init --recursive

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSTACK_TRACE=OFF \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl);$(brew --prefix expat)"
cmake --build . -j "$(sysctl -n hw.ncpu)"

open bin/biscuit.app
```

Notes:
- `zxing-cpp` comes from Homebrew: no need to build it (the QR scanner is on).
- Warnings like `ld: warning: building for macOS-26.5 ... built for newer version`
  don't matter for a development build.
- Tor: on macOS, Homebrew's Tor (`brew install tor`) is copied into
  `Biscuit.app/Contents/bin` with its libraries (`contrib/macdeploy/bundle-tor.sh`),
  so Tor mode works in one click. Another Tor binary can be given with
  `-DBISCUIT_TOR_BIN=/path/to/tor`.

## Unit tests

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build . -j "$(sysctl -n hw.ncpu)"
ctest --test-dir tests --output-on-failure
```

## Trocador: the relay (official builds) or a key (development)

Official builds go through Biscuit's relay, which keeps the Trocador partner key on the
server ([BiscuitWallet/biscuit-relay](https://github.com/BiscuitWallet/biscuit-relay)):
the key is never compiled into the app. Create `secrets.cmake` at the root (ignored by git):

```cmake
set(BISCUIT_TROCADOR_RELAY_URL "https://relay.example.org/api/")
```

For development only, the app can call Trocador directly with a key:

```cmake
set(BISCUIT_TROCADOR_API_KEY "your-key")
```

To test with a local relay: `-DBISCUIT_TROCADOR_RELAY_URL=http://127.0.0.1:8080/api/`.

With neither a relay nor a key, the Swap tab runs in **demo mode**: simulated offers,
no real exchange, nothing can be sent. CMake says which mode is used when it configures.

## Atomic swaps: the biscuit-swapd helper

Atomic swaps (BTC → XMR) go through `biscuit-swapd`, in the `external/biscuit-swapd`
submodule ([BiscuitWallet/biscuit-swapd](https://github.com/BiscuitWallet/biscuit-swapd),
a fork of eigenwallet, GPL-3.0). Rust comes from Homebrew's `rustup`, whose `cargo` is not
in the PATH:

```sh
export PATH="/opt/homebrew/opt/rustup/bin:$PATH"
git submodule update --init external/biscuit-swapd
cd external/biscuit-swapd
cargo build --release -p biscuit-swapd      # 10 to 20 min (full LTO)
strip target/release/biscuit-swapd          # about 15 MB
```

Don't pipe the build's output (`| tail`…): a step may ask a question and wait without
showing anything.

CMake then finds `external/biscuit-swapd/target/release/biscuit-swapd` by itself and copies
it next to the executable at every build (run `cmake ..` again after the first build).
Another binary can be given explicitly:

```sh
cmake .. -DBISCUIT_SWAPD_BINARY=/path/to/biscuit-swapd
```

To change biscuit-swapd: work in `external/biscuit-swapd` (branch `main`), push, then commit
the new submodule commit in Biscuit.

Without the helper, the atomic swap tab says it is missing.

## Linux

### Release builds (Guix)

On a Linux x86_64 machine with Guix installed (see [RELEASE.md](RELEASE.md) for the whole release):

```sh
# 1. biscuit-swapd, outside Guix (Ubuntu 22.04 container, podman)
contrib/biscuit-swapd/build.sh linux      # or: build.sh windows
# check / update contrib/biscuit-swapd/SHA256SUMS, commit

# 2. AppImage + binary + source archive, reproducible
HOSTS="x86_64-linux-gnu" ./contrib/guix/guix-build

# 3. .deb, .rpm, Arch and .tar.gz packages from the Guix binary (podman, nfpm)
contrib/packaging/build-packages.sh guix/guix-build-<version>/output/x86_64-linux-gnu [VERSION]
```

The first Guix build takes hours (toolchain, Qt…); later ones reuse the cache. The git tree
must be clean (submodules included). A tagged release refuses to build without biscuit-swapd.
Without VERSION, the packages are named `0.0.0~git<commit>`.

The packages were tested in Debian 12, Ubuntu 22.04, Fedora and Arch containers.
biscuit-swapd needs glibc 2.34 or later.

### Development build

See [HACKING.md](HACKING.md). On Fedora 44 (GCC 16), building biscuit-swapd needs
`x86_64-linux-gnu-*` aliases to the system tools (gcc with `-std=gnu17`).

## Windows

Development on Windows isn't supported: Windows builds are cross-compiled with Guix
(see [RELEASE.md](RELEASE.md)).
