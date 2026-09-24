// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_COINPARAMS_H
#define BISCUIT_COINPARAMS_H

#include <QList>
#include <QString>

namespace biscuit::coins {

// Network parameters of a Bitcoin-like coin. BTC and LTC share all the wallet
// code; only these values differ.
struct CoinParams {
    QString ticker;           // "BTC"
    QString name;             // "Bitcoin"
    QString bech32Hrp;        // "bc"
    quint32 bip44CoinType;    // BIP44 coin type: 0 = BTC, 2 = LTC, 1 = testnets
    int decimals;             // 8
    quint8 p2pkhVersion;      // base58 version byte of "1..." / "L..." addresses
    QList<quint8> p2shVersions;  // base58 version bytes of script hash addresses

    bool operator==(const CoinParams &other) const { return ticker == other.ticker && bech32Hrp == other.bech32Hrp; }
};

const CoinParams &bitcoin();
const CoinParams &litecoin();
const CoinParams &bitcoinTestnet();
const CoinParams &litecoinTestnet();

}

#endif // BISCUIT_COINPARAMS_H
