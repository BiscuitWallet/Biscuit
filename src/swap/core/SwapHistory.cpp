// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapHistory.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>

namespace biscuit::swap {

SwapHistory SwapHistory::fromJson(const QByteArray &json) {
    SwapHistory history;
    const QJsonArray array = QJsonDocument::fromJson(json).object().value("trades").toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        if (const auto trade = Trade::fromJson(obj)) {
            history.m_trades.append(*trade);
        } else {
            history.m_unreadable.append(obj);
        }
    }
    return history;
}

QByteArray SwapHistory::toJson() const {
    QJsonArray array;
    for (const Trade &trade : m_trades) {
        array.append(trade.toJson());
    }
    for (const QJsonObject &obj : m_unreadable) {
        array.append(obj);
    }
    const QJsonObject root{{"version", 1}, {"trades", array}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void SwapHistory::upsert(const Trade &trade) {
    for (Trade &existing : m_trades) {
        if (existing.providerId == trade.providerId && existing.tradeId == trade.tradeId) {
            existing = trade;
            return;
        }
    }
    m_trades.append(trade);
}

const Trade *SwapHistory::find(const QString &providerId, const QString &tradeId) const {
    for (const Trade &trade : m_trades) {
        if (trade.providerId == providerId && trade.tradeId == tradeId) {
            return &trade;
        }
    }
    return nullptr;
}

QList<Trade> SwapHistory::trades() const {
    QList<Trade> sorted = m_trades;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Trade &a, const Trade &b) {
        return a.createdAt > b.createdAt;
    });
    return sorted;
}

QList<Trade> SwapHistory::tradesToPoll(const QDateTime &now) const {
    QList<Trade> result;
    for (const Trade &trade : m_trades) {
        if (isTerminal(trade.status)) {
            continue;
        }
        if (trade.createdAt.isValid() && trade.createdAt.daysTo(now) > pollingLimitDays) {
            continue;
        }
        result.append(trade);
    }
    return result;
}

}
