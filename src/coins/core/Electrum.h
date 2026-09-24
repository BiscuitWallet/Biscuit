// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ELECTRUM_H
#define BISCUIT_ELECTRUM_H

#include <optional>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include "HdAccount.h"
#include "Transactions.h"

// Pure (network-free) part of the Electrum protocol client used by the BTC/LTC
// wallets: JSON-RPC framing, address discovery, transaction analysis.
// Protocol: https://electrum-protocol.readthedocs.io
namespace biscuit::coins::electrum {

// ----------------------------------------------------------------- JSON-RPC

// One request per line.
QByteArray request(int id, const QString &method, const QJsonArray &params = {});

struct Message {
    std::optional<int> id;      // responses
    QJsonValue result;
    QString error;              // non-empty on error responses
    QString method;             // notifications (subscriptions)
    QJsonArray params;
};
std::optional<Message> parseMessage(const QByteArray &line);

// Splits the TCP stream into lines. A server sending a line longer than
// `maxLineBytes` is treated as hostile (memory exhaustion) and must be dropped.
class LineBuffer {
public:
    explicit LineBuffer(qsizetype maxLineBytes = 8 * 1024 * 1024) : m_max(maxLineBytes) {}
    // Returns the complete lines received so far; std::nullopt if a line is too long.
    std::optional<QList<QByteArray>> feed(const QByteArray &data);
    void clear() { m_buffer.clear(); }

private:
    QByteArray m_buffer;
    qsizetype m_max;
};

// --------------------------------------------------------------------- fees

// blockchain.estimatefee answers in coin/kB (-1 if unknown). Returns sat/vB,
// never below `minRate`.
double feeRateFromEstimate(double coinPerKb, double minRate = 1.0);

// ---------------------------------------------------------- address discovery

struct AddressRef {
    HdAccount::Chain chain;
    quint32 index;
    bool operator==(const AddressRef &o) const { return chain == o.chain && index == o.index; }
};

// BIP44 gap limit discovery: addresses are checked in order on both chains,
// and scanning stops after `gap` consecutive addresses without any history.
class GapScanner {
public:
    explicit GapScanner(quint32 gap = 20) : m_gap(gap) {}

    // Next addresses to check (empty when discovery is complete).
    QList<AddressRef> nextBatch();
    void setUsed(const AddressRef &ref, bool used);
    bool done() const;

    // First index never used on a chain (next fresh receive/change address).
    quint32 firstUnused(HdAccount::Chain chain) const;

private:
    struct ChainState {
        qint64 lastUsed = -1;     // highest index with history
        qint64 checkedUpTo = -1;  // highest index whose result is known
        qint64 requestedUpTo = -1;
    };
    ChainState &state(HdAccount::Chain chain) { return chain == HdAccount::Receive ? m_receive : m_change; }
    const ChainState &state(HdAccount::Chain chain) const { return chain == HdAccount::Receive ? m_receive : m_change; }

    quint32 m_gap;
    ChainState m_receive;
    ChainState m_change;
    QMap<quint64, bool> m_results;  // key = chain << 32 | index
};

// -------------------------------------------------------------- transactions

struct TxIn {
    QString prevTxid;
    quint32 prevVout = 0;
};

struct TxOut {
    QByteArray scriptPubKey;
    quint64 value = 0;
};

struct ParsedTx {
    QString txid;
    QList<TxIn> inputs;
    QList<TxOut> outputs;
};

// Parses a raw transaction (hex, with or without witness).
std::optional<ParsedTx> parseTransaction(const QString &hex);

struct HistoryEntry {
    QString txid;
    int height = 0;            // 0 or negative = unconfirmed
    qint64 delta = 0;          // satoshis received minus sent by this wallet
    std::optional<quint64> fee;  // known when every input belongs to the wallet
};

// Computes what each transaction changed for the wallet, from the wallet's
// transactions and scriptPubKeys. Inputs spending coins of other transactions
// of the wallet count as spent.
QList<HistoryEntry> computeHistory(const QMap<QString, ParsedTx> &txs, const QMap<QString, int> &heights,
                                   const QSet<QByteArray> &walletScripts);

// Unspent outputs of the wallet, derived from its own transactions.
struct WalletUtxo {
    Utxo utxo;
    QByteArray scriptPubKey;
};
QList<WalletUtxo> computeUtxos(const QMap<QString, ParsedTx> &txs, const QMap<QString, int> &heights,
                               const QMap<QByteArray, AddressRef> &walletScripts);

}

#endif // BISCUIT_ELECTRUM_H
