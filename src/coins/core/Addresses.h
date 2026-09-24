// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ADDRESSES_H
#define BISCUIT_ADDRESSES_H

#include <optional>

#include <QByteArray>
#include <QString>

#include "CoinParams.h"

namespace biscuit::coins {

    // OP_0 <hash160(pubkey)> for a 33-byte compressed public key.
    QByteArray p2wpkhScriptPubKey(const QByteArray &publicKey);
    QString p2wpkhAddress(const QByteArray &publicKey, const CoinParams &params);

    // Destination check for sending: accepts native SegWit (bech32/bech32m of
    // the coin's prefix) and legacy base58 addresses of the coin. Returns the
    // scriptPubKey to pay, or std::nullopt if the address is not valid for this
    // coin (wrong network, bad checksum…).
    std::optional<QByteArray> addressToScriptPubKey(const QString &address, const CoinParams &params);

    inline bool isValidAddress(const QString &address, const CoinParams &params) {
        return addressToScriptPubKey(address, params).has_value();
    }

    // Electrum protocol script hash: sha256(scriptPubKey), byte-reversed, hex.
    QString electrumScriptHash(const QByteArray &scriptPubKey);
}

#endif // BISCUIT_ADDRESSES_H
