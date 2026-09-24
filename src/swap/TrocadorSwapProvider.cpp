// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "TrocadorSwapProvider.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include "RequestPolicy.h"
#include "TrocadorApi.h"
#include "utils/NetworkManager.h"
#include "utils/config.h"

#ifdef BISCUIT_HAS_TROCADOR_KEY
#include "BiscuitSecrets.h"
#endif

namespace biscuit::swap {

namespace {
    // Shared by every wallet window: the limit is per installation
    // (6 requests per minute, as agreed with Trocador).
    policy::RateLimiter &limiter() {
        static policy::RateLimiter l(6, 60 * 1000);
        return l;
    }

    bool &keyRejectedFlag() {
        static bool rejected = false;
        return rejected;
    }

    // Certificate pinning: trocador.app is served by Let's Encrypt, so only the
    // ISRG roots are trusted for it, not every CA installed on the computer
    // (blocks interception through an added root, e.g. mitmproxy). If Trocador
    // changes CA, swaps fail with a TLS error until an update adds it.
    QSslConfiguration pinnedTls() {
        static const QSslConfiguration config = [] {
            QList<QSslCertificate> roots;
            for (const char *path : {":/assets/certs/isrg-root-x1.pem", ":/assets/certs/isrg-root-x2.pem"}) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    roots += QSslCertificate::fromData(f.readAll(), QSsl::Pem);
                }
            }
            QSslConfiguration c = QSslConfiguration::defaultConfiguration();
            c.setCaCertificates(roots);
            c.setPeerVerifyMode(QSslSocket::VerifyPeer);
            return c;
        }();
        return config;
    }

    bool isKeyError(int status, const QByteArray &body) {
        if (status == 401 || status == 403) return true;
        const QString error = trocador::parseErrorMessage(body).toLower();
        return error.contains("api key");
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
#ifdef BISCUIT_HAS_TROCADOR_KEY
    QByteArray key(trocador_key_len, Qt::Uninitialized);
    for (unsigned long i = 0; i < trocador_key_len; ++i) {
        key[i] = char(trocador_key_masked[i] ^ trocador_key_pad[i]);
    }
    return QString::fromLatin1(key);
#else
    return {};
#endif
}

bool TrocadorSwapProvider::keyRejected() {
    return keyRejectedFlag();
}

bool TrocadorSwapProvider::proxyActive() {
    return conf()->get(Config::proxy).toInt() != Config::Proxy::None;
}

bool TrocadorSwapProvider::directConnectionAllowed() {
    return !proxyActive() || conf()->get(Config::swapDirectConsent).toBool();
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

void TrocadorSwapProvider::get(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt) {
    // Over the limit, requests are delayed, never sent in bursts.
    const qint64 delay = limiter().reserve(QDateTime::currentMSecsSinceEpoch());
    if (delay > 0) {
        QTimer::singleShot(delay, this, [this, method, query, handler, attempt] { send(method, query, handler, attempt); });
        return;
    }
    send(method, query, handler, attempt);
}

void TrocadorSwapProvider::send(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt) {
    if (conf()->get(Config::offlineMode).toBool()) {
        handler({}, "Offline mode is enabled");
        return;
    }
    if (!directConnectionAllowed()) {
        handler({}, "Swaps connect to Trocador without Tor. Allow it in the Swap tab first.");
        return;
    }
    if (keyRejectedFlag()) {
        handler({}, "Swaps are unavailable in this version of Biscuit. Please update Biscuit.");
        return;
    }

    QUrl url(QString(trocador::apiBaseUrl) + method);
    url.setQuery(query);
    QNetworkRequest request(url);
    // Same headers for every user: nothing about the system or its language.
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; rv:102.0) Gecko/20100101 Firefox/102.0");
    request.setRawHeader("Accept-Language", "en");
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("API-Key", m_apiKey.toUtf8());   // never logged
    request.setSslConfiguration(pinnedTls());

    QNetworkReply *reply = getNetworkClearnet()->get(request);
    reply->setParent(this);

    connect(reply, &QNetworkReply::finished, this, [this, reply, method, query, handler, attempt] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            // Rate limited: back off (1, 2, 4 minutes), never retry at once.
            if (attempt < 3) {
                QTimer::singleShot((60 << attempt) * 1000, this, [this, method, query, handler, attempt] {
                    get(method, query, handler, attempt + 1);
                });
                return;
            }
            handler({}, "Trocador is busy (too many requests). Try again in a few minutes.");
            return;
        }
        const QByteArray body = reply->readAll();
        if (isKeyError(status, body)) {
            // Rejected key: switch swaps off cleanly, never hammer the API.
            keyRejectedFlag() = true;
            handler({}, "Swaps are unavailable in this version of Biscuit. Please update Biscuit.");
            return;
        }
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
