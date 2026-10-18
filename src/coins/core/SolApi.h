// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SOLAPI_H
#define BISCUIT_SOLAPI_H

#include <optional>

#include <QDateTime>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include "Sol.h"

// What Biscuit asks of Solana nodes (JSON-RPC, the same requests as
// Ethereum's: eth::rpc) and how it reads the answers. No network here: every
// field of the replies is checked. The history comes from the nodes too
// (getSignaturesForAddress, getTransaction): no explorer.
namespace biscuit::coins::sol {

// Public nodes that answer without an account: the Solana Foundation's and
// PublicNode (checked on 2026-10-18; drpc, Ankr and OnFinality need a key).
const QStringList &defaultNodes();
// Where a transaction can be seen ("…/tx/<signature>").
inline const QString explorer = QStringLiteral("https://explorer.solana.com");

// getBalance: {"context": …, "value": lamports}.
std::optional<quint64> parseBalance(const QJsonValue &result);

struct Blockhash {
    QByteArray hash;                  // 32 bytes
    quint64 lastValidBlockHeight = 0; // the transaction expires after it
};
// getLatestBlockhash: {"context": …, "value": {"blockhash", "lastValidBlockHeight"}}.
std::optional<Blockhash> parseLatestBlockhash(const QJsonValue &result);

// Priority fee per compute unit (micro-lamports) from
// getRecentPrioritizationFees: the 75th percentile of the recent slots, kept
// between these bounds (at most 5000 lamports for a whole transfer).
constexpr quint64 minimumPriorityFee = 10000;
constexpr quint64 maximumPriorityFee = 5000000;
std::optional<quint64> parsePriorityFee(const QJsonValue &result);

// getSignaturesForAddress: newest first.
struct SignatureInfo {
    QString signature;   // base58, the transaction id
    quint64 slot = 0;
    QDateTime time;      // invalid if the node does not know it
    bool failed = false;
};
std::optional<QList<SignatureInfo>> parseSignatures(const QJsonValue &result);

struct HistoryEntry {
    QString signature;
    bool incoming = false;
    quint64 amount = 0;          // lamports received, or sent (fee apart)
    quint64 fee = 0;             // paid by us (outgoing only)
    QByteArray counterparty;     // 32 bytes if one SOL transfer names it, else empty
    QDateTime time;              // invalid while pending
    quint64 slot = 0;            // 0 while pending
    bool failed = false;
};
// getTransaction (jsonParsed): what it changed for `address`, from the
// balances before and after. std::nullopt if malformed; an entry with
// nothing for us (amount 0, not paid by us) has `relevant` false.
struct ParsedTransaction {
    HistoryEntry entry;
    bool relevant = false;
};
std::optional<ParsedTransaction> parseTransaction(const QJsonValue &result, const QByteArray &address);

// getSignatureStatuses for one signature: not seen (std::nullopt), or
// seen ("confirmed" or "finalized"), failed or not.
struct Status {
    bool confirmed = false;
    bool failed = false;
};
std::optional<std::optional<Status>> parseSignatureStatus(const QJsonValue &result);

}

#endif // BISCUIT_SOLAPI_H
