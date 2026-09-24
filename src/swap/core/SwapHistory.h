// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_SWAPHISTORY_H
#define BISCUIT_SWAP_SWAPHISTORY_H

#include <QByteArray>
#include <QDateTime>
#include <QList>

#include "SwapTypes.h"

namespace biscuit::swap {

// Local list of swaps. It is the only durable record of a trade: aggregators
// delete trades after a while (14 days for Trocador).
//
// Stored inside the Monero wallet cache (encrypted by wallet2 with the wallet
// password), under the attribute below. To be migrated to the multi-coin
// vault in phase 2.
class SwapHistory {
public:
    static constexpr char walletAttribute[] = "biscuit.swaps";

    // Aggregators forget trades after this delay: stop polling afterwards.
    static constexpr qint64 pollingLimitDays = 14;

    // Unreadable entries are skipped, never dropped silently from storage:
    // `unreadable` keeps them so they are written back unchanged.
    static SwapHistory fromJson(const QByteArray &json);
    QByteArray toJson() const;

    // Inserts or replaces the trade with the same provider and trade id.
    void upsert(const Trade &trade);
    const Trade *find(const QString &providerId, const QString &tradeId) const;

    // Newest first.
    QList<Trade> trades() const;

    // Trades whose status may still change and that are recent enough for the
    // aggregator to still know them.
    QList<Trade> tradesToPoll(const QDateTime &now) const;

    qsizetype size() const { return m_trades.size(); }

private:
    QList<Trade> m_trades;
    QList<QJsonObject> m_unreadable;
};

}

#endif // BISCUIT_SWAP_SWAPHISTORY_H
