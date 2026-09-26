// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "TrocadorApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Amount.h"

namespace biscuit::swap::trocador {

namespace {
    void setError(QString *error, const QString &message) {
        if (error) {
            *error = message;
        }
    }

    // Trocador returns amounts as JSON numbers or strings.
    QString amountValue(const QJsonValue &value) {
        if (value.isString()) {
            return amount::normalize(value.toString().trimmed());
        }
        if (value.isDouble()) {
            return amount::fromDouble(value.toDouble());
        }
        return {};
    }

    QString memoOrZero(const QString &memo) {
        return memo.isEmpty() ? QStringLiteral("0") : memo;
    }

    QString memoFromApi(const QJsonValue &value) {
        const QString memo = value.toString();
        return (memo == QLatin1String("0") || memo == QLatin1String("None")) ? QString() : memo;
    }

    std::optional<QJsonObject> parseObject(const QByteArray &body, QString *error) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            setError(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
            return std::nullopt;
        }
        QJsonObject obj;
        if (doc.isObject()) {
            obj = doc.object();
        } else if (doc.isArray() && !doc.array().isEmpty() && doc.array().first().isObject()) {
            obj = doc.array().first().toObject();
        } else {
            setError(error, QStringLiteral("Unexpected response"));
            return std::nullopt;
        }
        if (obj.contains("error")) {
            setError(error, parseErrorMessage(body));
            return std::nullopt;
        }
        return obj;
    }

    void addAssets(QUrlQuery &q, const Asset &from, const Asset &to) {
        q.addQueryItem("ticker_from", from.ticker);
        q.addQueryItem("network_from", from.network);
        q.addQueryItem("ticker_to", to.ticker);
        q.addQueryItem("network_to", to.network);
    }
}

QUrlQuery rateQuery(const QuoteRequest &request) {
    QUrlQuery q;
    addAssets(q, request.from, request.to);
    // Fixed rate is Trocador's "payment" mode: the amount to receive is given.
    const bool fixed = request.rateType == RateType::Fixed;
    q.addQueryItem(fixed ? "amount_to" : "amount_from", amount::normalize(request.amount()));
    q.addQueryItem("payment", fixed ? "True" : "False");
    q.addQueryItem("min_kycrating", kycRatingToString(request.minKycRating));
    q.addQueryItem("markup", markup);
    return q;
}

QUrlQuery newTradeQuery(const TradeRequest &request, const QString &rateId) {
    const Quote &quote = request.quote;
    QUrlQuery q;
    q.addQueryItem("id", rateId);
    addAssets(q, quote.from, quote.to);
    const bool fixed = quote.rateType == RateType::Fixed;
    q.addQueryItem(fixed ? "amount_to" : "amount_from", amount::normalize(fixed ? quote.amountTo : quote.amountFrom));
    q.addQueryItem("address", request.payoutAddress);
    q.addQueryItem("address_memo", memoOrZero(request.payoutMemo));
    q.addQueryItem("refund", request.refundAddress);
    q.addQueryItem("refund_memo", memoOrZero(request.refundMemo));
    q.addQueryItem("provider", quote.exchange);
    q.addQueryItem("payment", fixed ? "True" : "False");
    q.addQueryItem("markup", markup);
    return q;
}

QUrlQuery tradeQuery(const QString &tradeId) {
    QUrlQuery q;
    q.addQueryItem("id", tradeId);
    return q;
}

QUrlQuery validateAddressQuery(const Asset &asset, const QString &address) {
    QUrlQuery q;
    q.addQueryItem("ticker", asset.ticker);
    q.addQueryItem("network", asset.network);
    q.addQueryItem("address", address);
    return q;
}

std::optional<QList<AssetInfo>> parseCoins(const QByteArray &body, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        setError(error, doc.isObject() ? parseErrorMessage(body) : QStringLiteral("Invalid coin list"));
        return std::nullopt;
    }

    QList<AssetInfo> coins;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        AssetInfo info;
        info.asset = {obj.value("ticker").toString().toLower(), obj.value("network").toString()};
        info.name = obj.value("name").toString();
        info.minimum = amountValue(obj.value("minimum"));
        info.maximum = amountValue(obj.value("maximum"));
        info.hasMemo = obj.value("memo").toBool();  // to verify
        if (info.asset.isValid()) {
            coins.append(info);
        }
    }
    return coins;
}

std::optional<RateResult> parseRate(const QByteArray &body, const QuoteRequest &request, QString *error) {
    const auto obj = parseObject(body, error);
    if (!obj) {
        return std::nullopt;
    }

    RateResult result;
    result.rateId = obj->value("trade_id").toString();
    if (result.rateId.isEmpty()) {
        setError(error, QStringLiteral("Missing rate id"));
        return std::nullopt;
    }

    // Floating: each quote gives amount_to for the requested amount_from.
    // Fixed: each quote gives amount_from for the requested amount_to.
    const QString amountFrom = amountValue(obj->value("amount_from"));
    const QString amountTo = amountValue(obj->value("amount_to"));
    const QJsonArray quotes = obj->value("quotes").toObject().value("quotes").toArray();
    for (const QJsonValue &value : quotes) {
        const QJsonObject q = value.toObject();
        Quote quote;
        quote.providerId = providerId;
        quote.exchange = q.value("provider").toString();
        quote.rateId = result.rateId;
        quote.from = request.from;
        quote.to = request.to;
        const bool fixed = request.rateType == RateType::Fixed;
        quote.amountFrom = fixed ? amountValue(q.value("amount_from"))
                                 : (amountFrom.isEmpty() ? amount::normalize(request.amountFrom) : amountFrom);
        quote.amountTo = fixed ? (amountTo.isEmpty() ? amount::normalize(request.amountTo) : amountTo)
                               : amountValue(q.value("amount_to"));
        quote.rateType = request.rateType;
        quote.kycRating = kycRatingFromString(q.value("kycrating").toString());
        const QJsonValue eta = q.value("eta");  // minutes
        if (eta.isDouble()) {
            quote.etaMinutes = static_cast<int>(eta.toDouble());
        }
        if (!quote.exchange.isEmpty() && !quote.amountFrom.isEmpty() && !quote.amountTo.isEmpty()) {
            result.quotes.append(quote);
        }
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
    t.tradeId = obj->value("trade_id").toString();
    t.exchange = obj->value("provider").toString();
    t.exchangeTradeId = obj->value("id_provider").toString();
    t.exchangePassword = obj->value("password").toString();
    t.from = {obj->value("ticker_from").toString().toLower(), obj->value("network_from").toString()};
    t.to = {obj->value("ticker_to").toString().toLower(), obj->value("network_to").toString()};
    t.amountFrom = amountValue(obj->value("amount_from"));
    t.amountTo = amountValue(obj->value("amount_to"));
    t.rateType = obj->value("fixed").toBool() || obj->value("payment").toBool() ? RateType::Fixed : RateType::Floating;
    t.depositAddress = obj->value("address_provider").toString();
    t.depositMemo = memoFromApi(obj->value("address_provider_memo"));
    t.payoutAddress = obj->value("address_user").toString();
    t.payoutMemo = memoFromApi(obj->value("address_user_memo"));
    t.refundAddress = obj->value("refund_address").toString();
    t.refundMemo = memoFromApi(obj->value("refund_address_memo"));
    t.status = tradeStatusFromString(obj->value("status").toString());
    t.createdAt = QDateTime::fromString(obj->value("date").toString(), Qt::ISODate);

    if (t.tradeId.isEmpty() || t.depositAddress.isEmpty() || t.status == TradeStatus::Unknown) {
        setError(error, QStringLiteral("Incomplete trade data"));
        return std::nullopt;
    }
    return t;
}

std::optional<bool> parseValidateAddress(const QByteArray &body, QString *error) {
    const auto obj = parseObject(body, error);
    if (!obj) {
        return std::nullopt;
    }
    const QJsonValue result = obj->value("result");
    if (!result.isBool()) {
        setError(error, QStringLiteral("Unexpected response"));
        return std::nullopt;
    }
    return result.toBool();
}

QString parseErrorMessage(const QByteArray &body) {
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString err = obj.value("error").toString();
    const QString message = obj.value("message").toString();
    if (!err.isEmpty() && !message.isEmpty()) {
        return QString("%1: %2").arg(err, message);
    }
    return err.isEmpty() ? message : err;
}

QString parseRelayError(const QByteArray &body) {
    static const QStringList relayCodes = {
        "tor_exit", "rate_limited", "unavailable", "upstream_unreachable", "upstream_error",
        "not_found", "method_not_allowed", "query_too_long", "bad_query", "internal",
    };
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    if (!relayCodes.contains(obj.value("error").toString())) {
        return {};
    }
    return obj.value("message").toString();
}

}
