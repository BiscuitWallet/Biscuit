// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "EthApi.h"

#include <algorithm>

namespace biscuit::coins::eth {

namespace {
    void fail(QString *error, const QString &message) {
        if (error) *error = message;
    }

    // "0x" + exactly `size` bytes of hex; empty if anything else.
    QByteArray hexBytes(const QJsonValue &value, int size) {
        const QString s = value.toString();
        if (!s.startsWith("0x") || s.size() != 2 + 2 * size) {
            return {};
        }
        const QByteArray b = QByteArray::fromHex(s.mid(2).toLatin1());
        return b.size() == size && b.toHex() == s.mid(2).toLower().toLatin1() ? b : QByteArray();
    }

    QByteArray addressOf(const QJsonValue &party) {   // {"hash": "0x…", …} or null
        return hexBytes(party.toObject().value("hash"), 20);
    }

    std::optional<u128> decimal(const QJsonValue &value) {   // "1650…", as Blockscout sends amounts
        const QString s = value.toString();
        if (s.isEmpty() || s.size() > 39 || s.contains('.')) {
            return std::nullopt;
        }
        return parseAmount(s, 0);
    }

    // Block and time: both missing while the transaction is pending.
    bool readBlock(const QJsonObject &item, HistoryEntry &entry) {
        const QJsonValue block = item.value("block_number");
        if (block.isNull() || block.isUndefined()) {
            return true;
        }
        if (!block.isDouble() || block.toDouble() < 1) {
            return false;
        }
        entry.block = quint64(block.toDouble());
        entry.time = QDateTime::fromString(item.value("timestamp").toString(), Qt::ISODateWithMs);
        return entry.time.isValid();
    }
}

const QStringList &defaultNodes() {
    static const QStringList list{
        "https://ethereum-rpc.publicnode.com",
        "https://eth.drpc.org",
        "https://rpc.mevblocker.io",
        "https://rpc.flashbots.net",
    };
    return list;
}

QString defaultBlockscout() {
    return "https://eth.blockscout.com";
}

// ---------------------------------------------------------------- JSON-RPC

namespace rpc {

QJsonObject request(int id, const QString &method, const QJsonArray &params) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

std::optional<QJsonValue> result(const QJsonValue &reply, int id, QString *error) {
    const QJsonObject obj = reply.toObject();
    if (obj.value("id").toInt(-1) != id) {
        fail(error, "Unexpected reply from the Ethereum node");
        return std::nullopt;
    }
    if (obj.contains("error")) {
        const QString message = obj.value("error").toObject().value("message").toString();
        fail(error, message.isEmpty() ? QString("Error from the Ethereum node") : message);
        return std::nullopt;
    }
    if (!obj.contains("result")) {
        fail(error, "Unexpected reply from the Ethereum node");
        return std::nullopt;
    }
    return obj.value("result");
}

std::optional<QJsonValue> batchResult(const QJsonArray &replies, int id, QString *error) {
    for (const QJsonValue &reply : replies) {
        if (reply.toObject().value("id").toInt(-1) == id) {
            return result(reply, id, error);
        }
    }
    fail(error, "Incomplete reply from the Ethereum node");
    return std::nullopt;
}

}

std::optional<Fees> parseFeeHistory(const QJsonValue &result) {
    const QJsonArray bases = result.toObject().value("baseFeePerGas").toArray();
    const QJsonArray rewards = result.toObject().value("reward").toArray();
    if (bases.isEmpty()) {
        return std::nullopt;
    }
    Fees fees;
    // The last base fee is the next block's.
    const auto base = parseQuantity(bases.last().toString());
    if (!base) {
        return std::nullopt;
    }
    fees.baseFee = *base;
    QList<u128> tips;
    for (const QJsonValue &r : rewards) {
        const auto tip = parseQuantity(r.toArray().first().toString());
        if (!tip) {
            return std::nullopt;
        }
        tips << *tip;
    }
    std::sort(tips.begin(), tips.end());
    fees.priorityFee = std::max(tips.isEmpty() ? u128(0) : tips.at(tips.size() / 2), minimumPriorityFee);
    fees.maxFeePerGas = 2 * fees.baseFee + fees.priorityFee;
    return fees;
}

// --------------------------------------------------------------- Blockscout

namespace blockscout {

QString transactionsPath(const QByteArray &address) {
    return QString("/api/v2/addresses/%1/transactions").arg(checksumAddress(address));
}

QString tokenTransfersPath(const QByteArray &address, const Token &token) {
    return QString("/api/v2/addresses/%1/token-transfers?type=ERC-20&token=%2")
            .arg(checksumAddress(address), checksumAddress(token.contract));
}

std::optional<QList<HistoryEntry>> parseTransactions(const QJsonValue &reply, const QByteArray &address,
                                                     QHash<QByteArray, u128> *fees) {
    if (!reply.toObject().value("items").isArray()) {
        return std::nullopt;
    }
    QList<HistoryEntry> list;
    for (const QJsonValue &v : reply.toObject().value("items").toArray()) {
        const QJsonObject item = v.toObject();
        HistoryEntry e;
        e.hash = hexBytes(item.value("hash"), 32);
        const QByteArray from = addressOf(item.value("from"));
        const QByteArray to = addressOf(item.value("to"));   // empty: contract creation
        const auto value = decimal(item.value("value"));
        if (e.hash.isEmpty() || from.isEmpty() || !value || !readBlock(item, e)) {
            return std::nullopt;   // not what Blockscout sends: refuse the whole page
        }
        if (from != address && to != address) {
            continue;
        }
        e.incoming = from != address;
        e.counterparty = e.incoming ? from : to;
        e.amount = *value;
        e.failed = item.value("status").toString() == "error";
        if (e.incoming && (e.amount == 0 || e.failed)) {
            continue;   // nothing received
        }
        if (!e.incoming) {
            e.fee = decimal(item.value("fee").toObject().value("value")).value_or(0);
        }
        // A call to USDT/USDC: the transfer comes from /token-transfers, which
        // only lists the ones that went through. A failed one is shown here.
        const Token *token = tokenByContract(to);
        if (!e.incoming && e.amount == 0 && token) {
            if (fees) fees->insert(e.hash, e.fee);
            const auto transfer = decodeErc20Transfer(hexBytes(item.value("raw_input"), 68));
            if (!e.failed || !transfer) {
                continue;
            }
            e.asset = token->symbol;
            e.counterparty = transfer->first;
            e.amount = transfer->second;
        } else {
            e.asset = "ETH";
        }
        list << e;
    }
    return list;
}

std::optional<QList<HistoryEntry>> parseTokenTransfers(const QJsonValue &reply, const QByteArray &address,
                                                       const Token &token) {
    if (!reply.toObject().value("items").isArray()) {
        return std::nullopt;
    }
    QList<HistoryEntry> list;
    for (const QJsonValue &v : reply.toObject().value("items").toArray()) {
        const QJsonObject item = v.toObject();
        const QJsonObject info = item.value("token").toObject();
        if (hexBytes(info.value("address_hash"), 20) != token.contract) {
            continue;   // another token, whatever its name
        }
        HistoryEntry e;
        e.hash = hexBytes(item.value("transaction_hash"), 32);
        const QByteArray from = addressOf(item.value("from"));
        const QByteArray to = addressOf(item.value("to"));
        const QJsonObject total = item.value("total").toObject();
        const auto value = decimal(total.value("value"));
        if (e.hash.isEmpty() || from.isEmpty() || to.isEmpty() || !value || !readBlock(item, e)
            || total.value("decimals").toString() != QString::number(token.decimals)) {
            return std::nullopt;
        }
        if (from != address && to != address) {
            continue;
        }
        e.asset = token.symbol;
        e.incoming = from != address;
        e.counterparty = e.incoming ? from : to;
        e.amount = *value;
        list << e;
    }
    return list;
}

}

}
