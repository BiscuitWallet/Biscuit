// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_SWAP_TROCADORAPI_H
#define BISCUIT_SWAP_TROCADORAPI_H

#include <optional>

#include <QByteArray>
#include <QList>
#include <QUrlQuery>

#include "SwapTypes.h"

// Pure (network-free) part of the Trocador integration: query building and
// response parsing. Reference: https://trocador.app/en/docs/
//
// Field names were cross-checked with Cake Wallet's open-source integration
// while the official docs were unreachable. Fields marked "to verify" must be
// checked against the docs before the first real trade.
namespace biscuit::swap::trocador {

    inline constexpr char providerId[] = "trocador";
    inline constexpr char apiBaseUrl[] = "https://trocador.app/api/";

    // Biscuit never adds a markup: the user pays the same price as on
    // Trocador, and Trocador shares its own commission with the app.
    inline constexpr char markup[] = "0";

    QUrlQuery rateQuery(const QuoteRequest &request);
    QUrlQuery newTradeQuery(const TradeRequest &request, const QString &rateId);
    QUrlQuery tradeQuery(const QString &tradeId);
    QUrlQuery validateAddressQuery(const Asset &asset, const QString &address);

    struct RateResult {
        QString rateId;           // Trocador "trade_id" of the rate, used by new_trade
        QList<Quote> quotes;      // in API order, rank with rankQuotes()
    };

    // Each function returns std::nullopt and fills `error` on failure.
    std::optional<QList<AssetInfo>> parseCoins(const QByteArray &body, QString *error);
    std::optional<RateResult> parseRate(const QByteArray &body, const QuoteRequest &request, QString *error);

    // Parses new_trade (object) and trade (list with one object) responses.
    std::optional<Trade> parseTrade(const QByteArray &body, QString *error);

    // validateaddress answers {"result": true|false} (to verify).
    std::optional<bool> parseValidateAddress(const QByteArray &body, QString *error);

    // Extracts {"error": ..., "message": ...} or returns an empty string.
    QString parseErrorMessage(const QByteArray &body);
}

#endif // BISCUIT_SWAP_TROCADORAPI_H
