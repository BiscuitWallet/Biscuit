// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PublicData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace biscuit::datafeed {

namespace {
    // Same list for everyone: never derived from the user's settings.
    const QStringList coins = {
        "monero", "bitcoin", "litecoin", "ethereum", "tether", "usd-coin", "bitcoin-cash",
        "dash", "zcash", "dogecoin", "solana", "ripple", "cardano", "tron", "wownero",
    };

    std::optional<QJsonValue> parse(const QByteArray &body) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError) {
            return std::nullopt;
        }
        return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    }

    QJsonObject message(const QString &cmd, const QJsonValue &data) {
        return {{"cmd", cmd}, {"data", data}};
    }
}

QString cryptoRatesUrl() {
    QUrl url("https://api.coingecko.com/api/v3/coins/markets");
    QUrlQuery q;
    q.addQueryItem("vs_currency", "usd");
    q.addQueryItem("ids", coins.join(','));
    q.addQueryItem("price_change_percentage", "24h");
    url.setQuery(q);
    return url.toString();
}

QString fiatRatesUrl() {
    return "https://api.frankfurter.dev/v1/latest?base=USD";
}

QString crowdfundingUrl() {
    return "https://ccs.getmonero.org/index.php/projects";
}

std::optional<QJsonObject> cryptoRatesMessage(const QByteArray &body) {
    const auto value = parse(body);
    if (!value || !value->isArray()) {
        return std::nullopt;
    }

    QJsonArray rates;
    for (const QJsonValue &entry : value->toArray()) {
        const QJsonObject obj = entry.toObject();
        const QString symbol = obj.value("symbol").toString();
        const double price = obj.value("current_price").toDouble();
        if (symbol.isEmpty() || price <= 0) {
            continue;
        }
        // Keep only what the app uses (no image URLs).
        rates.append(QJsonObject{
            {"symbol", symbol},
            {"name", obj.value("name").toString()},
            {"current_price", price},
            {"price_change_percentage_24h", obj.value("price_change_percentage_24h").toDouble()},
        });
    }
    if (rates.isEmpty()) {
        return std::nullopt;
    }
    return message("crypto_rates", rates);
}

std::optional<QJsonObject> fiatRatesMessage(const QByteArray &body) {
    const auto value = parse(body);
    if (!value || !value->isObject()) {
        return std::nullopt;
    }

    QJsonObject rates;
    const QJsonObject source = value->toObject().value("rates").toObject();
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (it.value().toDouble() > 0) {
            rates.insert(it.key(), it.value().toDouble());
        }
    }
    if (rates.isEmpty()) {
        return std::nullopt;
    }
    rates.insert("USD", 1.0);  // base currency, not listed by the API
    return message("fiat_rates", QJsonObject{{"rates", rates}});
}

std::optional<QJsonObject> crowdfundingMessage(const QByteArray &body) {
    const auto value = parse(body);
    if (!value || !value->isObject()) {
        return std::nullopt;
    }

    QJsonArray proposals;
    for (const QJsonValue &entry : value->toObject().value("data").toArray()) {
        QJsonObject obj = entry.toObject();
        if (obj.value("state").toString() != QLatin1String("FUNDING-REQUIRED")) {
            continue;
        }
        // Fields the Feather service used to add.
        obj.insert("organizer", "CCS");
        obj.insert("currency", "XMR");
        obj.insert("urlpath", "funding-required/");
        proposals.append(obj);
    }
    return message("ccs", proposals);
}

}
