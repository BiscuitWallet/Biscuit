// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SILENTPAYMENTS_H
#define BISCUIT_SILENTPAYMENTS_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>

#include "CoinParams.h"

// BIP-352 silent payments, sending side: decoding sp1… addresses and
// deriving the one-time Taproot output keys from the transaction's inputs.
// Built on libsecp256k1 (bundled with libwally-core); checked against the
// BIP's test vectors.
namespace biscuit::coins::sp {

struct Address {
    QByteArray scanKey;    // 33-byte compressed public key
    QByteArray spendKey;   // 33-byte compressed public key
    bool testnet = false;  // "tsp" rather than "sp"
};

// True for strings that look like a silent payment address (sp1…/tsp1…).
bool looksLikeAddress(const QString &text);

// Decodes an sp1… (mainnet) or tsp1… (testnet, signet) address: bech32m,
// version 0 carries exactly the two keys; later versions are read as their
// first 66 bytes, version 31 is refused (BIP-352).
std::optional<Address> decodeAddress(const QString &address, QString *error = nullptr);

// The address if it is a silent payment address for this coin and network
// (Bitcoin only: sp1… on mainnet, tsp1… on testnet).
std::optional<Address> addressFor(const QString &address, const CoinParams &params, QString *error = nullptr);

// One input of the transaction being built; pass them all. Only inputs
// whose public key the sender reveals count toward the shared secret
// (P2WPKH, P2SH-P2WPKH, P2PKH with compressed keys, and Taproot other than
// a NUMS script path), but every input's outpoint takes part in it.
struct Input {
    QString txid;              // hex, as displayed (big-endian)
    quint32 vout = 0;
    QByteArray privateKey;     // 32 bytes, when `counts`
    bool taproot = false;      // x-only key: the private key is negated for an odd y
    bool counts = true;
};

// Most outputs for one scan key in one transaction (BIP-352 K_max).
inline constexpr int maxOutputsPerScanKey = 2323;

// The x-only (32-byte) Taproot output key for each recipient, in the order
// given. Several recipients may share a scan key (numbered outputs).
// Fails when the inputs' keys sum to zero or a group exceeds K_max.
std::optional<QList<QByteArray>> outputKeys(const QList<Input> &inputs, const QList<Address> &recipients,
                                            QString *error = nullptr);

// The P2TR scriptPubKey for an x-only output key: OP_1 <32 bytes>.
QByteArray taprootScript(const QByteArray &xOnlyKey);

}

#endif // BISCUIT_SILENTPAYMENTS_H
