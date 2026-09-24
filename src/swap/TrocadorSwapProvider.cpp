// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "TrocadorSwapProvider.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

#include "RequestPolicy.h"
#include "TrocadorApi.h"
#include "utils/Networking.h"

namespace biscuit::swap {

namespace {
    // Shared by every wallet window: the limit is per installation.
    policy::RateLimiter &limiter() {
        static policy::RateLimiter l(10, 60 * 1000);
        return l;
    }

    // The coin list changes rarely: fetched at most once a day.
    struct CoinCache {
        QList<AssetInfo> assets;
        QDateTime fetchedAt;
    };
    CoinCache &coinCache() {
        static CoinCache c;
        return c;
    }
}

TrocadorSwapProvider::TrocadorSwapProvider(const QString &apiKey, QObject *parent)
    : SwapProvider(parent)
    , m_apiKey(apiKey)
{
}

QString TrocadorSwapProvider::builtInApiKey() {
#ifdef BISCUIT_TROCADOR_API_KEY
    return QStringLiteral(BISCUIT_TROCADOR_API_KEY);
#else
    return {};
#endif
}

QString TrocadorSwapProvider::id() const {
    return trocador::providerId;
}

QString TrocadorSwapProvider::displayName() const {
    return "Trocador";
}

QString TrocadorSwapProvider::kycPolicy() const {
    return "Aggregator. Each exchange has a KYC rating from A (no KYC) to D (funds may be held).";
}

QString TrocadorSwapProvider::supportUrl() const {
    return "https://trocador.app/en/contact/";
}

void TrocadorSwapProvider::get(const QString &method, const QUrlQuery &query, ReplyHandler handler) {
    // Over the limit, requests are delayed, never sent in bursts.
    const qint64 delay = limiter().reserve(QDateTime::currentMSecsSinceEpoch());
    if (delay > 0) {
        QTimer::singleShot(delay, this, [this, method, query, handler] { send(method, query, handler); });
        return;
    }
    send(method, query, handler);
}

void TrocadorSwapProvider::send(const QString &method, const QUrlQuery &query, ReplyHandler handler) {
    QUrl url(QString(trocador::apiBaseUrl) + method);
    url.setQuery(query);

    Networking network{this};
    QNetworkReply *reply = network.getJson(this, url.toString(), {{"API-Key", m_apiKey.toUtf8()}});
    if (!reply) {
        handler({}, "Offline mode is enabled");
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [reply, handler] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        // Trocador answers errors with a JSON body and a 4xx status: let the
        // parsers read it, only report the network error if there is no body.
        const QString networkError = body.isEmpty() && reply->error() != QNetworkReply::NoError
                                     ? reply->errorString() : QString();
        handler(body, networkError);
    });
}

void TrocadorSwapProvider::supportedAssets(AssetsCallback callback) {
    const CoinCache &cache = coinCache();
    if (!cache.assets.isEmpty() && cache.fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600) {
        callback(cache.assets, {});
        return;
    }
    get("coins", {}, [callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback({}, networkError);
            return;
        }
        QString error;
        const auto coins = trocador::parseCoins(body, &error);
        if (coins) {
            coinCache() = {*coins, QDateTime::currentDateTimeUtc()};
        }
        callback(coins.value_or(QList<AssetInfo>{}), error);
    });
}

void TrocadorSwapProvider::getQuotes(const QuoteRequest &request, QuotesCallback callback) {
    get("new_rate", trocador::rateQuery(request), [request, callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback({}, networkError);
            return;
        }
        QString error;
        const auto result = trocador::parseRate(body, request, &error);
        callback(result ? result->quotes : QList<Quote>{}, error);
    });
}

void TrocadorSwapProvider::createTrade(const TradeRequest &request, TradeCallback callback) {
    const QUrlQuery query = trocador::newTradeQuery(request, request.quote.rateId);
    get("new_trade", query, [this, callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback(std::nullopt, networkError);
            return;
        }
        QString error;
        auto trade = trocador::parseTrade(body, &error);
        if (trade) {
            trade->supportUrl = supportUrl();
        }
        callback(trade, error);
    });
}

void TrocadorSwapProvider::getTradeStatus(const Trade &trade, TradeCallback callback) {
    get("trade", trocador::tradeQuery(trade.tradeId), [this, callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback(std::nullopt, networkError);
            return;
        }
        QString error;
        auto updated = trocador::parseTrade(body, &error);
        if (updated) {
            updated->supportUrl = supportUrl();
        }
        callback(updated, error);
    });
}

void TrocadorSwapProvider::validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) {
    get("validateaddress", trocador::validateAddressQuery(asset, address),
        [callback](const QByteArray &body, const QString &networkError) {
            if (!networkError.isEmpty()) {
                callback(std::nullopt, networkError);
                return;
            }
            QString error;
            callback(trocador::parseValidateAddress(body, &error), error);
        });
}

}
