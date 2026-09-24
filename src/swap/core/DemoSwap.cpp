// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "DemoSwap.h"

#include <QCryptographicHash>

#include "Amount.h"

namespace biscuit::swap::demo {

namespace {
    struct DemoAsset {
        const char *ticker;
        const char *network;
        const char *name;
        double xmrValue;   // fake price of 1 unit, in XMR
        const char *minimum;
    };

    constexpr DemoAsset demoAssets[] = {
        {"xmr", "Mainnet", "Monero", 1.0, "0.01"},
        {"btc", "Mainnet", "Bitcoin", 240.0, "0.0001"},
        {"ltc", "Mainnet", "Litecoin", 0.45, "0.01"},
        {"eth", "ERC20", "Ethereum", 11.0, "0.005"},
        {"usdt", "ERC20", "Tether (ERC20)", 0.004, "10"},
        {"usdt", "TRC20", "Tether (TRC20)", 0.004, "10"},
    };

    struct DemoExchange {
        const char *name;
        KycRating kyc;
        double spread;     // fraction lost to the exchange
        int eta;           // minutes
    };

    constexpr DemoExchange demoExchanges[] = {
        {"Demo Exchange A", KycRating::A, 0.012, 20},
        {"Demo Exchange B", KycRating::B, 0.008, 15},
        {"Demo Exchange C", KycRating::C, 0.005, 30},
        {"Demo Halt Exchange", KycRating::D, 0.020, 10},
    };

    const DemoAsset *findAsset(const Asset &asset) {
        for (const auto &a : demoAssets) {
            if (asset.ticker == QLatin1String(a.ticker) && asset.network == QLatin1String(a.network)) {
                return &a;
            }
        }
        return nullptr;
    }
}

QList<AssetInfo> assets() {
    QList<AssetInfo> list;
    for (const auto &a : demoAssets) {
        list.append({{a.ticker, a.network}, a.name, a.minimum, QString(), false});
    }
    return list;
}

QList<Quote> quotes(const QuoteRequest &request) {
    const DemoAsset *from = findAsset(request.from);
    const DemoAsset *to = findAsset(request.to);
    if (!from || !to || from == to || !amount::isValid(request.amountFrom) || amount::isZero(request.amountFrom)) {
        return {};
    }

    // Display-only arithmetic: demo amounts are never sent anywhere.
    const double amountFrom = request.amountFrom.toDouble();
    const double gross = amountFrom * from->xmrValue / to->xmrValue;

    const QString rateId = QString("demo-rate-%1").arg(QString::fromLatin1(
            QCryptographicHash::hash((request.from.ticker + request.to.ticker + request.amountFrom).toUtf8(),
                                     QCryptographicHash::Sha256).toHex().left(12)));

    QList<Quote> result;
    for (const auto &ex : demoExchanges) {
        Quote q;
        q.providerId = providerId;
        q.exchange = ex.name;
        q.rateId = rateId;
        q.from = request.from;
        q.to = request.to;
        q.amountFrom = amount::normalize(request.amountFrom);
        q.amountTo = amount::fromDouble(gross * (1.0 - ex.spread));
        q.rateType = request.rateType;
        q.kycRating = ex.kyc;
        q.etaMinutes = ex.eta;
        result.append(q);
    }
    return result;
}

Trade createTrade(const TradeRequest &request, const QDateTime &now) {
    const Quote &q = request.quote;
    const QByteArray seed = (q.rateId + q.exchange + request.payoutAddress + now.toString(Qt::ISODateWithMs)).toUtf8();
    const QString id = QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(10));

    Trade t;
    t.providerId = providerId;
    t.tradeId = "DEMO" + id.toUpper();
    t.exchange = q.exchange;
    t.exchangeTradeId = "demo-" + id;
    t.from = q.from;
    t.to = q.to;
    t.amountFrom = q.amountFrom;
    t.amountTo = q.amountTo;
    t.rateType = q.rateType;
    t.kycRating = q.kycRating;
    t.depositAddress = QString(depositAddressPrefix) + "NOT-A-REAL-ADDRESS-" + id;
    t.payoutAddress = request.payoutAddress;
    t.payoutMemo = request.payoutMemo;
    t.refundAddress = request.refundAddress;
    t.refundMemo = request.refundMemo;
    t.status = TradeStatus::Waiting;
    t.createdAt = now;
    t.updatedAt = now;
    return t;
}

TradeStatus statusAt(const Trade &trade, const QDateTime &now) {
    const qint64 seconds = trade.createdAt.secsTo(now);
    if (trade.exchange.contains(QLatin1String("Halt")) && seconds >= 90) {
        return TradeStatus::Halted;
    }
    if (seconds >= 180) return TradeStatus::Finished;
    if (seconds >= 120) return TradeStatus::Sending;
    if (seconds >= 60) return TradeStatus::Confirming;
    return TradeStatus::Waiting;
}

bool isDemoAddress(const QString &address) {
    return address.startsWith(QLatin1String(depositAddressPrefix));
}

}
