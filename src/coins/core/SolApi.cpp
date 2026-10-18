// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SolApi.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>

namespace biscuit::coins::sol {

namespace {
    // A JSON integer that is exactly one (JSON numbers are doubles: exact up to 2^53).
    std::optional<quint64> integer(const QJsonValue &v) {
        if (!v.isDouble()) {
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (d < 0 || d > 9007199254740992.0 || d != double(quint64(d))) {
            return std::nullopt;
        }
        return quint64(d);
    }

    QDateTime blockTime(const QJsonValue &v) {
        const auto t = integer(v);
        return t ? QDateTime::fromSecsSinceEpoch(qint64(*t), QTimeZone::UTC) : QDateTime();
    }

    bool isSignature(const QString &s) {
        const auto b = fromBase58(s);
        return b && b->size() == 64;
    }
}

const QStringList &defaultNodes() {
    static const QStringList list{"https://api.mainnet-beta.solana.com", "https://solana-rpc.publicnode.com"};
    return list;
}

std::optional<quint64> parseBalance(const QJsonValue &result) {
    return integer(result.toObject().value("value"));
}

std::optional<Blockhash> parseLatestBlockhash(const QJsonValue &result) {
    const QJsonObject value = result.toObject().value("value").toObject();
    const auto hash = fromBase58(value.value("blockhash").toString());
    const auto height = integer(value.value("lastValidBlockHeight"));
    if (!hash || hash->size() != 32 || !height) {
        return std::nullopt;
    }
    return Blockhash{*hash, *height};
}

std::optional<quint64> parsePriorityFee(const QJsonValue &result) {
    if (!result.isArray()) {
        return std::nullopt;
    }
    QList<quint64> fees;
    for (const QJsonValue &v : result.toArray()) {
        const auto fee = integer(v.toObject().value("prioritizationFee"));
        if (!fee) {
            return std::nullopt;
        }
        fees << *fee;
    }
    if (fees.isEmpty()) {
        return minimumPriorityFee;
    }
    std::sort(fees.begin(), fees.end());
    const quint64 p75 = fees.at(std::min<qsizetype>(fees.size() * 3 / 4, fees.size() - 1));
    return std::clamp(p75, minimumPriorityFee, maximumPriorityFee);
}

std::optional<QList<SignatureInfo>> parseSignatures(const QJsonValue &result) {
    if (!result.isArray()) {
        return std::nullopt;
    }
    QList<SignatureInfo> list;
    for (const QJsonValue &v : result.toArray()) {
        const QJsonObject o = v.toObject();
        SignatureInfo s;
        s.signature = o.value("signature").toString();
        const auto slot = integer(o.value("slot"));
        if (!isSignature(s.signature) || !slot) {
            return std::nullopt;
        }
        s.slot = *slot;
        s.time = blockTime(o.value("blockTime"));
        s.failed = !o.value("err").isNull() && !o.value("err").isUndefined();
        list << s;
    }
    return list;
}

std::optional<ParsedTransaction> parseTransaction(const QJsonValue &result, const QByteArray &address) {
    const QJsonObject root = result.toObject();
    const QJsonObject meta = root.value("meta").toObject();
    const QJsonObject message = root.value("transaction").toObject().value("message").toObject();
    const QJsonArray keys = message.value("accountKeys").toArray();
    const QJsonArray signatures = root.value("transaction").toObject().value("signatures").toArray();
    const QJsonArray pre = meta.value("preBalances").toArray();
    const QJsonArray post = meta.value("postBalances").toArray();
    const auto slot = integer(root.value("slot"));
    const auto fee = integer(meta.value("fee"));
    if (keys.isEmpty() || signatures.isEmpty() || !isSignature(signatures.at(0).toString()) || !slot || !fee
        || pre.size() < keys.size() || post.size() < keys.size()) {
        return std::nullopt;
    }
    const QString me = addressText(address);
    int index = -1;
    for (int i = 0; i < keys.size(); ++i) {
        // jsonParsed: {"pubkey", "signer", "writable", …}.
        if (keys.at(i).toObject().value("pubkey").toString() == me) {
            index = i;
            break;
        }
    }
    ParsedTransaction out;
    HistoryEntry &e = out.entry;
    e.signature = signatures.at(0).toString();
    e.slot = *slot;
    e.time = blockTime(root.value("blockTime"));
    e.failed = !meta.value("err").isNull() && !meta.value("err").isUndefined();
    if (index < 0) {
        return out;   // not about us (or only through a token account)
    }
    const auto before = integer(pre.at(index)), after = integer(post.at(index));
    if (!before || !after) {
        return std::nullopt;
    }
    // Index 0 is the fee payer: our send (or anything we signed and paid).
    const bool paid = index == 0;
    if (paid) {
        const quint64 spent = *before > *after ? *before - *after : 0;
        e.fee = *fee;
        e.incoming = *after > *before;
        e.amount = e.incoming ? *after - *before : (spent > *fee ? spent - *fee : 0);
    } else {
        e.incoming = *after >= *before;
        e.amount = e.incoming ? *after - *before : *before - *after;
    }
    // The other side, if one plain SOL transfer involves us.
    const QJsonArray instructions = message.value("instructions").toArray();
    for (const QJsonValue &v : instructions) {
        const QJsonObject ix = v.toObject();
        const QJsonObject parsed = ix.value("parsed").toObject();
        if (ix.value("program").toString() != "system" || parsed.value("type").toString() != "transfer") {
            continue;
        }
        const QJsonObject info = parsed.value("info").toObject();
        const QString source = info.value("source").toString(), destination = info.value("destination").toString();
        const QString other = source == me ? destination : destination == me ? source : QString();
        if (const auto key = other.isEmpty() ? std::nullopt : parseAddress(other)) {
            e.counterparty = *key;
            break;
        }
    }
    out.relevant = e.amount > 0 || paid;
    return out;
}

std::optional<std::optional<Status>> parseSignatureStatus(const QJsonValue &result) {
    const QJsonArray value = result.toObject().value("value").toArray();
    if (value.size() != 1) {
        return std::nullopt;
    }
    if (value.at(0).isNull()) {
        return std::optional<Status>();
    }
    const QJsonObject o = value.at(0).toObject();
    const QString level = o.value("confirmationStatus").toString();
    Status s;
    s.confirmed = level == "confirmed" || level == "finalized";
    s.failed = !o.value("err").isNull() && !o.value("err").isUndefined();
    return std::optional<Status>(s);
}

}
