// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "ExchangesSwapProvider.h"

#include <QDateTime>
#include <QFile>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include "ExchangesApi.h"
#include "RequestPolicy.h"
#include "coins/core/Addresses.h"
#include "coins/core/CoinParams.h"
#include "coins/core/Eth.h"
#include "constants.h"
#include "datafeed/core/PublicData.h"
#include "libwalletqt/WalletManager.h"
#include "utils/NetworkManager.h"
#include "utils/config.h"

namespace biscuit::swap {

namespace {
    // Same pace as Trocador: at most 6 requests a minute per installation.
    policy::RateLimiter &limiter() {
        static policy::RateLimiter l(6, 60 * 1000);
        return l;
    }

    // The relay is served by Let's Encrypt: only the ISRG roots are trusted
    // (see TrocadorSwapProvider). The onion needs no certificate.
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

    int proxy() {
        return conf()->get(Config::proxy).toInt();
    }

    struct AssetCache {
        QList<AssetInfo> assets;
        QDateTime fetchedAt;
    };
    AssetCache &assetCache() {
        static AssetCache c;
        return c;
    }
}

ExchangesSwapProvider::ExchangesSwapProvider(const QString &baseUrl, QObject *parent)
    : SwapProvider(parent)
    , m_baseUrl(baseUrl)
{
}

QString ExchangesSwapProvider::builtInBaseUrl() {
#ifdef BISCUIT_TROCADOR_RELAY_URL
    return QString(BISCUIT_TROCADOR_RELAY_URL) + "x/";
#else
    return {};
#endif
}

QString ExchangesSwapProvider::id() const {
    return exchanges::providerId;
}

QString ExchangesSwapProvider::displayName() const {
    return "Biscuit";
}

QString ExchangesSwapProvider::kycPolicy() const {
    return "Exchanges that never ask for KYC and accept Tor, called by Biscuit's server.";
}

QString ExchangesSwapProvider::supportUrl() const {
    return {};
}

// Without a proxy, or with Tor (the relay's onion). Not over i2p or another
// SOCKS proxy, which may not reach the onion.
bool ExchangesSwapProvider::available() const {
    return proxy() == Config::Proxy::None || proxy() == Config::Proxy::Tor;
}

QString ExchangesSwapProvider::baseUrl() const {
    if (proxy() == Config::Proxy::Tor && m_baseUrl.startsWith("https://")) {
        return datafeed::onionSiteUrl() + "/api/x/";
    }
    return m_baseUrl;
}

void ExchangesSwapProvider::get(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt) {
    const qint64 delay = limiter().reserve(QDateTime::currentMSecsSinceEpoch());
    if (delay > 0) {
        QTimer::singleShot(delay, this, [this, method, query, handler, attempt] { send(method, query, handler, attempt); });
        return;
    }
    send(method, query, handler, attempt);
}

void ExchangesSwapProvider::send(const QString &method, const QUrlQuery &query, ReplyHandler handler, int attempt) {
    if (conf()->get(Config::offlineMode).toBool()) {
        handler({}, "Offline mode is enabled");
        return;
    }
    if (!available()) {
        handler({}, "Not available with this proxy. Use Tor or no proxy.");
        return;
    }

    QUrl url(baseUrl() + method);
    url.setQuery(query);
    QNetworkRequest request(url);
    // No language, no system: the relay and the exchanges get the swap only.
    request.setRawHeader("User-Agent", "Biscuit");
    if (url.scheme() == "https") {
        request.setSslConfiguration(pinnedTls());
    }
    // In Tor mode the onion goes through Tor; otherwise straight to the relay.
    QNetworkAccessManager *network = proxy() == Config::Proxy::Tor ? getNetwork(url.toString()) : getNetworkClearnet();
    QNetworkReply *reply = network->get(request);
    reply->setParent(this);

    connect(reply, &QNetworkReply::finished, this, [this, reply, method, query, handler, attempt] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            if (attempt < 3) {
                QTimer::singleShot((60 << attempt) * 1000, this, [this, method, query, handler, attempt] {
                    get(method, query, handler, attempt + 1);
                });
                return;
            }
            handler({}, "Too many requests. Try again in a few minutes.");
            return;
        }
        const QByteArray body = reply->readAll();
        const QString networkError = body.isEmpty() && reply->error() != QNetworkReply::NoError
                                     ? reply->errorString() : QString();
        handler(body, networkError);
    });
}

void ExchangesSwapProvider::supportedAssets(AssetsCallback callback) {
    const AssetCache &cache = assetCache();
    if (!cache.assets.isEmpty() && cache.fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600) {
        callback(cache.assets, {});
        return;
    }
    get("assets", {}, [callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback({}, networkError);
            return;
        }
        QString error;
        const auto assets = exchanges::parseAssets(body, &error);
        if (assets) {
            assetCache() = {*assets, QDateTime::currentDateTimeUtc()};
        }
        callback(assets.value_or(QList<AssetInfo>{}), error);
    });
}

void ExchangesSwapProvider::getQuotes(const QuoteRequest &request, QuotesCallback callback) {
    get("quotes", exchanges::quotesQuery(request), [request, callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback({}, networkError);
            return;
        }
        QString error;
        const auto result = exchanges::parseQuotes(body, request, &error);
        if (!result) {
            callback({}, error);
            return;
        }
        // Each exchange's refusal ("the minimum is…") is shown with its name.
        callback(result->quotes, result->quotes.isEmpty() ? result->errors.join("\n") : QString());
    });
}

void ExchangesSwapProvider::createTrade(const TradeRequest &request, TradeCallback callback) {
    get("new_trade", exchanges::newTradeQuery(request), [callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback(std::nullopt, networkError);
            return;
        }
        QString error;
        callback(exchanges::parseTrade(body, &error), error);
    });
}

void ExchangesSwapProvider::getTradeStatus(const Trade &trade, TradeCallback callback) {
    get("trade", exchanges::tradeQuery(trade), [callback](const QByteArray &body, const QString &networkError) {
        if (!networkError.isEmpty()) {
            callback(std::nullopt, networkError);
            return;
        }
        QString error;
        callback(exchanges::parseTrade(body, &error), error);
    });
}

void ExchangesSwapProvider::validateAddress(const Asset &asset, const QString &address, ValidationCallback callback) {
    bool valid = false;
    if (asset.ticker == "xmr") {
        valid = WalletManager::addressValid(address, constants::networkType);
    } else if (asset.ticker == "btc") {
        valid = coins::isValidAddress(address, coins::bitcoin());
    } else if (asset.ticker == "ltc") {
        valid = coins::isValidAddress(address, coins::litecoin());
    } else if (asset.network == "ERC20") {
        valid = coins::eth::parseAddress(address).has_value();
    } else {
        callback(std::nullopt, "Unsupported coin");
        return;
    }
    callback(valid, {});
}

}
