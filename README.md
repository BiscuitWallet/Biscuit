<p align="center">
  <img src="src/assets/images/appicons/256x256.png" width="128" height="128" alt="Biscuit">
</p>

<h1 align="center">Biscuit Wallet</h1>

<p align="center">
  <em>A light, private desktop wallet for Monero, Bitcoin and Litecoin.</em><br>
  <a href="https://biscuitwallet.com">Website</a> ·
  <a href="https://biscuitwallet.com/download/">Download</a> ·
  <a href="https://biscuitwallet.com/docs/">Documentation</a> ·
  <a href="https://biscuitwallet.com/why/">Why Biscuit?</a>
</p>

---

Biscuit keeps Monero, Bitcoin and Litecoin in one small desktop app. Swap between them without an account, turn on Tor with one click, and keep your keys where they belong: on your computer.

It is built on [Feather Wallet](https://featherwallet.org) and keeps what makes Feather good: a native Qt app, keys that never leave your machine, and nothing that phones home. Biscuit adds Bitcoin and Litecoin wallets and built-in swaps.

## Features

- **Three coins, one wallet.** Monero, Bitcoin and Litecoin behind a single password. Several Bitcoin and Litecoin wallets side by side.
- **Exchange swaps.** XMR, BTC and LTC in any direction, with offers from many exchanges through [Trocador](https://trocador.app). Offers are ranked only by what you receive; no account, no identity checks (exchanges rated A only).
- **Atomic swaps (beta).** Bitcoin to Monero directly with independent market makers, using the [eigenwallet](https://eigenwallet.org) protocol. Nobody holds your coins during the swap.
- **Tor built in.** One switch routes every connection through Tor. On Tails and Whonix, Biscuit uses the system's Tor.
- **Nothing phones home.** No accounts, no analytics, no crash reports. Prices come straight from public sources, with the same requests for everyone.
- **Everything Feather does for Monero:** coin control, subaddresses, node management and more.

## Privacy

Your seeds, keys, balances and history stay on your computer, encrypted with your password. Exchange swaps go through a small Biscuit server that holds our partner key; what it records is described in the [privacy policy](https://biscuitwallet.com/privacy/). Atomic swaps do not use it.

## Download

Biscuit is not released yet. Builds for Linux (AppImage, Flatpak, Tails), macOS and Windows will be published on [biscuitwallet.com/download](https://biscuitwallet.com/download/), with checksums and a signature to [verify](https://biscuitwallet.com/docs/verify/) before you install.

## Building

See [BUILD.md](BUILD.md). Biscuit builds with CMake and Qt 6; macOS (Apple Silicon) is documented first, Linux and Windows follow.

## Contact

[biscuitwallet@tutamail.com](mailto:biscuitwallet@tutamail.com)

Nobody from Biscuit will ever ask for your seed, password or private keys.

## Credits

Biscuit stands on the work of others:

- [Feather Wallet](https://featherwallet.org), the foundation of this app
- [The Monero Project](https://getmonero.org), for Monero and its wallet library
- [eigenwallet](https://eigenwallet.org), for the BTC → XMR atomic swap protocol
- [libwally-core](https://github.com/ElementsProject/libwally-core), for Bitcoin and Litecoin keys and transactions
- [Trocador](https://trocador.app), for exchange swaps

## License

Biscuit is free software, released under the BSD 3-Clause License inherited from Feather Wallet and Monero. See [LICENSE](LICENSE).
