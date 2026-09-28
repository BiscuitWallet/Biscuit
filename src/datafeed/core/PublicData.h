// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PUBLICDATA_H
#define BISCUIT_PUBLICDATA_H

#include <optional>

#include <QByteArray>
#include <QJsonObject>

// Public data sources replacing the Feather websocket service: prices, fiat
// rates and crowdfunding proposals, fetched directly from public APIs, plus
// Biscuit's own news feed.
//
// Privacy rules:
//  - every Biscuit sends exactly the same requests (fixed lists, nothing that
//    depends on the user's settings, wallet or balance);
//  - no API key, no cookie, no identifier;
//  - requests go through utils/Networking, so the Tor setting applies.
//
// The functions below convert API answers into the message format of the
// Feather websocket ({"cmd": ..., "data": ...}), so existing features (fiat
// conversion, Tickers, Calc, Home) work unchanged.
namespace biscuit::datafeed {

    // CoinGecko markets, fixed coin list.
    QString cryptoRatesUrl();
    // European Central Bank reference rates via Frankfurter, base USD.
    QString fiatRatesUrl();
    // Monero Community Crowdfunding System.
    QString crowdfundingUrl();
    // Biscuit news, Atom feed from biscuitwallet.com.
    QString newsUrl();

    std::optional<QJsonObject> cryptoRatesMessage(const QByteArray &body);
    std::optional<QJsonObject> fiatRatesMessage(const QByteArray &body);
    std::optional<QJsonObject> crowdfundingMessage(const QByteArray &body);
    // {"cmd": "news", "data": [{"title", "url", "date": "yyyy-MM-dd", "summary"}]},
    // newest first, plain text only, links limited to biscuitwallet.com.
    std::optional<QJsonObject> newsMessage(const QByteArray &body);
}

#endif // BISCUIT_PUBLICDATA_H
