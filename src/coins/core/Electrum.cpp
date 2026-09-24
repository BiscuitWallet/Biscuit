// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Electrum.h"

#include <algorithm>
#include <memory>

#include <QJsonDocument>
#include <QJsonObject>

#include <wally_core.h>
#include <wally_transaction.h>

#include "WallyInit.h"

namespace biscuit::coins::electrum {

// ----------------------------------------------------------------- JSON-RPC

QByteArray request(int id, const QString &method, const QJsonArray &params) {
    const QJsonObject obj{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<Message> parseMessage(const QByteArray &line) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const QJsonObject obj = doc.object();
    Message msg;
    if (obj.contains("id") && obj.value("id").isDouble()) {
        msg.id = obj.value("id").toInt();
        msg.result = obj.value("result");
        const QJsonValue error = obj.value("error");
        if (!error.isNull() && !error.isUndefined()) {
            msg.error = error.isObject() ? error.toObject().value("message").toString() : error.toVariant().toString();
            if (msg.error.isEmpty()) msg.error = "Server error";
        }
        return msg;
    }
    if (obj.contains("method")) {
        msg.method = obj.value("method").toString();
        msg.params = obj.value("params").toArray();
        return msg;
    }
    return std::nullopt;
}

std::optional<QList<QByteArray>> LineBuffer::feed(const QByteArray &data) {
    m_buffer.append(data);
    QList<QByteArray> lines;
    qsizetype start = 0;
    qsizetype nl;
    while ((nl = m_buffer.indexOf('\n', start)) >= 0) {
        if (nl - start > m_max) {
            return std::nullopt;
        }
        const QByteArray line = m_buffer.mid(start, nl - start).trimmed();
        if (!line.isEmpty()) {
            lines.append(line);
        }
        start = nl + 1;
    }
    m_buffer.remove(0, start);
    if (m_buffer.size() > m_max) {
        return std::nullopt;
    }
    return lines;
}

// --------------------------------------------------------------------- fees

double feeRateFromEstimate(double coinPerKb, double minRate) {
    if (!(coinPerKb > 0)) {
        return minRate;
    }
    // coin/kB -> sat/vB: * 1e8 sat/coin / 1000 vB/kB.
    return std::max(minRate, coinPerKb * 1e8 / 1000.0);
}

// ---------------------------------------------------------- address discovery

namespace {
    quint64 key(HdAccount::Chain chain, quint32 index) {
        return (quint64(chain) << 32) | index;
    }
}

QList<AddressRef> GapScanner::nextBatch() {
    QList<AddressRef> batch;
    for (auto chain : {HdAccount::Receive, HdAccount::Change}) {
        ChainState &s = state(chain);
        const qint64 target = s.lastUsed + m_gap;
        for (qint64 i = s.requestedUpTo + 1; i <= target; ++i) {
            batch.append({chain, quint32(i)});
        }
        s.requestedUpTo = std::max(s.requestedUpTo, target);
    }
    return batch;
}

void GapScanner::setUsed(const AddressRef &ref, bool used) {
    m_results[key(ref.chain, ref.index)] = used;
    ChainState &s = state(ref.chain);
    if (used && qint64(ref.index) > s.lastUsed) {
        s.lastUsed = ref.index;
    }
    while (m_results.contains(key(ref.chain, quint32(s.checkedUpTo + 1)))) {
        s.checkedUpTo++;
    }
}

bool GapScanner::done() const {
    for (auto chain : {HdAccount::Receive, HdAccount::Change}) {
        const ChainState &s = state(chain);
        if (s.checkedUpTo < s.lastUsed + qint64(m_gap)) {
            return false;
        }
    }
    return true;
}

quint32 GapScanner::firstUnused(HdAccount::Chain chain) const {
    return quint32(state(chain).lastUsed + 1);
}

// -------------------------------------------------------------- transactions

namespace {
    struct TxDeleter {
        void operator()(wally_tx *tx) const { wally_tx_free(tx); }
    };

    QString hashToHex(const unsigned char *hash) {
        QByteArray b(reinterpret_cast<const char *>(hash), WALLY_TXHASH_LEN);
        std::reverse(b.begin(), b.end());
        return QString::fromLatin1(b.toHex());
    }
}

std::optional<ParsedTx> parseTransaction(const QString &hex) {
    ensureWallyInit();
    wally_tx *raw = nullptr;
    if (wally_tx_from_hex(hex.toLatin1().constData(), WALLY_TX_FLAG_USE_WITNESS, &raw) != WALLY_OK || !raw) {
        return std::nullopt;
    }
    std::unique_ptr<wally_tx, TxDeleter> tx(raw);

    ParsedTx parsed;
    unsigned char txid[WALLY_TXHASH_LEN];
    if (wally_tx_get_txid(tx.get(), txid, sizeof(txid)) != WALLY_OK) {
        return std::nullopt;
    }
    parsed.txid = hashToHex(txid);
    for (size_t i = 0; i < tx->num_inputs; ++i) {
        parsed.inputs.append({hashToHex(tx->inputs[i].txhash), tx->inputs[i].index});
    }
    for (size_t i = 0; i < tx->num_outputs; ++i) {
        const wally_tx_output &o = tx->outputs[i];
        parsed.outputs.append({QByteArray(reinterpret_cast<const char *>(o.script), qsizetype(o.script_len)), o.satoshi});
    }
    return parsed;
}

QList<HistoryEntry> computeHistory(const QMap<QString, ParsedTx> &txs, const QMap<QString, int> &heights,
                                   const QSet<QByteArray> &walletScripts) {
    QList<HistoryEntry> history;
    for (const ParsedTx &tx : txs) {
        quint64 received = 0;
        quint64 outputsTotal = 0;
        for (const TxOut &o : tx.outputs) {
            outputsTotal += o.value;
            if (walletScripts.contains(o.scriptPubKey)) received += o.value;
        }

        quint64 spent = 0;
        quint64 inputsTotal = 0;
        bool allInputsKnown = !tx.inputs.isEmpty();
        for (const TxIn &in : tx.inputs) {
            const auto prev = txs.constFind(in.prevTxid);
            if (prev == txs.constEnd() || in.prevVout >= quint32(prev->outputs.size())) {
                allInputsKnown = false;
                continue;
            }
            const TxOut &prevOut = prev->outputs.at(in.prevVout);
            inputsTotal += prevOut.value;
            if (walletScripts.contains(prevOut.scriptPubKey)) {
                spent += prevOut.value;
            } else {
                allInputsKnown = false;
            }
        }

        HistoryEntry e;
        e.txid = tx.txid;
        e.height = heights.value(tx.txid, 0);
        e.delta = qint64(received) - qint64(spent);
        if (allInputsKnown && inputsTotal >= outputsTotal) {
            e.fee = inputsTotal - outputsTotal;
        }
        history.append(e);
    }

    // Newest first: unconfirmed, then by decreasing height.
    std::stable_sort(history.begin(), history.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        const bool ua = a.height <= 0, ub = b.height <= 0;
        if (ua != ub) return ua;
        return a.height > b.height;
    });
    return history;
}

QList<WalletUtxo> computeUtxos(const QMap<QString, ParsedTx> &txs, const QMap<QString, int> &heights,
                               const QMap<QByteArray, AddressRef> &walletScripts) {
    QSet<QString> spentOutpoints;
    for (const ParsedTx &tx : txs) {
        for (const TxIn &in : tx.inputs) {
            spentOutpoints.insert(in.prevTxid + ':' + QString::number(in.prevVout));
        }
    }

    QList<WalletUtxo> utxos;
    for (const ParsedTx &tx : txs) {
        for (int i = 0; i < tx.outputs.size(); ++i) {
            const TxOut &o = tx.outputs.at(i);
            const auto ref = walletScripts.constFind(o.scriptPubKey);
            if (ref == walletScripts.constEnd() || spentOutpoints.contains(tx.txid + ':' + QString::number(i))) {
                continue;
            }
            WalletUtxo w;
            w.utxo.txid = tx.txid;
            w.utxo.vout = quint32(i);
            w.utxo.value = o.value;
            w.utxo.chain = ref->chain;
            w.utxo.index = ref->index;
            w.utxo.height = heights.value(tx.txid, 0);
            w.scriptPubKey = o.scriptPubKey;
            utxos.append(w);
        }
    }
    return utxos;
}

}
