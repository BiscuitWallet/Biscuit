<p align="center">
  <img src="src/assets/images/appicons/256x256.png" width="112" height="112" alt="Biscuit, a pixel-art bear holding a Monero coin">
</p>

<h1 align="center">Biscuit Wallet</h1>

<p align="center">
  <em>A private and modern desktop wallet for Monero, Bitcoin and Litecoin.</em><br><br>
  <a href="https://biscuitwallet.com">Website</a> ·
  <a href="https://biscuitwallet.com/download/">Download</a> ·
  <a href="https://biscuitwallet.com/docs/">Documentation</a> ·
  <a href="https://biscuitwallet.com/news/">Journal</a> ·
  <a href="https://biscuitwallet.com/features/">Features</a> ·
  <a href="https://biscuitwallet.com/donate/">Donate</a>
</p>

<p align="center">
  <img src="https://biscuitwallet.com/img/gallery/atomic-search.png" width="760" alt="Biscuit looking for atomic swap makers over Tor, with offers to swap Bitcoin for Monero">
</p>

Biscuit started as a fork of [Feather Wallet](https://featherwallet.org), which is about as good as a Monero wallet gets on a computer. We added everything we love: Bitcoin and Litecoin next to Monero, swaps between all three, atomic swaps with no exchange in the middle, Tor for the whole app, and lots of little details. It runs on Windows, macOS and Linux.

## What's inside

- **Monero**, the way Feather does it: subaddresses, coin control, transaction proofs, view-only wallets, Ledger and Trezor, offline signing with QR codes, your own node.
- **Bitcoin and Litecoin**: light wallets over Electrum servers (public ones or your own), several wallets per coin under one password, coin control, coin freezing, fee bumping, silent payment sending, signed messages.
- **Swaps**: any pair of XMR, BTC and LTC through [Trocador](https://trocador.app), offers from exchanges that ask for no identity checks, ranked only by what you receive.
- **Atomic swaps** (beta): Bitcoin to Monero directly with a market maker, using the [eigenwallet](https://eigenwallet.org) protocol. No exchange holds your coins along the way.
- **Tor**: built in, for every connection, or only for prices and news. On Tails and Whonix, Biscuit uses the system's Tor. In Tor mode, prices, the Journal and updates come from our onion service.
- **No account, no analytics, no crash reports.** Wallet files are encrypted with your password and stay on your computer.

## Download

| System | Files |
|---|---|
| **Windows** 10 and later | installer or portable zip |
| **macOS** 14 and later | Apple Silicon or Intel |
| **Linux** x86_64 | AppImage (also for Tails), `.deb`, `.rpm`, Arch package, `.tar.gz` |

Get them from **[biscuitwallet.com/download](https://biscuitwallet.com/download/)**, with the commands to install from a terminal. Biscuit updates itself after checking the release signature.

Also over Tor: `biscuit6qpejzxfr7us7oibhjasvrozfeno7xonffzzoj4lmw6o3kbyd.onion`

## Verifying a release

Every release is signed with the Biscuit release key:

```
Biscuit Wallet releases
D714 332E 6101 C1DA AC60  E644 202F 1FA3 98DE CEFA
```

Releases are built with [Guix](https://guix.gnu.org) from the published source archive, so anyone can rebuild them and compare. The steps, and the two exceptions (the macOS signature, the atomic swap helper), are in [Verifying your download](https://biscuitwallet.com/docs/verify/).

## Building

See [BUILD.md](BUILD.md) for a development build (CMake, Qt 6) and [RELEASE.md](RELEASE.md) for release builds.

## Contributing

Biscuit is open source so that anyone can read the code, verify our builds and build it themselves. It is made by a small team, so we don't take pull requests and can't answer questions or support requests on GitHub. You're welcome to fork it under the terms of the license.

## Security

Please report vulnerabilities privately, as described in [SECURITY.md](SECURITY.md). Nobody from Biscuit will ever ask for your seed, your password or your private keys.

## Support Biscuit

Biscuit is made and looked after with a lot of love. If you've grown fond of it too, [donations](https://biscuitwallet.com/donate/) in Monero, Bitcoin or Litecoin help us keep going.

## Credits

Biscuit stands on the work of others: [Feather Wallet](https://featherwallet.org), [The Monero Project](https://getmonero.org), [eigenwallet](https://eigenwallet.org) for atomic swaps, [libwally-core](https://github.com/ElementsProject/libwally-core) for Bitcoin and Litecoin, [Trocador](https://trocador.app) for exchange swaps, and [Tor](https://www.torproject.org).

## License

Biscuit is free software under the BSD 3-Clause License inherited from Feather Wallet and Monero, see [LICENSE](LICENSE). Its atomic swap helper, biscuit-swapd, is under GPL-3.0; its source is published with every release.

Contact: [biscuitwallet@tutamail.com](mailto:biscuitwallet@tutamail.com)
