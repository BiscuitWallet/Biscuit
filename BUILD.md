# Compiler Biscuit

Biscuit est un fork de Feather 2.9.1. Les services en ligne de Feather sont coupés
par défaut (option CMake `WITH_FEATHER_SERVICES=OFF`). Les instructions détaillées d'origine sont dans
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

open bin/biscuit.app
```

Remarques :
- `zxing-cpp` est disponible dans Homebrew : pas besoin de le compiler (scanner QR actif).
- Les avertissements `ld: warning: building for macOS-26.5 ... built for newer version`
  sont sans conséquence pour un build de développement.
- Tor : sur macOS, le Tor de Homebrew (`brew install tor`) est copié automatiquement dans
  `Biscuit.app/Contents/bin` avec ses bibliothèques (script `contrib/macdeploy/bundle-tor.sh`).
  Dans l'app, « Tor » fonctionne alors en un clic, sans rien configurer. Pour une version
  publique, utiliser un Tor compilé en statique (`-DBISCUIT_TOR_BIN=/chemin/vers/tor`).

## Tests unitaires

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build . -j "$(sysctl -n hw.ncpu)"
ctest --test-dir tests --output-on-failure
```

## Trocador : relais (builds officiels) ou clé (développement)

Les builds officiels passent par le relais Biscuit, qui garde la clé Trocador côté
serveur (dépôt privé `BiscuitWallet/biscuit-relay`). La clé n'est alors jamais compilée dans l'app.
Créer `secrets.cmake` à la racine (ignoré par git) :

```cmake
set(BISCUIT_TROCADOR_RELAY_URL "https://relay.example.org/api/")
```

Pour le développement seulement, l'app peut appeler Trocador directement avec la clé :

```cmake
set(BISCUIT_TROCADOR_API_KEY "votre-cle")
```

Pour tester avec un relais local : `-DBISCUIT_TROCADOR_RELAY_URL=http://127.0.0.1:8080/api/`.

Sans relais ni clé, l'onglet Swap fonctionne en **mode démo** : offres simulées, aucun
échange réel, aucun envoi possible. CMake affiche le mode utilisé à la configuration.

## Swaps atomiques : le programme biscuit-swapd

Les swaps atomiques (BTC → XMR) passent par `biscuit-swapd`, compilé depuis le
fork d'eigenwallet (`~/Developer/_ref/eigenwallet`, branche `biscuit`). Rust est
installé par le `rustup` de Homebrew, dont `cargo` n'est pas dans le PATH :

```sh
export PATH="/opt/homebrew/opt/rustup/bin:$PATH"
cd ~/Developer/_ref/eigenwallet
cargo build --release -p biscuit-swapd      # 10 à 20 min (LTO complète)
strip target/release/biscuit-swapd          # environ 15 Mo
```

Ne pas rediriger la sortie de la compilation (`| tail`…) : une étape peut poser
une question et rester bloquée sans rien afficher.

Puis l'inclure dans Biscuit (copié à côté de l'exécutable à chaque build) :

```sh
cmake .. -DBISCUIT_SWAPD_BINARY=$HOME/Developer/_ref/eigenwallet/target/release/biscuit-swapd
```

Sans cette option, l'onglet du swap atomique indique que le programme manque.

## Linux

À documenter (voir `HACKING.md` en attendant).

## Windows

À documenter. Feather ne supporte pas le développement sous Windows, les binaires
Windows sont produits par compilation croisée (voir `contrib/guix`).
