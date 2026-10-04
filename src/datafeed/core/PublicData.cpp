// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PublicData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace biscuit::datafeed {

namespace {
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

QString siteUrl() {
    return "https://biscuitwallet.com";
}

QString onionSiteUrl() {
    return "http://biscuit6qpejzxfr7us7oibhjasvrozfeno7xonffzzoj4lmw6o3kbyd.onion";
}

QString onionUrl(const QString &url) {
    // Plain http is enough there: the onion address authenticates the server
    // and Tor encrypts the connection end to end.
    const QString site = siteUrl();
    if (url == site || url.startsWith(site + "/")) {
        return onionSiteUrl() + url.mid(site.size());
    }
    return url;
}

// Biscuit's own service (biscuitwallet.com) fetches CoinGecko, the ECB rates
// and the Monero CCS list and serves them unchanged: the app only talks to
// that host, and the sources never see users' IP addresses.
QString cryptoRatesUrl() {
    return "https://biscuitwallet.com/data/crypto.json";
}

QString fiatRatesUrl() {
    return "https://biscuitwallet.com/data/fiat.json";
}

QString crowdfundingUrl() {
    return "https://biscuitwallet.com/data/ccs.json";
}

QString updatesUrl() {
    return "https://biscuitwallet.com/updates.json";
}

std::optional<QJsonObject> updatesMessage(const QByteArray &body) {
    const auto value = parse(body);
    if (!value || !value->isObject() || !value->toObject().value("platform").isObject()) {
        return std::nullopt;
    }
    return message("updates", value->toObject());
}

QString newsUrl() {
    return "https://biscuitwallet.com/news/feed.xml";
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

std::optional<QJsonObject> newsMessage(const QByteArray &body) {
    constexpr int maxItems = 20;
    constexpr int maxTitle = 120;
    constexpr int maxSummary = 400;

    struct Item { QString title, url, date, summary; bool pinned = false; };
    QList<Item> items;
    Item current;
    bool inEntry = false;
    bool isFeed = false;

    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("feed")) {
                isFeed = true;
            } else if (name == QLatin1String("entry")) {
                inEntry = true;
                current = {};
            } else if (inEntry && name == QLatin1String("title")) {
                current.title = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            } else if (inEntry && name == QLatin1String("summary")) {
                current.summary = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            } else if (inEntry && name == QLatin1String("updated")) {
                current.date = xml.readElementText().left(10);
            } else if (inEntry && name == QLatin1String("link") && current.url.isEmpty()) {
                current.url = xml.attributes().value("href").toString();
            } else if (inEntry && name == QLatin1String("category")
                       && xml.attributes().value("term") == QLatin1String("pinned")) {
                current.pinned = true;
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("entry")) {
            inEntry = false;
            // Only our own pages: a tampered feed cannot send users elsewhere.
            const QUrl url(current.url);
            const bool ours = url.scheme() == QLatin1String("https") && url.host() == QLatin1String("biscuitwallet.com")
                              && url.userInfo().isEmpty() && url.port() == -1;
            const bool dated = QDate::fromString(current.date, Qt::ISODate).isValid();
            if (ours && dated && !current.title.isEmpty()) {
                items.append(current);
            }
        }
    }
    if (xml.hasError() || !isFeed) {
        return std::nullopt;
    }

    // Pinned posts first, then newest first.
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.pinned != b.pinned ? a.pinned : a.date > b.date;
    });
    QJsonArray news;
    for (const Item &item : items.mid(0, maxItems)) {
        news.append(QJsonObject{
            {"title", item.title.left(maxTitle)},
            {"url", item.url},
            {"date", item.date},
            {"summary", item.summary.left(maxSummary)},
            {"pinned", item.pinned},
        });
    }
    return message("news", news);
}

}
