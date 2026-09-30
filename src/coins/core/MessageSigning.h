// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MESSAGESIGNING_H
#define BISCUIT_MESSAGESIGNING_H

#include <QByteArray>
#include <QString>

#include "CoinParams.h"

// Signed messages ("<Coin> Signed Message"), as Bitcoin Core, Electrum and
// Sparrow use them: a recoverable ECDSA signature of the message hash, base64.
namespace biscuit::coins::message {

// double-SHA256 of "\x18Bitcoin Signed Message:\n" (or Litecoin) + length + message.
QByteArray hash(const QString &message, const CoinParams &params);

// Base64 signature with the key of a compressed-key address. The header byte
// is the Electrum one (31-34), which Electrum, Sparrow and BIP-137 verifiers
// accept for bc1q…/ltc1q… addresses; empty on failure.
QString sign(const QByteArray &privateKey, const QString &message, const CoinParams &params);

// True when `signature` was made for `message` by the key of `address`:
// P2PKH (compressed or not), P2SH-P2WPKH and P2WPKH addresses, with either the
// Electrum (27-34) or the BIP-137 (35-42) header bytes.
bool verify(const QString &address, const QString &message, const QString &signature, const CoinParams &params);

}

#endif // BISCUIT_MESSAGESIGNING_H
