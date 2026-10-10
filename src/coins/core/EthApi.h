// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ETHAPI_H
#define BISCUIT_ETHAPI_H

#include <optional>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "Eth.h"

// What Biscuit asks of Ethereum nodes (JSON-RPC) and of Blockscout (history),
// and how it reads the answers. No network here: the wallet sends the
// requests; this code builds them and checks every field of the replies.
namespace biscuit::coins::eth {

// Public nodes that answer without an account. MEV Blocker and Flashbots
// also broadcast without going through the public mempool.
const QStringList &defaultNodes();
// Open source explorer used for the history only (never for the balance,
// the nonce or the fees). Anyone can run their own instance.
QString defaultBlockscout();

// ---------------------------------------------------------------- JSON-RPC

namespace rpc {
    QJsonObject request(int id, const QString &method, const QJsonArray &params = {});
    // The "result" of the reply to request `id`, or the node's error.
    std::optional<QJsonValue> result(const QJsonValue &reply, int id, QString *error = nullptr);
    // Same, for one request of a batch (an array of replies, in any order).
    std::optional<QJsonValue> batchResult(const QJsonArray &replies, int id, QString *error = nullptr);
}

// Fees of the next block, from eth_feeHistory(5 blocks, "latest", [percentile]).
struct Fees {
    u128 baseFee = 0;          // of the next block
    u128 priorityFee = 0;      // tip: median over the recent blocks of the percentile asked
    u128 maxFeePerGas = 0;     // 2 x base fee + tip: still enough after 6 full blocks
};
std::optional<Fees> parseFeeHistory(const QJsonValue &result);
// Never below this tip (0.01 gwei): some blocks report no tips at all.
constexpr u128 minimumPriorityFee = 10000000;

// --------------------------------------------------------------- Blockscout

struct HistoryEntry {
    QByteArray hash;            // 32 bytes
    QString asset;              // "ETH", "USDT", "USDC"
    bool incoming = false;
    u128 amount = 0;            // in the asset's units
    u128 fee = 0;               // ETH paid by us (outgoing only)
    QByteArray counterparty;    // 20 bytes: sender if incoming, recipient if outgoing
    QDateTime time;             // invalid while pending
    quint64 block = 0;          // 0 while pending
    bool failed = false;
};

namespace blockscout {
    QString transactionsPath(const QByteArray &address);
    QString tokenTransfersPath(const QByteArray &address, const Token &token);

    // ETH sent or received by `address` (/transactions). A transaction that
    // only calls a token contract carries no ETH: its fee is kept in `fees`
    // (hash -> fee) for the matching token transfer.
    std::optional<QList<HistoryEntry>> parseTransactions(const QJsonValue &reply, const QByteArray &address,
                                                         QHash<QByteArray, u128> *fees = nullptr);
    // The query of the next, older page ("block_number=…&index=…"), from the
    // reply's next_page_params; empty on the last page.
    QString nextPageQuery(const QJsonValue &reply);
    // `path` with that query appended.
    QString withQuery(const QString &path, const QString &query);

    // Transfers of `token` (/token-transfers). Anything from another contract,
    // however it is named, is ignored: fake USDT never shows up.
    std::optional<QList<HistoryEntry>> parseTokenTransfers(const QJsonValue &reply, const QByteArray &address,
                                                           const Token &token);
}

}

#endif // BISCUIT_ETHAPI_H
