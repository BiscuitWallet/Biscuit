// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "ExchangesApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Amount.h"

namespace biscuit::swap::exchanges {

namespace {
    void setError(QString *error, const QString &message) {
        if (error) {
            *error = message;
        }
    }

    std::optional<QJsonObject> parseObject(const QByteArray &body, QString *error) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            setError(error, QStringLiteral("Unexpected response"));
            return std::nullopt;
        }
        const QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            setError(error, parseErrorMessage(body));
            return std::nullopt;
        }
        return obj;
    }

    Asset assetOf(const QJsonValue &value) {
        const QJsonObject obj = value.toObject();
        return {obj.value("ticker").toString(), obj.value("network").toString()};
    }

    QString amountOf(const QJsonValue &value) {
        return amount::normalize(value.toString().trimmed());
    }

    void addAsset(QUrlQuery &query, const QString &prefix, const Asset &asset) {
        query.addQueryItem(prefix, asset.ticker);
        query.addQueryItem(prefix + "_network", asset.network);
    }
}

QUrlQuery quotesQuery(const QuoteRequest &request) {
    QUrlQuery query;
    addAsset(query, "from", request.from);
    addAsset(query, "to", request.to);
    query.addQueryItem("amount", request.amount());
    query.addQueryItem("fixed", request.rateType == RateType::Fixed ? "1" : "0");
    return query;
}

QUrlQuery newTradeQuery(const TradeRequest &request) {
    const Quote &q = request.quote;
    const bool fixed = q.rateType == RateType::Fixed;
    QUrlQuery query;
    query.addQueryItem("exchange", q.rateId);
    addAsset(query, "from", q.from);
    addAsset(query, "to", q.to);
    query.addQueryItem("amount", fixed ? q.amountTo : q.amountFrom);
    query.addQueryItem("fixed", fixed ? "1" : "0");
    query.addQueryItem("address", request.payoutAddress);
    if (!request.payoutMemo.isEmpty()) {
        query.addQueryItem("memo", request.payoutMemo);
    }
    if (!request.refundAddress.isEmpty()) {
        query.addQueryItem("refund", request.refundAddress);
        if (!request.refundMemo.isEmpty()) {
            query.addQueryItem("refund_memo", request.refundMemo);
        }
    }
    return query;
}

QUrlQuery tradeQuery(const Trade &trade) {
    QUrlQuery query;
    query.addQueryItem("id", trade.tradeId);
    query.addQueryItem("address", trade.payoutAddress);
    return query;
}

std::optional<QList<AssetInfo>> parseAssets(const QByteArray &body, QString *error) {
    const auto obj = parseObject(body, error);
    if (!obj) {
        return std::nullopt;
    }
    QList<AssetInfo> assets;
    for (const QJsonValue &v : obj->value("assets").toArray()) {
        AssetInfo info;
        info.asset = assetOf(v);
        info.name = v.toObject().value("name").toString();
        info.hasMemo = v.toObject().value("memo").toBool();   // XRP, XLM…
        if (info.asset.isValid()) {
            assets.append(info);
        }
    }
    if (assets.isEmpty()) {
        setError(error, QStringLiteral("No coins available"));
        return std::nullopt;
    }
    return assets;
}

std::optional<QuotesResult> parseQuotes(const QByteArray &body, const QuoteRequest &request, QString *error) {
    const auto obj = parseObject(body, error);
    if (!obj) {
        return std::nullopt;
    }
    QuotesResult result;
    for (const QJsonValue &v : obj->value("errors").toArray()) {
        if (!v.toString().isEmpty()) {
            result.errors.append(v.toString().left(300));
        }
    }
    for (const QJsonValue &v : obj->value("quotes").toArray()) {
        const QJsonObject q = v.toObject();
        Quote quote;
        quote.providerId = providerId;
        quote.exchange = q.value("name").toString();
        quote.rateId = q.value("exchange").toString();   // the relay's exchange id
        quote.from = request.from;
        quote.to = request.to;
        quote.amountFrom = amountOf(q.value("amount_from"));
        quote.amountTo = amountOf(q.value("amount_to"));
        quote.rateType = q.value("fixed").toBool() ? RateType::Fixed : RateType::Floating;
        quote.kycRating = kycRatingFromString(q.value("kyc").toString());
        if (quote.exchange.isEmpty() || quote.rateId.isEmpty() || quote.rateType != request.rateType
            || quote.amountFrom.isEmpty() || quote.amountTo.isEmpty()
            || amount::isZero(quote.amountFrom) || amount::isZero(quote.amountTo)) {
            continue;
        }
        result.quotes.append(quote);
    }
    return result;
}

std::optional<Trade> parseTrade(const QByteArray &body, QString *error) {
    const auto obj = parseObject(body, error);
    if (!obj) {
        return std::nullopt;
    }
    Trade t;
    t.providerId = providerId;
    t.tradeId = obj->value("id").toString();
    t.exchange = obj->value("name").toString();
    t.exchangeTradeId = obj->value("exchange_id").toString();
    t.supportUrl = obj->value("support").toString();
    t.from = assetOf(obj->value("from"));
    t.to = assetOf(obj->value("to"));
    t.amountFrom = amountOf(obj->value("amount_from"));
    t.amountTo = amountOf(obj->value("amount_to"));
    t.rateType = obj->value("fixed").toBool() ? RateType::Fixed : RateType::Floating;
    t.kycRating = kycRatingFromString(obj->value("kyc").toString());
    t.depositAddress = obj->value("deposit_address").toString();
    t.depositMemo = obj->value("deposit_memo").toString();
    t.payoutAddress = obj->value("address").toString();
    t.payoutMemo = obj->value("memo").toString();
    t.refundAddress = obj->value("refund").toString();
    t.status = tradeStatusFromString(obj->value("status").toString());
    t.createdAt = QDateTime::fromString(obj->value("created_at").toString(), Qt::ISODateWithMs);

    if (t.tradeId.isEmpty() || t.exchange.isEmpty() || !t.from.isValid() || !t.to.isValid()
        || t.depositAddress.isEmpty() || t.amountFrom.isEmpty() || t.amountTo.isEmpty()) {
        setError(error, QStringLiteral("Incomplete swap from the exchange"));
        return std::nullopt;
    }
    return t;
}

QString parseErrorMessage(const QByteArray &body) {
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString message = obj.value("message").toString();
    if (!message.isEmpty()) {
        return message.left(300);
    }
    return obj.value("error").toString().left(100);
}

}
