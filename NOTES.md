# Notes d'architecture (base Feather 2.9.1)

Résumé de l'étude du code de Feather, pour savoir où brancher les modules Biscuit.

## Vue d'ensemble

| Dossier | Rôle |
|---|---|
| `monero/` | Sous-module Monero (wallet2, crypto). On n'y touche pas. |
| `src/libwalletqt/` | Couche Qt au-dessus de l'API wallet de Monero : `Wallet`, `WalletManager`, `PendingTransaction`, `TransactionHistory`, `Subaddress`, `Coins`… Base de l'implémentation Monero de la future couche `CoinWallet`. |
| `src/model/` | Modèles Qt (historique, adresses, coins, nœuds, liste des fichiers de wallet). |
| `src/` (racine) | Fenêtres principales : `MainWindow` (onglets), `SendWidget`, `ReceiveWidget`, `HistoryWidget`, `CoinsWidget`, `ContactsWidget`, `SettingsDialog`, `WindowManager`. |
| `src/wizard/` | Assistant de démarrage (création, restauration, ouverture, réseau). |
| `src/dialog/` | Boîtes de dialogue (seed, clés, confirmation d'envoi, à propos…). |
| `src/utils/` | Réseau, Tor, config, nœuds, prix, utilitaires. |
| `src/plugins/` | Onglets optionnels (Home, Tickers, Calc…). Désactivés dans Biscuit car ils dépendent des serveurs Feather. |
| `src/ui/` | Feuilles de style (`qdarkstyle`, `BreezeStyleSheets`) pour les thèmes. |
| `src/assets/` | Icônes, liste de nœuds intégrée (`nodes.json`), textes (`about.txt`), docs embarquées. |

## Démarrage

`main.cpp` crée `Application` (instance unique via fichier de verrou), charge la config,
choisit le réseau (mainnet/stagenet/testnet) puis lance `WindowManager`.
`WindowManager` gère l'assistant, les fenêtres `MainWindow` (une par wallet ouvert),
l'icône de barre des tâches et les réglages réseau/proxy.

## Wallet Monero

- `WalletManager` ouvre, crée et restaure les wallets (appels asynchrones).
- `Wallet` expose solde (`balanceUpdated`), synchronisation (`syncStatus`,
  `connectionStatusChanged`), adresses (`address(account, index)`), historique,
  création et envoi de transactions (`createTransaction` → `PendingTransaction` →
  `commitTransaction`), stockage (`store`).
- Les seeds : Polyseed par défaut (16 mots), seed Monero 25 mots également gérée
  (`src/polyseed`, `src/monero_seed`).
- Données propres à l'app stockées dans le cache du wallet via `setCacheAttribute`
  (clés `feather.*`, conservées pour rester compatibles avec Feather).

## Onglets et interface

- `MainWindow.ui` contient un `QTabWidget` : Historique, Envoyer, Recevoir, Coins,
  Contacts, plus les onglets de plugins (insérés via `PluginRegistry`).
- L'interface Plugin (`src/plugins/Plugin.h`) permet d'ajouter un onglet sans toucher
  au reste : piste possible pour les futurs onglets Swap et Acheter/Vendre, ce qui
  limiterait les conflits avec Feather.
- Thèmes : `ColorScheme` et les feuilles de style de `src/ui/`. Les nouveaux écrans
  doivent utiliser les widgets Qt standard (formulaires `.ui`, `QFormLayout`,
  `QTableView`) comme le reste de l'app.

## Réseau et Tor

- **Point d'entrée HTTP : `utils/Networking`** (`get`, `getJson`, `postJson`).
  Il choisit automatiquement le bon `QNetworkAccessManager` via `getNetwork()` :
  direct si aucun proxy, sinon SOCKS5 (Tor ou autre), sauf pour les adresses locales.
  **Tout nouveau code réseau (Trocador, Electrum, partenaires) doit passer par là**
  pour respecter le réglage Tor.
- `WindowManager::onProxySettingsChanged()` applique le proxy choisi et démarre ou
  arrête le démon Tor via `TorManager`.
- `TorManager` lance un Tor embarqué seulement si le proxy est Tor, qu'aucun Tor ne
  tourne déjà et que l'app a été compilée avec Tor (`-DTOR_DIR`). Sinon, il utilise
  un Tor local (`127.0.0.1:9050`).
- Niveaux de confidentialité Tor (`torPrivacyLevel`) : tout sauf le nœud, tout sauf
  la synchronisation initiale, tout.
- Biscuit : proxy par défaut = aucun (Tor optionnel).

## Nœuds

- `utils/nodes` choisit un nœud au hasard dans la liste, sinon utilise un nœud
  personnalisé. Sans le websocket Feather, la liste vient de `assets/nodes.json`.
- À terme, Biscuit devra maintenir sa propre liste de nœuds (mise à jour avec
  chaque version, pas de configuration distante).

## Services Feather remplacés par des sources publiques

Le websocket Feather (ws.featherwallet.org) n'est jamais contacté
(`WITH_FEATHER_SERVICES=OFF`). À la place, `WebsocketClient` lance
`datafeed/PublicDataFeed`, qui interroge directement :

| Donnée | Source | Fréquence |
|---|---|---|
| Prix crypto (`crypto_rates`) | CoinGecko | ~10 min |
| Taux fiat (`fiat_rates`) | Frankfurter (taux BCE) | ~1 h |
| Crowdfunding (`ccs`) | ccs.getmonero.org | ~1 h |

Les réponses sont converties au format des messages Feather : conversion fiat,
Tickers, Calc et Home fonctionnent sans modification. Règles de confidentialité :
requêtes identiques pour tous (listes fixes), aucune clé ni identifiant, passage par
`Networking` (Tor si activé), délais aléatoires. Coupé si Tor « onion uniquement » ou
i2p. Activable/désactivable dans l'assistant et les réglages (« Public data »).
Non repris : Revuo (pas d'API publique), liste de nœuds dynamique (liste intégrée),
hauteurs de blocs, mises à jour.

## Config et fichiers

- `utils/config` : réglages JSON (`settings.json`) dans
  `~/Library/Application Support/Biscuit` (macOS), `~/.config/biscuit` (Linux).
- Wallets : `~/Biscuit/wallets` par défaut.
- Mode portable : fichier `.portable` à côté de l'exécutable → dossier `biscuit_data`.

## Build et CI

- CMake. Options utiles : `WITH_SCANNER`, `TOR_DIR`, `CHECK_UPDATES`,
  `WITH_FEATHER_SERVICES`, `WITH_PLUGIN_*`.
- Les builds de version de Feather passent par Guix (`contrib/guix`), reproductibles.
  À reprendre plus tard pour les binaires officiels de Biscuit.
- Feather n'a pas de tests unitaires. Biscuit en ajoute (`tests/`, Qt Test + CTest).

## Où brancher Biscuit

- `src/coins/` : couche `CoinWallet`, avec l'implémentation Monero qui délègue à
  `libwalletqt/Wallet`.
- `src/swap/` : `SwapProvider`, `SwapManager`, Trocador (via `Networking`).
- `src/ramp/` : `RampProvider`.
- Interface : onglets Swap et Acheter/Vendre, probablement sous forme de plugins
  internes, pour modifier le moins possible `MainWindow`.
