# Releases

Biscuit releases are built with [Guix](https://guix.gnu.org), so that anyone
can rebuild them from the published source and get the same files, byte for
byte. This file explains how to do that, then how we make a release.

## Reproducing a release

You need a Linux computer (x86_64) with Guix installed (see
[`contrib/guix/INSTALL.md`](contrib/guix/INSTALL.md)), about 50 GB of free
disk space, and time: the first build compiles the whole toolchain from source
and takes hours. Later builds reuse Guix's cache.

1. Get the source of the release, at its tag:

   ```bash
   git clone https://github.com/BiscuitWallet/Biscuit.git
   cd Biscuit
   git checkout <version>            # e.g. 1.0.12
   git submodule update --init --recursive
   ```

   The source archive on the download page (`biscuit-<version>.tar.gz`) is the
   same tree.

2. Put the atomic swap helper next to it. `biscuit-swapd` (Rust) is not built
   by Guix, and a release build stops unless each helper matches the SHA-256
   recorded in [`contrib/biscuit-swapd/SHA256SUMS`](contrib/biscuit-swapd/SHA256SUMS).
   Either rebuild it with
   [`contrib/biscuit-swapd/build.sh`](contrib/biscuit-swapd/build.sh), or take
   it from the release's own files, and place it in `contrib/biscuit-swapd/bin/`
   under the name listed in `SHA256SUMS`.

3. Build:

   ```bash
   HOSTS="x86_64-linux-gnu x86_64-w64-mingw32 x86_64-w64-mingw32.installer x86_64-apple-darwin arm64-apple-darwin" \
       ./contrib/guix/guix-build
   ```

   Any subset of these hosts works. The files land in
   `guix/guix-build-<version>/output/`. The macOS SDK is downloaded and checked
   by the build itself.

4. Compare: download `hashes-<version>-plain.txt` and the release key from the
   download page, check the signature (`gpg --verify`), then compare the
   SHA-256 of your files with the list.

Two things differ, on purpose:

- **macOS:** Apple Silicon only runs signed code, so after the Guix build we
  sign `Biscuit.app` with an ad-hoc signature (no Apple account, no identity)
  and zip it again ([`contrib/release/macos-adhoc-sign.sh`](contrib/release/macos-adhoc-sign.sh)).
  The published macOS zips are therefore not the Guix output byte for byte;
  the Windows and Linux files are.
- **biscuit-swapd**, as above: built outside Guix, pinned by its checksum.

The Linux packages (`.deb`, `.rpm`, Arch and `.tar.gz`) are made from the
Guix binary with [`contrib/packaging/build-packages.sh`](contrib/packaging/build-packages.sh)
(nfpm in a container, every file dated to the release commit).

If you reproduce a release, or can't, please tell us: open an issue or write
to us from https://biscuitwallet.com/contact/.

## Making a release

Releases come out every two weeks, unless a security fix can't wait.

1. **Before:** update `src/assets/restore_heights_monero_{mainnet,stagenet}.txt`
   and the node lists when needed, and check the dependencies in
   `contrib/depends` for security fixes. Monero changes come from upstream
   Feather and Monero.
2. **The helper:** if `external/biscuit-swapd` changed, rebuild it for every
   platform (`contrib/biscuit-swapd/build.sh linux|windows|macos`) and commit
   the new `contrib/biscuit-swapd/SHA256SUMS`.
3. **Version:** one commit, `Biscuit <version>`, that only changes the version
   in `CMakeLists.txt`.
4. **Tag:** an annotated tag on that commit (`git tag -a <version>`). A tag is
   never moved: a fix found after tagging makes a new version.
5. **Push** the branch and the tag.
6. **Build** with Guix from a clean checkout of the tag (see above), then the
   Linux packages.
7. **Sign the macOS builds** on a Mac: `contrib/release/macos-adhoc-sign.sh <output>`.
8. **Publish** from the Mac that holds the release key:
   `contrib/release/publish.sh <version> <output>`. It uploads the files, the
   source archive and the release key, writes the SHA-256 list of every file
   and clearsigns it, and updates `updates.json` last, once everything it points
   to is online: that's when Biscuit offers the update.
9. **Site:** the announcement in the Journal, the download page (version,
   file names, sizes) and anything the release changes in the docs.

The release key is `D714 332E 6101 C1DA AC60  E644 202F 1FA3 98DE CEFA`
("Biscuit Wallet releases"); the app only installs updates signed with it.
