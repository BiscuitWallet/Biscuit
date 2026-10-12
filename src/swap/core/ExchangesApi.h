// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_EXCHANGESAPI_H
#define BISCUIT_SWAP_EXCHANGESAPI_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QUrlQuery>

#include "SwapTypes.h"

// Pure (network-free) part of Biscuit's own exchanges: the relay's /api/x/
// routes (biscuit-relay, exchanges.go). The relay calls exchanges that never
// ask for KYC and accept swaps created through Tor, with keys kept on the
// server; amounts come back as plain decimals cut to each coin's decimals.
namespace biscuit::swap::exchanges {

    inline constexpr char providerId[] = "exchanges";

    QUrlQuery quotesQuery(const QuoteRequest &request);
    QUrlQuery newTradeQuery(const TradeRequest &request);
    // Status needs the payout address: the relay answers nobody else.
    QUrlQuery tradeQuery(const Trade &trade);

    struct QuotesResult {
        QList<Quote> quotes;
        QStringList errors;   // one per exchange, "PegasusSwap: the minimum is…"
    };

    // Each function returns std::nullopt and fills `error` on failure.
    std::optional<QList<AssetInfo>> parseAssets(const QByteArray &body, QString *error);
    std::optional<QuotesResult> parseQuotes(const QByteArray &body, const QuoteRequest &request, QString *error);
    std::optional<Trade> parseTrade(const QByteArray &body, QString *error);

    // {"error": ..., "message": ...} from the relay, or an empty string.
    QString parseErrorMessage(const QByteArray &body);
}

#endif // BISCUIT_SWAP_EXCHANGESAPI_H
