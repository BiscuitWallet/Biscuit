// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINPARAMS_H
#define BISCUIT_COINPARAMS_H

#include <QList>
#include <QString>

namespace biscuit::coins {

// Network parameters of a Bitcoin-like coin. BTC, LTC and DOGE share all the
// wallet code; only these values differ. Ethereum and Solana have their own
// wallets (EthWallet, SolWallet): only their names and decimals are here, so
// that they are listed like the others.
struct CoinParams {
    QString ticker;           // "BTC"
    QString name;             // "Bitcoin"
    QString bech32Hrp;        // "bc"
    quint32 bip44CoinType;    // BIP44 coin type: 0 = BTC, 2 = LTC, 1 = testnets
    int decimals;             // 8
    quint8 p2pkhVersion;      // base58 version byte of "1..." / "L..." addresses
    QList<quint8> p2shVersions;  // base58 version bytes of script hash addresses
    bool ethereum = false;       // ETH (with its tokens), not a Bitcoin-like coin
    bool solana = false;         // SOL, not a Bitcoin-like coin
    // No SegWit (DOGE): "D..." P2PKH addresses on m/44'/coin'/account',
    // legacy transactions, no replace-by-fee. Otherwise BIP84 P2WPKH.
    bool legacy = false;
    quint64 dustLimit = 546;     // smallest output created, in base units
    // Fee rates in base units per vbyte: the floor used when the server's
    // estimate is lower or unknown, and a sanity ceiling.
    double minFeeRate = 1.0;
    double maxFeeRate = 10000.0;

    bool operator==(const CoinParams &other) const { return ticker == other.ticker && bech32Hrp == other.bech32Hrp; }
};

const CoinParams &bitcoin();
const CoinParams &litecoin();
const CoinParams &ethereum();
const CoinParams &solana();
const CoinParams &dogecoin();
const CoinParams &bitcoinTestnet();
const CoinParams &litecoinTestnet();

// The coins with Electrum wallets (CoinWallet): BTC, LTC, DOGE.
const QList<const CoinParams *> &electrumCoins();

// A transaction of an Electrum coin on its block explorer.
QString explorerTxUrl(const CoinParams &params, const QString &txid);

}

#endif // BISCUIT_COINPARAMS_H
