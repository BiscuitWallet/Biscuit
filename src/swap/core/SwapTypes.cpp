// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "SwapTypes.h"

#include <QJsonValue>

#include "Amount.h"

namespace biscuit::swap {

QString Asset::displayName() const {
    const QString upper = ticker.toUpper();
    if (network.isEmpty() || network.compare(QLatin1String("Mainnet"), Qt::CaseInsensitive) == 0) {
        return upper;
    }
    return QString("%1 (%2)").arg(upper, network);
}

// ---------------------------------------------------------------- KYC rating

KycRating kycRatingFromString(const QString &value) {
    const QString v = value.trimmed().toUpper();
    if (v == QLatin1String("A")) return KycRating::A;
    if (v == QLatin1String("B")) return KycRating::B;
    if (v == QLatin1String("C")) return KycRating::C;
    if (v == QLatin1String("D")) return KycRating::D;
    return KycRating::Unknown;
}

QString kycRatingToString(KycRating rating) {
    switch (rating) {
        case KycRating::A: return QStringLiteral("A");
        case KycRating::B: return QStringLiteral("B");
        case KycRating::C: return QStringLiteral("C");
        case KycRating::D: return QStringLiteral("D");
        case KycRating::Unknown: break;
    }
    return QStringLiteral("?");
}

QString kycRatingDescription(KycRating rating) {
    switch (rating) {
        case KycRating::A: return QStringLiteral("No KYC");
        case KycRating::B: return QStringLiteral("KYC rarely requested");
        case KycRating::C: return QStringLiteral("KYC may be requested");
        case KycRating::D: return QStringLiteral("KYC possible, funds may be held");
        case KycRating::Unknown: break;
    }
    return QStringLiteral("Unknown KYC policy");
}

bool kycRatingAtLeast(KycRating rating, KycRating minimum) {
    if (rating == KycRating::Unknown || minimum == KycRating::Unknown) {
        return false;
    }
    return static_cast<int>(rating) <= static_cast<int>(minimum);
}

// -------------------------------------------------------------- Trade status

namespace {
    struct StatusName {
        TradeStatus status;
        const char *name;
    };

    // Names as used by Trocador.
    constexpr StatusName statusNames[] = {
        {TradeStatus::New, "new"},
        {TradeStatus::Waiting, "waiting"},
        {TradeStatus::Confirming, "confirming"},
        {TradeStatus::Sending, "sending"},
        {TradeStatus::PaidPartially, "paid partially"},
        {TradeStatus::Finished, "finished"},
        {TradeStatus::Failed, "failed"},
        {TradeStatus::Expired, "expired"},
        {TradeStatus::Halted, "halted"},
        {TradeStatus::Refunded, "refunded"},
    };

    // Position on the normal path of a trade, -1 for side states.
    int progress(TradeStatus status) {
        switch (status) {
            case TradeStatus::New: return 0;
            case TradeStatus::Waiting: return 1;
            case TradeStatus::PaidPartially: return 2;
            case TradeStatus::Confirming: return 3;
            case TradeStatus::Sending: return 4;
            case TradeStatus::Finished: return 5;
            default: return -1;
        }
    }
}

TradeStatus tradeStatusFromString(const QString &value) {
    QString v = value.trimmed().toLower();
    v.replace(QLatin1Char('_'), QLatin1Char(' '));
    for (const auto &entry : statusNames) {
        if (v == QLatin1String(entry.name)) {
            return entry.status;
        }
    }
    return TradeStatus::Unknown;
}

QString tradeStatusToString(TradeStatus status) {
    for (const auto &entry : statusNames) {
        if (entry.status == status) {
            return QString::fromLatin1(entry.name);
        }
    }
    return QStringLiteral("unknown");
}

QString tradeStatusDescription(TradeStatus status) {
    switch (status) {
        case TradeStatus::New: return QStringLiteral("Trade created");
        case TradeStatus::Waiting: return QStringLiteral("Waiting for your deposit");
        case TradeStatus::Confirming: return QStringLiteral("Deposit received, waiting for confirmations");
        case TradeStatus::Sending: return QStringLiteral("Exchange is sending your coins");
        case TradeStatus::PaidPartially: return QStringLiteral("Deposit is lower than expected");
        case TradeStatus::Finished: return QStringLiteral("Finished");
        case TradeStatus::Failed: return QStringLiteral("Failed, contact support");
        case TradeStatus::Expired: return QStringLiteral("Expired, no deposit received in time");
        case TradeStatus::Halted: return QStringLiteral("Halted by the exchange, contact support");
        case TradeStatus::Refunded: return QStringLiteral("Refunded");
        case TradeStatus::Unknown: break;
    }
    return QStringLiteral("Unknown status");
}

bool isTerminal(TradeStatus status) {
    // Expired, Failed and Halted are not terminal: a late deposit or a support
    // intervention can still move the trade forward.
    return status == TradeStatus::Finished || status == TradeStatus::Refunded;
}

bool needsSupport(TradeStatus status) {
    return status == TradeStatus::Failed || status == TradeStatus::Halted;
}

bool canTransition(TradeStatus from, TradeStatus to) {
    if (from == to || to == TradeStatus::Unknown || isTerminal(from)) {
        return false;
    }

    const int fromProgress = progress(from);
    const int toProgress = progress(to);

    // Any open trade can fail, halt, expire or be refunded.
    if (toProgress < 0) {
        return true;
    }
    // Leaving a side state (late deposit, support fixed the trade).
    if (fromProgress < 0) {
        return true;
    }
    return toProgress > fromProgress;
}

bool applyStatus(Trade &trade, TradeStatus status, const QDateTime &now) {
    if (!canTransition(trade.status, status)) {
        return false;
    }
    trade.status = status;
    trade.updatedAt = now;
    return true;
}

// --------------------------------------------------------------------- JSON

namespace {
    QJsonObject assetToJson(const Asset &asset) {
        return {{"ticker", asset.ticker}, {"network", asset.network}};
    }

    Asset assetFromJson(const QJsonValue &value) {
        const QJsonObject obj = value.toObject();
        return {obj.value("ticker").toString(), obj.value("network").toString()};
    }
}

QJsonObject Trade::toJson() const {
    QJsonObject obj;
    obj["version"] = 1;
    obj["provider_id"] = providerId;
    obj["trade_id"] = tradeId;
    obj["exchange"] = exchange;
    obj["exchange_trade_id"] = exchangeTradeId;
    obj["exchange_password"] = exchangePassword;
    obj["support_url"] = supportUrl;
    obj["from"] = assetToJson(from);
    obj["to"] = assetToJson(to);
    obj["amount_from"] = amountFrom;
    obj["amount_to"] = amountTo;
    obj["rate_type"] = rateType == RateType::Fixed ? "fixed" : "floating";
    obj["kyc_rating"] = kycRatingToString(kycRating);
    obj["deposit_address"] = depositAddress;
    obj["deposit_memo"] = depositMemo;
    obj["payout_address"] = payoutAddress;
    obj["payout_memo"] = payoutMemo;
    obj["refund_address"] = refundAddress;
    obj["refund_memo"] = refundMemo;
    obj["status"] = tradeStatusToString(status);
    obj["created_at"] = createdAt.toUTC().toString(Qt::ISODate);
    obj["updated_at"] = updatedAt.toUTC().toString(Qt::ISODate);
    obj["deposit_txid"] = depositTxId;
    return obj;
}

std::optional<Trade> Trade::fromJson(const QJsonObject &obj) {
    if (obj.value("version").toInt() != 1) {
        return std::nullopt;
    }

    Trade t;
    t.providerId = obj.value("provider_id").toString();
    t.tradeId = obj.value("trade_id").toString();
    t.exchange = obj.value("exchange").toString();
    t.exchangeTradeId = obj.value("exchange_trade_id").toString();
    t.exchangePassword = obj.value("exchange_password").toString();
    t.supportUrl = obj.value("support_url").toString();
    t.from = assetFromJson(obj.value("from"));
    t.to = assetFromJson(obj.value("to"));
    t.amountFrom = obj.value("amount_from").toString();
    t.amountTo = obj.value("amount_to").toString();
    t.rateType = obj.value("rate_type").toString() == QLatin1String("fixed") ? RateType::Fixed : RateType::Floating;
    t.kycRating = kycRatingFromString(obj.value("kyc_rating").toString());
    t.depositAddress = obj.value("deposit_address").toString();
    t.depositMemo = obj.value("deposit_memo").toString();
    t.payoutAddress = obj.value("payout_address").toString();
    t.payoutMemo = obj.value("payout_memo").toString();
    t.refundAddress = obj.value("refund_address").toString();
    t.refundMemo = obj.value("refund_memo").toString();
    t.status = tradeStatusFromString(obj.value("status").toString());
    t.createdAt = QDateTime::fromString(obj.value("created_at").toString(), Qt::ISODate);
    t.updatedAt = QDateTime::fromString(obj.value("updated_at").toString(), Qt::ISODate);
    t.depositTxId = obj.value("deposit_txid").toString();

    if (t.providerId.isEmpty() || t.tradeId.isEmpty() || !t.from.isValid() || !t.to.isValid()) {
        return std::nullopt;
    }
    if (!amount::isValid(t.amountFrom) || !amount::isValid(t.amountTo)) {
        return std::nullopt;
    }
    return t;
}

QList<AssetInfo> walletAssets(const QList<AssetInfo> &partnerAssets) {
    QList<AssetInfo> result;
    for (const char *ticker : {"xmr", "btc", "ltc"}) {
        const Asset wanted{ticker, "Mainnet"};
        for (const AssetInfo &info : partnerAssets) {
            if (info.asset == wanted) {
                result.append(info);
                break;
            }
        }
    }
    return result;
}

}
