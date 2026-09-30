// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Transactions.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QRandomGenerator>

#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_script.h>
#include <wally_transaction.h>

#include "WallyInit.h"

namespace biscuit::coins {

namespace {
    // Virtual sizes (vB) for native SegWit.
    constexpr double txOverhead = 10.5;   // version, locktime, counts, segwit marker
    constexpr double p2wpkhInput = 68.0;  // outpoint, sequence, witness (72-byte sig worst case)
    constexpr quint32 rbfSequence = 0xfffffffd;

    int outputSize(int scriptSize) {
        return 8 + 1 + scriptSize;  // value, script length, script
    }

    quint64 feeFor(int vsize, double feeRate) {
        return static_cast<quint64>(std::ceil(vsize * feeRate));
    }

    void setError(QString *error, const QString &message) {
        if (error) *error = message;
    }

    QByteArray txidToInternal(const QString &txid) {
        QByteArray bytes = QByteArray::fromHex(txid.toLatin1());
        std::reverse(bytes.begin(), bytes.end());
        return bytes;
    }

    struct TxDeleter {
        void operator()(wally_tx *tx) const { wally_tx_free(tx); }
    };
    struct WitnessDeleter {
        void operator()(wally_tx_witness_stack *w) const { wally_tx_witness_stack_free(w); }
    };

    const unsigned char *u(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }
}

int estimateVsize(int inputs, const QList<int> &outputScriptSizes) {
    double vsize = txOverhead + inputs * p2wpkhInput;
    for (int size : outputScriptSizes) {
        vsize += outputSize(size);
    }
    return static_cast<int>(std::ceil(vsize));
}

std::optional<TxPlan> planTransaction(const QList<Utxo> &utxos, const QByteArray &destination, quint64 amount,
                                      double feeRate, const QByteArray &changeScript, bool sendAll, QString *error) {
    if (destination.isEmpty() || changeScript.isEmpty()) {
        setError(error, "Invalid address");
        return std::nullopt;
    }
    if (!(feeRate >= 1.0) || feeRate > 10000.0) {
        setError(error, "Invalid fee rate");
        return std::nullopt;
    }
    if (utxos.isEmpty()) {
        setError(error, "No funds available");
        return std::nullopt;
    }

    quint64 total = 0;
    for (const Utxo &u : utxos) total += u.value;

    TxPlan plan;

    if (sendAll) {
        plan.inputs = utxos;
        plan.estimatedVsize = estimateVsize(utxos.size(), {int(destination.size())});
        plan.fee = feeFor(plan.estimatedVsize, feeRate);
        if (total <= plan.fee || total - plan.fee < dustLimit) {
            setError(error, "Balance too low to pay the network fee");
            return std::nullopt;
        }
        plan.amount = total - plan.fee;
        plan.outputs = {{destination, plan.amount}};
        return plan;
    }

    if (amount < dustLimit) {
        setError(error, QString("Amount too small (minimum %1 sats)").arg(dustLimit));
        return std::nullopt;
    }

    const int destSize = destination.size();
    const int changeSize = changeScript.size();
    auto feeWithChange = [&](int n) { return feeFor(estimateVsize(n, {destSize, changeSize}), feeRate); };
    auto feeNoChange = [&](int n) { return feeFor(estimateVsize(n, {destSize}), feeRate); };

    // 1. A single coin: the smallest one that covers everything.
    QList<Utxo> selected;
    const Utxo *best = nullptr;
    for (const Utxo &u : utxos) {
        if (u.value >= amount + feeNoChange(1) && (!best || u.value < best->value)) {
            best = &u;
        }
    }
    if (best) {
        selected = {*best};
    } else {
        // 2. Largest coins first until amount + fee are covered.
        QList<Utxo> sorted = utxos;
        std::sort(sorted.begin(), sorted.end(), [](const Utxo &a, const Utxo &b) { return a.value > b.value; });
        quint64 sum = 0;
        for (const Utxo &u : sorted) {
            selected.append(u);
            sum += u.value;
            if (sum >= amount + feeWithChange(selected.size())) {
                break;
            }
        }
        if (sum < amount + feeNoChange(selected.size())) {
            setError(error, "Insufficient funds (amount + network fee)");
            return std::nullopt;
        }
    }

    quint64 inputSum = 0;
    for (const Utxo &u : selected) inputSum += u.value;
    const int n = selected.size();

    plan.inputs = selected;
    plan.amount = amount;
    const quint64 fee2 = feeWithChange(n);
    if (inputSum >= amount + fee2 && inputSum - amount - fee2 >= dustLimit) {
        const quint64 change = inputSum - amount - fee2;
        plan.fee = fee2;
        plan.estimatedVsize = estimateVsize(n, {destSize, changeSize});
        // Random position: the change output must not be guessable.
        const bool changeFirst = QRandomGenerator::global()->bounded(2) == 0;
        plan.outputs = changeFirst ? QList<TxOutput>{{changeScript, change}, {destination, amount}}
                                   : QList<TxOutput>{{destination, amount}, {changeScript, change}};
        plan.changeOutput = changeFirst ? 0 : 1;
    } else {
        // No change: the remainder (below dust) goes to the fee.
        plan.fee = inputSum - amount;
        plan.estimatedVsize = estimateVsize(n, {destSize});
        plan.outputs = {{destination, amount}};
    }
    return plan;
}

std::optional<TxPlan> planFeeBump(const QList<Utxo> &inputs, const QList<TxOutput> &outputs, int changeOutput,
                                  quint64 originalFee, double feeRate, QList<Utxo> extraCoins,
                                  const QByteArray &changeScript, QString *error) {
    if (inputs.isEmpty() || outputs.isEmpty() || changeOutput >= outputs.size()) {
        setError(error, "This transaction cannot be replaced");
        return std::nullopt;
    }
    if (!(feeRate >= 1.0) || feeRate > 10000.0) {
        setError(error, "Invalid fee rate");
        return std::nullopt;
    }

    TxPlan plan;
    plan.inputs = inputs;
    quint64 paid = 0;   // what the payment itself needs (every output but the change)
    for (int i = 0; i < outputs.size(); ++i) {
        if (i != changeOutput) {
            plan.outputs.append(outputs[i]);
            paid += outputs[i].value;
        }
    }
    const QByteArray change = changeOutput >= 0 ? outputs[changeOutput].scriptPubKey : changeScript;
    std::sort(extraCoins.begin(), extraCoins.end(), [](const Utxo &a, const Utxo &b) { return a.value > b.value; });

    for (;;) {
        quint64 total = 0;
        for (const Utxo &u : plan.inputs) total += u.value;
        QList<int> sizes;
        for (const TxOutput &o : plan.outputs) sizes.append(o.scriptPubKey.size());

        // With change first; without it when the change would be dust.
        for (const bool withChange : {true, false}) {
            QList<int> s = sizes;
            if (withChange) s.append(change.size());
            const int vsize = estimateVsize(plan.inputs.size(), s);
            const quint64 fee = std::max(feeFor(vsize, feeRate), originalFee + quint64(vsize));
            if (total < paid + fee) {
                continue;
            }
            const quint64 rest = total - paid - fee;
            if (withChange && rest < dustLimit) {
                continue;
            }
            TxPlan result = plan;
            result.fee = fee + (withChange ? 0 : rest);
            result.estimatedVsize = vsize;
            result.amount = paid;
            if (withChange) {
                // Keep the change where it was (or at the end when new).
                const int at = changeOutput >= 0 ? std::min<int>(changeOutput, result.outputs.size()) : result.outputs.size();
                result.outputs.insert(at, {change, rest});
                result.changeOutput = at;
            }
            return result;
        }
        if (extraCoins.isEmpty()) {
            setError(error, "Not enough funds to raise the fee: the change is too small and no other coin is available");
            return std::nullopt;
        }
        plan.inputs.append(extraCoins.takeFirst());
        if (change.isEmpty()) {
            setError(error, "Invalid change address");
            return std::nullopt;
        }
    }
}

int transactionVsize(const QString &hex) {
    ensureWallyInit();
    wally_tx *raw = nullptr;
    if (wally_tx_from_hex(hex.toLatin1().constData(), WALLY_TX_FLAG_USE_WITNESS, &raw) != WALLY_OK) {
        return 0;
    }
    std::unique_ptr<wally_tx, TxDeleter> tx(raw);
    size_t vsize = 0;
    return wally_tx_get_vsize(tx.get(), &vsize) == WALLY_OK ? int(vsize) : 0;
}

std::optional<SignedTx> signTransaction(const TxPlan &plan, const HdAccount &account, quint32 lockTime, QString *error) {
    ensureWallyInit();
    if (plan.inputs.isEmpty() || plan.outputs.isEmpty()) {
        setError(error, "Empty transaction");
        return std::nullopt;
    }

    wally_tx *raw = nullptr;
    if (wally_tx_init_alloc(WALLY_TX_VERSION_2, lockTime, plan.inputs.size(), plan.outputs.size(), &raw) != WALLY_OK) {
        setError(error, "Unable to create transaction");
        return std::nullopt;
    }
    std::unique_ptr<wally_tx, TxDeleter> tx(raw);

    for (const Utxo &in : plan.inputs) {
        const QByteArray hash = txidToInternal(in.txid);
        if (hash.size() != WALLY_TXHASH_LEN
            || wally_tx_add_raw_input(tx.get(), u(hash), hash.size(), in.vout, rbfSequence, nullptr, 0, nullptr, 0) != WALLY_OK) {
            setError(error, "Invalid input");
            return std::nullopt;
        }
    }
    for (const TxOutput &out : plan.outputs) {
        if (wally_tx_add_raw_output(tx.get(), out.value, u(out.scriptPubKey), out.scriptPubKey.size(), 0) != WALLY_OK) {
            setError(error, "Invalid output");
            return std::nullopt;
        }
    }

    for (int i = 0; i < plan.inputs.size(); ++i) {
        const Utxo &in = plan.inputs.at(i);
        const QByteArray pub = account.publicKey(in.chain, in.index);
        QByteArray priv = account.privateKey(in.chain, in.index);

        // BIP143 scriptCode of a P2WPKH input is the P2PKH script of the key.
        unsigned char scriptCode[WALLY_SCRIPTPUBKEY_P2PKH_LEN];
        size_t scriptLen = 0;
        unsigned char sighash[SHA256_LEN];
        unsigned char sig[EC_SIGNATURE_LEN];
        bool ok = wally_scriptpubkey_p2pkh_from_bytes(u(pub), pub.size(), WALLY_SCRIPT_HASH160,
                                                      scriptCode, sizeof(scriptCode), &scriptLen) == WALLY_OK
               && wally_tx_get_btc_signature_hash(tx.get(), i, scriptCode, scriptLen, in.value, WALLY_SIGHASH_ALL,
                                                  WALLY_TX_FLAG_USE_WITNESS, sighash, sizeof(sighash)) == WALLY_OK
               && priv.size() == EC_PRIVATE_KEY_LEN
               && wally_ec_sig_from_bytes(u(priv), priv.size(), sighash, sizeof(sighash),
                                          EC_FLAG_ECDSA | EC_FLAG_GRIND_R, sig, sizeof(sig)) == WALLY_OK
               // Never broadcast a signature that does not verify.
               && wally_ec_sig_verify(u(pub), pub.size(), sighash, sizeof(sighash), EC_FLAG_ECDSA, sig, sizeof(sig)) == WALLY_OK;
        wally_bzero(priv.data(), priv.size());
        if (!ok) {
            setError(error, "Signing failed");
            return std::nullopt;
        }

        wally_tx_witness_stack *rawWitness = nullptr;
        if (wally_witness_p2wpkh_from_sig(u(pub), pub.size(), sig, sizeof(sig), WALLY_SIGHASH_ALL, &rawWitness) != WALLY_OK) {
            setError(error, "Signing failed");
            return std::nullopt;
        }
        std::unique_ptr<wally_tx_witness_stack, WitnessDeleter> witness(rawWitness);
        if (wally_tx_set_input_witness(tx.get(), i, witness.get()) != WALLY_OK) {
            setError(error, "Signing failed");
            return std::nullopt;
        }
    }

    SignedTx result;
    char *hex = nullptr;
    size_t vsize = 0;
    unsigned char txid[WALLY_TXHASH_LEN];
    if (wally_tx_to_hex(tx.get(), WALLY_TX_FLAG_USE_WITNESS, &hex) != WALLY_OK
        || wally_tx_get_vsize(tx.get(), &vsize) != WALLY_OK
        || wally_tx_get_txid(tx.get(), txid, sizeof(txid)) != WALLY_OK) {
        if (hex) wally_free_string(hex);
        setError(error, "Unable to serialize transaction");
        return std::nullopt;
    }
    result.hex = QString::fromLatin1(hex);
    wally_free_string(hex);
    QByteArray id(reinterpret_cast<const char *>(txid), sizeof(txid));
    std::reverse(id.begin(), id.end());
    result.txid = QString::fromLatin1(id.toHex());
    result.vsize = static_cast<int>(vsize);
    result.fee = plan.fee;
    return result;
}

}
