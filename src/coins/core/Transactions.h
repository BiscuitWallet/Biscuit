// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_TRANSACTIONS_H
#define BISCUIT_TRANSACTIONS_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>

#include "HdAccount.h"

// Building and signing BTC/LTC transactions from the wallet's own P2WPKH
// coins, and DOGE ones from its P2PKH coins (params.legacy). Planning (coin
// selection, fee, change) is plain arithmetic in base units; signing and
// serialization are done by libwally-core.
namespace biscuit::coins {

struct Utxo {
    QString txid;            // hex, as displayed by explorers and Electrum
    quint32 vout = 0;
    quint64 value = 0;       // satoshis
    HdAccount::Chain chain = HdAccount::Receive;
    quint32 index = 0;       // address index on that chain
    int height = 0;          // 0 = unconfirmed
};

struct TxOutput {
    QByteArray scriptPubKey;
    quint64 value = 0;
};

struct TxPlan {
    QList<Utxo> inputs;
    QList<TxOutput> outputs;   // destination and optional change, in final order
    int changeOutput = -1;     // index in outputs, -1 if no change
    quint64 amount = 0;        // sent to the destination
    quint64 fee = 0;
    int estimatedVsize = 0;
    // Paying a silent payment address (sp1…): the output is a one-time
    // Taproot key derived from the chosen inputs (BIP-352).
    QString silentPaymentAddress;
};

struct SignedTx {
    QString hex;
    QString txid;
    int vsize = 0;
    quint64 fee = 0;
};

// Outputs worth less than params.dustLimit are not created (change goes to
// the fee). Fee rates are in base units per vbyte (sat/vB; koinu/B for DOGE).

// Estimated virtual size of a transaction spending `inputs` of the wallet's
// coins (P2WPKH, or P2PKH for a legacy coin) to outputs with the given
// scriptPubKey sizes (worst-case signature length).
int estimateVsize(const CoinParams &params, int inputs, const QList<int> &outputScriptSizes);

// What spending one of the wallet's coins adds to a transaction's fee at
// `feeRate`: 68 vB for native SegWit, 148 B for P2PKH. A coin worth no more
// than that is dust: it costs more to move than it brings.
quint64 inputCost(const CoinParams &params, double feeRate);
inline bool isDust(const CoinParams &params, quint64 value, double feeRate) { return value <= inputCost(params, feeRate); }

// Chooses coins and computes fee and change.
//  - sendAll: spends every coin, amount = total - fee;
//  - otherwise prefers a single coin (fewer links between addresses), else
//    adds the largest coins until the amount and fee are covered.
// Change below the dust limit is added to the fee. The change output is put
// at a random position.
std::optional<TxPlan> planTransaction(const CoinParams &params, const QList<Utxo> &utxos, const QByteArray &destination, quint64 amount,
                                      double feeRate, const QByteArray &changeScript, bool sendAll,
                                      QString *error = nullptr);

// Replace-by-fee (BIP-125): the same payment again with a higher fee. Keeps
// every input and every output of the original except the change
// (`changeOutput`, -1 if none), which pays the extra fee; when it cannot,
// adds coins from `extraCoins` (largest first) with change to `changeScript`.
// The new fee is at least `feeRate` × size and exceeds the original fee by at
// least 1 sat/vB of the new size (relay rule). Legacy coins don't signal
// replace-by-fee: refused.
std::optional<TxPlan> planFeeBump(const CoinParams &params, const QList<Utxo> &inputs, const QList<TxOutput> &outputs, int changeOutput,
                                  quint64 originalFee, double feeRate, QList<Utxo> extraCoins,
                                  const QByteArray &changeScript, QString *error = nullptr);

// Virtual size of a serialized transaction (hex), 0 if it cannot be parsed.
int transactionVsize(const QString &hex);

// Signs every input with the account keys (BIP143, SIGHASH_ALL, low-R) and
// verifies each signature before returning. `lockTime` should be the current
// block height (anti fee-sniping). Inputs signal replace-by-fee. A legacy
// coin's transaction (version 1) is signed with the original signature hash,
// the signatures in each input's script, without replace-by-fee.
std::optional<SignedTx> signTransaction(const TxPlan &plan, const HdAccount &account, quint32 lockTime,
                                        QString *error = nullptr);

}

#endif // BISCUIT_TRANSACTIONS_H
