# Compiler Biscuit

Biscuit est un fork de Feather 2.9.1. Les instructions détaillées d'origine sont dans
`HACKING.md`. Ce fichier résume ce qui a été testé.

## macOS (Apple Silicon, testé sur macOS 27.2)

Pré-requis : Xcode Command Line Tools et [Homebrew](https://brew.sh).

```bash
brew install qt libsodium libzip qrencode unbound cmake boost hidapi openssl expat \
             libunwind-headers protobuf pkgconfig zxing-cpp tor

git clone <url-du-depot> biscuit
cd biscuit
git submodule update --init --recursive

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSTACK_TRACE=OFF \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl);$(brew --prefix expat)"
cmake --build . -j "$(sysctl -n hw.ncpu)"

open bin/feather.app
```

Remarques :
- `zxing-cpp` est disponible dans Homebrew : pas besoin de le compiler (scanner QR actif).
- Les avertissements `ld: warning: building for macOS-26.5 ... built for newer version`
  sont sans conséquence pour un build de développement.
- Tor : Feather n'embarque pas Tor en build de développement. Pour tester l'option Tor,
  lancer `brew services start tor`, ou compiler avec `-DTOR_DIR=/chemin/vers/tor`.

## Linux

À documenter (voir `HACKING.md` en attendant).

## Windows

À documenter. Feather ne supporte pas le développement sous Windows, les binaires
Windows sont produits par compilation croisée (voir `contrib/guix`).
